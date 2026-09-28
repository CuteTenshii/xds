#include "check.h"
#include "synthetic.h"

#include <xds/xds.h>

#include <zlib.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

using xds::Swizzle;

constexpr std::array<Swizzle, 4> kArgb{Swizzle::Z, Swizzle::Y, Swizzle::X, Swizzle::W};
constexpr std::array<Swizzle, 4> kAlphaOnly{Swizzle::One, Swizzle::One, Swizzle::One, Swizzle::W};

using Pixel = std::array<std::uint8_t, 4>;

/// Parses a header with the given fetch constant and `dataSize` zero bytes.
xds::Texture fromFetch(const std::array<std::uint32_t, 6>& fetch, std::size_t dataSize)
{
    std::vector<std::uint8_t> bytes(xds::kHeaderSize + dataSize, 0);
    synthetic::putBe32(bytes.data(), 3);
    for (std::size_t i = 0; i < fetch.size(); ++i) {
        synthetic::putBe32(bytes.data() + 28 + 4 * i, fetch[i]);
    }
    return xds::Texture::parse(std::move(bytes));
}

Pixel pixel(const xds::Image& image, std::uint32_t x, std::uint32_t y)
{
    const std::uint8_t* p = image.rgba.data() + (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {p[0], p[1], p[2], p[3]};
}

Pixel topLeftPixel(const synthetic::Spec& spec,
    const std::function<std::vector<std::uint8_t>(std::uint32_t, std::uint32_t, std::uint32_t)>& block)
{
    return pixel(xds::Texture::parse(synthetic::build(spec, block)).decodeLevel(0), 0, 0);
}

std::uint32_t le32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint32_t be32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
        | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

/// A DXT color block: RGB565 endpoints and a 2-bit index per texel.
std::vector<std::uint8_t> colorBlock(std::uint16_t c0, std::uint16_t c1, std::uint32_t indices)
{
    return {static_cast<std::uint8_t>(c0), static_cast<std::uint8_t>(c0 >> 8), static_cast<std::uint8_t>(c1),
        static_cast<std::uint8_t>(c1 >> 8), static_cast<std::uint8_t>(indices),
        static_cast<std::uint8_t>(indices >> 8), static_cast<std::uint8_t>(indices >> 16),
        static_cast<std::uint8_t>(indices >> 24)};
}

/// An interpolated 8-bit block (DXT5 alpha, DXT5A, half of DXN) in which
/// every texel uses the 3-bit `index`.
std::vector<std::uint8_t> interpolatedBlock(std::uint8_t a0, std::uint8_t a1, std::uint32_t index)
{
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) {
        bits |= static_cast<std::uint64_t>(index) << (3 * i);
    }
    std::vector<std::uint8_t> block{a0, a1};
    for (int i = 0; i < 6; ++i) {
        block.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    }
    return block;
}

} // namespace

TEST_CASE("tiled addressing maps every block of a surface to its own slot")
{
    for (const std::uint32_t log2Bytes : {2u, 3u, 4u}) {
        const std::uint32_t pitch = 64;
        const std::uint32_t height = 64;
        const std::uint32_t blockBytes = 1u << log2Bytes;
        std::set<std::uint32_t> seen;
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < pitch; ++x) {
                const std::uint32_t offset = xds::tiledOffset2D(x, y, pitch, log2Bytes);
                CHECK(offset % blockBytes == 0);
                CHECK(offset < pitch * height * blockBytes);
                seen.insert(offset);
            }
        }
        CHECK_EQ(seen.size(), std::size_t{pitch} * height);
    }
}

TEST_CASE("fetch constant fields decode from a real texture header")
{
    // tracks/treadmark.xds
    const xds::TextureInfo info =
        xds::decodeFetchConstant({0x840000FE, 0x00000075, 0x000FE1FF, 0x00000D10, 0x00000240, 0x00010A00});
    CHECK_EQ(info.format, 53u);
    CHECK(info.endian == xds::Endian::Swap8In16);
    CHECK(info.dimension == xds::Dimension::TwoD);
    CHECK(info.tiled);
    CHECK(info.packedMips);
    CHECK_EQ(info.width, 512u);
    CHECK_EQ(info.height, 128u);
    CHECK_EQ(info.pitch, 512u);
    CHECK_EQ(info.levelCount(), 10u);
    CHECK_EQ(info.baseAddress, 0u);
    CHECK_EQ(info.mipAddress, 0x10000u);
    CHECK(xds::swizzleString(info.swizzle) == "XYZW");
    CHECK(xds::formatName(info.format) == "DXT4_5_AS_16_16_16_16");
    CHECK(xds::describe(info) == "DXT4_5_AS_16_16_16_16 512x128, 10 levels, tiled, swizzle XYZW");
}

TEST_CASE("fetch constants encode back to the fields they decode to")
{
    const auto sameFields = [](const xds::TextureInfo& a, const xds::TextureInfo& b) {
        return a.format == b.format && a.endian == b.endian && a.dimension == b.dimension && a.tiled == b.tiled
            && a.packedMips == b.packedMips && a.signedFormat == b.signedFormat && a.width == b.width
            && a.height == b.height && a.depth == b.depth && a.pitch == b.pitch && a.baseAddress == b.baseAddress
            && a.mipAddress == b.mipAddress && a.minMipLevel == b.minMipLevel && a.maxMipLevel == b.maxMipLevel
            && a.swizzle == b.swizzle && a.expAdjust == b.expAdjust;
    };
    // tracks/treadmark.xds, and a packed 12x80 texture from a car's gauges.
    for (const std::array<std::uint32_t, 6>& fetch :
        {std::array<std::uint32_t, 6>{0x840000FE, 0x00000075, 0x000FE1FF, 0x00000D10, 0x00000240, 0x00010A00},
            std::array<std::uint32_t, 6>{0x810000FE, 0x00000075, 0x0026A00B, 0x00000D10, 0x00000000, 0x00000A00}}) {
        const xds::TextureInfo info = xds::decodeFetchConstant(fetch);
        CHECK(sameFields(xds::decodeFetchConstant(xds::encodeFetchConstant(info)), info));
    }

    xds::TextureInfo info;
    info.format = 49;
    info.endian = xds::Endian::Swap8In32;
    info.tiled = true;
    info.packedMips = true;
    info.width = 300;
    info.height = 7;
    info.pitch = 384;
    info.baseAddress = 0x3000;
    info.mipAddress = 0x9000;
    info.maxMipLevel = 8;
    info.swizzle = {xds::Swizzle::Z, xds::Swizzle::One, xds::Swizzle::X, xds::Swizzle::Zero};
    info.expAdjust = -3;
    CHECK(sameFields(xds::decodeFetchConstant(xds::encodeFetchConstant(info)), info));
    CHECK_EQ(xds::encodeFetchConstant(info)[0] & 3u, 2u);
}

TEST_CASE("textures from bare data lay out and decode as their files do")
{
    synthetic::Spec spec;
    spec.format = 20;
    spec.width = 96;
    spec.height = 40;
    spec.maxLevel = 6;
    spec.endian = xds::Endian::Swap8In16;
    const auto block = [](std::uint32_t level, std::uint32_t bx, std::uint32_t by) {
        std::vector<std::uint8_t> data = interpolatedBlock(static_cast<std::uint8_t>(level * 30), 255, 0);
        const auto colour = colorBlock(static_cast<std::uint16_t>(bx * 2000), static_cast<std::uint16_t>(by * 3000), 0);
        data.insert(data.end(), colour.begin(), colour.end());
        return data;
    };
    const std::vector<std::uint8_t> file = synthetic::build(spec, block);
    const xds::Texture parsed = xds::Texture::parse(file);
    const std::span<const std::uint8_t> data = std::span<const std::uint8_t>(file).subspan(xds::kHeaderSize);
    const xds::Texture bare = xds::Texture::fromData(parsed.info(), data);

    CHECK_EQ(bare.levelCount(), parsed.levelCount());
    CHECK(bare.textureData().size() == data.size());
    CHECK(xds::describe(xds::decodeFetchConstant(bare.header().fetchConstant)) == xds::describe(parsed.info()));
    CHECK_EQ(bare.header().common, 3u);
    for (std::uint32_t level = 0; level < parsed.levelCount(); ++level) {
        CHECK_EQ(bare.levelLayout(level).surfaceOffset, parsed.levelLayout(level).surfaceOffset);
        CHECK_EQ(bare.levelLayout(level).offsetXBlocks, parsed.levelLayout(level).offsetXBlocks);
        CHECK_EQ(bare.levelLayout(level).offsetYBlocks, parsed.levelLayout(level).offsetYBlocks);
        CHECK(bare.extractLevel(level).data == parsed.extractLevel(level).data);
        CHECK(bare.decodeLevel(level).rgba == parsed.decodeLevel(level).rgba);
    }
    // The owning overload takes the data as it is.
    const xds::Texture moved =
        xds::Texture::fromData(parsed.info(), std::vector<std::uint8_t>(data.begin(), data.end()));
    CHECK(moved.decodeLevel(0).rgba == parsed.decodeLevel(0).rgba);
}

TEST_CASE("textures from bare data are checked like files")
{
    synthetic::Spec spec;
    const xds::Texture reference = xds::Texture::parse(synthetic::file(spec, 1 << 20));
    xds::TextureInfo info = reference.info();

    CHECK_THROWS(xds::Texture::fromData(info, std::vector<std::uint8_t>(100)));

    xds::TextureInfo empty = info;
    empty.width = 0;
    CHECK_THROWS(xds::Texture::fromData(empty, std::vector<std::uint8_t>(1 << 20)));

    xds::TextureInfo unsupported = info;
    unsupported.format = 26;
    CHECK_THROWS(xds::Texture::fromData(unsupported, std::vector<std::uint8_t>(1 << 20)));

    xds::TextureInfo cube = info;
    cube.dimension = xds::Dimension::Cube;
    CHECK_THROWS(xds::Texture::fromData(cube, std::vector<std::uint8_t>(1 << 20)));

    xds::TextureInfo narrow = info;
    narrow.pitch = 0;
    CHECK_THROWS(xds::Texture::fromData(narrow, std::vector<std::uint8_t>(1 << 20)));
}

TEST_CASE("layout of a wide DXT5 texture matches the game file")
{
    const std::array<std::uint32_t, 6> fetch{0x840000FE, 0x00000075, 0x000FE1FF, 0x00000D10, 0x00000240, 0x00010A00};
    const xds::Texture texture = fromFetch(fetch, 0x20000);
    const std::uint64_t offsets[] = {0, 0x10000, 0x18000, 0x1C000, 0x1C000, 0x1C000, 0x1C000, 0x1C000, 0x1C000,
        0x1C000};
    const std::uint32_t packedX[] = {0, 0, 0, 0, 0, 0, 8, 4, 2, 1};
    const std::uint32_t packedY[] = {0, 0, 0, 4, 2, 1, 0, 0, 0, 0};
    for (std::uint32_t level = 0; level < 10; ++level) {
        const xds::LevelLayout& layout = texture.levelLayout(level);
        CHECK_EQ(layout.surfaceOffset, offsets[level]);
        CHECK_EQ(layout.packed, level >= 3);
        CHECK_EQ(layout.offsetXBlocks, packedX[level]);
        CHECK_EQ(layout.offsetYBlocks, packedY[level]);
    }
    CHECK_EQ(texture.levelLayout(1).width, 256u);
    CHECK_EQ(texture.levelLayout(9).width, 1u);
    CHECK_THROWS(texture.levelLayout(10));
    // One byte short and the packed tail no longer fits.
    CHECK_THROWS(fromFetch(fetch, 0x1FFFF));
}

TEST_CASE("layout of a square 8_8_8_8 texture matches the game file")
{
    // tracks/wfp.xds
    const xds::Texture texture =
        fromFetch({0x84000002, 0x00000086, 0x003FE1FF, 0x00000C14, 0x00000240, 0x00100A00}, 0x156000);
    const std::uint64_t offsets[] = {0, 0x100000, 0x140000, 0x150000, 0x154000, 0x155000};
    for (std::uint32_t level = 0; level < 6; ++level) {
        CHECK_EQ(texture.levelLayout(level).surfaceOffset, offsets[level]);
    }
    const std::uint32_t packedX[] = {16, 8, 4, 0, 0};
    const std::uint32_t packedY[] = {0, 0, 0, 8, 4};
    for (std::uint32_t level = 5; level < 10; ++level) {
        CHECK_EQ(texture.levelLayout(level).surfaceOffset, 0x155000u);
        CHECK_EQ(texture.levelLayout(level).offsetXBlocks, packedX[level - 5]);
        CHECK_EQ(texture.levelLayout(level).offsetYBlocks, packedY[level - 5]);
    }
    CHECK(xds::swizzleString(texture.info().swizzle) == "ZYXW");
}

TEST_CASE("linear textures use a 256-byte row pitch below the top level")
{
    // cars/Shared.zip grille1_NRM_s_LOD0.xds
    const xds::Texture texture =
        fromFetch({0x010000FE, 0x000000B2, 0x000FE07F, 0x00000C14, 0x000001C0, 0x00010A00}, 0x18000);
    CHECK(!texture.info().tiled);
    CHECK_EQ(texture.levelLayout(0).surfacePitchBlocks, 128u);
    CHECK_EQ(texture.levelLayout(1).surfaceOffset, 0x10000u);
    CHECK_EQ(texture.levelLayout(2).surfaceOffset, 0x14000u);
    // 32 texels of 4 bytes pad to a 256-byte row: 64 blocks.
    CHECK_EQ(texture.levelLayout(2).surfacePitchBlocks, 64u);
    CHECK_EQ(texture.levelLayout(3).surfaceOffset, 0x16000u);
    CHECK(texture.levelLayout(3).packed);
}

TEST_CASE("a top level that is itself packed sits at its packed offset")
{
    // cars/VIP_Viper_13.zip DigitalGauge_VIP_VIPER_13_TEMPGAUGE_NEEDLE.XDS:
    // its data occupies block columns 4-6 of a 512-texel-tall surface.
    const xds::Texture texture =
        fromFetch({0x810000FE, 0x00000075, 0x0026A00B, 0x00000D10, 0x00000000, 0x00000A00}, 0x10000);
    const xds::LevelLayout& top = texture.levelLayout(0);
    CHECK(top.packed);
    CHECK_EQ(top.offsetXBlocks, 4u);
    CHECK_EQ(top.offsetYBlocks, 0u);
    CHECK_EQ(top.surfaceHeightBlocks, 128u);
}

TEST_CASE("8_8_8_8 texels decode with byte order and swizzle applied")
{
    synthetic::Spec spec;
    spec.format = 6;
    spec.width = 40;
    spec.height = 20;
    spec.maxLevel = 5;
    spec.endian = xds::Endian::Swap8In32;
    spec.swizzle = kArgb;
    // Little-endian texel bytes are X, Y, Z, W, which ZYXW turns into B, G, R, A.
    const auto texel = [](std::uint32_t level, std::uint32_t x, std::uint32_t y) {
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y),
            static_cast<std::uint8_t>(level), 200};
    };
    const xds::Texture texture = xds::Texture::parse(synthetic::build(spec, texel));
    for (std::uint32_t level = 0; level <= spec.maxLevel; ++level) {
        const xds::Image image = texture.decodeLevel(level);
        CHECK_EQ(image.width, std::max(1u, 40u >> level));
        CHECK_EQ(image.height, std::max(1u, 20u >> level));
        for (std::uint32_t y = 0; y < image.height; ++y) {
            for (std::uint32_t x = 0; x < image.width; ++x) {
                const Pixel expected{static_cast<std::uint8_t>(level), static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(x), 200};
                CHECK(pixel(image, x, y) == expected);
            }
        }
    }
    // The file stores each texel as a big-endian A8R8G8B8 word.
    CHECK_EQ(be32(texture.textureData().data()), 0xC8000000u);
}

TEST_CASE("linear textures decode like tiled ones")
{
    synthetic::Spec spec;
    spec.format = 6;
    spec.width = 64;
    spec.height = 64;
    spec.maxLevel = 6;
    spec.tiled = false;
    spec.endian = xds::Endian::Swap8In32;
    const auto texel = [](std::uint32_t level, std::uint32_t x, std::uint32_t y) {
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y),
            static_cast<std::uint8_t>(level), 255};
    };
    const xds::Texture texture = xds::Texture::parse(synthetic::build(spec, texel));
    for (std::uint32_t level = 0; level <= spec.maxLevel; ++level) {
        const xds::Image image = texture.decodeLevel(level);
        for (std::uint32_t y = 0; y < image.height; ++y) {
            for (std::uint32_t x = 0; x < image.width; ++x) {
                const Pixel expected{static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(level), 255};
                CHECK(pixel(image, x, y) == expected);
            }
        }
    }
}

TEST_CASE("DXT1 blocks decode four-color and three-color modes")
{
    synthetic::Spec spec;
    spec.format = 18;
    spec.width = 8;
    spec.height = 4;
    spec.endian = xds::Endian::Swap8In16;
    const auto block = [](std::uint32_t, std::uint32_t bx, std::uint32_t) {
        // Texel i uses index i % 4. Red > blue selects four colors; the
        // swapped endpoints select three colors and transparent black.
        const std::uint32_t indices = 0xE4E4E4E4;
        return bx == 0 ? colorBlock(0xF800, 0x001F, indices) : colorBlock(0x001F, 0xF800, indices);
    };
    const xds::Image image = xds::Texture::parse(synthetic::build(spec, block)).decodeLevel(0);
    CHECK(pixel(image, 0, 0) == Pixel({255, 0, 0, 255}));
    CHECK(pixel(image, 1, 0) == Pixel({0, 0, 255, 255}));
    CHECK(pixel(image, 2, 0) == Pixel({170, 0, 85, 255}));
    CHECK(pixel(image, 3, 0) == Pixel({85, 0, 170, 255}));
    CHECK(pixel(image, 4, 0) == Pixel({0, 0, 255, 255}));
    CHECK(pixel(image, 6, 0) == Pixel({128, 0, 128, 255}));
    CHECK(pixel(image, 7, 3) == Pixel({0, 0, 0, 0}));
}

TEST_CASE("DXT3 and DXT5 alpha decode")
{
    synthetic::Spec spec;
    spec.width = 4;
    spec.height = 4;
    spec.endian = xds::Endian::Swap8In16;

    spec.format = 19;
    const auto dxt3 = [](std::uint32_t, std::uint32_t, std::uint32_t) {
        std::vector<std::uint8_t> block{0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
        const auto color = colorBlock(0xFFFF, 0xFFFF, 0);
        block.insert(block.end(), color.begin(), color.end());
        return block;
    };
    const xds::Image explicitAlpha = xds::Texture::parse(synthetic::build(spec, dxt3)).decodeLevel(0);
    for (std::uint32_t i = 0; i < 16; ++i) {
        CHECK_EQ(pixel(explicitAlpha, i % 4, i / 4)[3], static_cast<std::uint8_t>(i * 17));
    }

    spec.format = 53;
    const auto dxt5 = [](std::uint32_t, std::uint32_t, std::uint32_t) {
        // Index 2 is the first interpolated value: (6 * 240 + 30) / 7.
        std::vector<std::uint8_t> block = interpolatedBlock(240, 30, 2);
        const auto color = colorBlock(0x07E0, 0x07E0, 0);
        block.insert(block.end(), color.begin(), color.end());
        return block;
    };
    CHECK(topLeftPixel(spec, dxt5) == Pixel({0, 255, 0, 210}));
}

TEST_CASE("single-channel formats replicate their value before the swizzle")
{
    synthetic::Spec spec;
    spec.width = 4;
    spec.height = 4;
    spec.endian = xds::Endian::Swap8In16;
    spec.swizzle = kAlphaOnly;

    spec.format = 59;
    const auto dxt5a = [](std::uint32_t, std::uint32_t, std::uint32_t) { return interpolatedBlock(90, 10, 0); };
    CHECK(topLeftPixel(spec, dxt5a) == Pixel({255, 255, 255, 90}));

    spec.format = 58;
    const auto dxt3a = [](std::uint32_t, std::uint32_t, std::uint32_t) {
        return std::vector<std::uint8_t>(8, 0x33);
    };
    CHECK(topLeftPixel(spec, dxt3a) == Pixel({255, 255, 255, 51}));

    spec.swizzle = {Swizzle::X, Swizzle::Y, Swizzle::Z, Swizzle::W};
    CHECK(topLeftPixel(spec, dxt3a) == Pixel({51, 51, 51, 51}));
}

TEST_CASE("DXN decodes two channels with zero blue and opaque alpha")
{
    synthetic::Spec spec;
    spec.format = 49;
    spec.width = 4;
    spec.height = 4;
    spec.endian = xds::Endian::Swap8In16;
    const auto block = [](std::uint32_t, std::uint32_t, std::uint32_t) {
        std::vector<std::uint8_t> data = interpolatedBlock(200, 0, 0);
        const auto y = interpolatedBlock(0, 100, 1);
        data.insert(data.end(), y.begin(), y.end());
        return data;
    };
    CHECK(topLeftPixel(spec, block) == Pixel({200, 100, 0, 255}));
}

TEST_CASE("DDS output keeps compressed blocks and every level")
{
    synthetic::Spec spec;
    spec.format = 20;
    spec.width = 64;
    spec.height = 32;
    spec.maxLevel = 6;
    spec.endian = xds::Endian::Swap8In16;
    const auto block = [](std::uint32_t level, std::uint32_t bx, std::uint32_t by) {
        std::vector<std::uint8_t> data(16);
        data[0] = static_cast<std::uint8_t>(level);
        data[1] = static_cast<std::uint8_t>(bx);
        data[2] = static_cast<std::uint8_t>(by);
        return data;
    };
    const xds::Texture texture = xds::Texture::parse(synthetic::build(spec, block));
    CHECK(xds::preferredDdsEncoding(texture.info()) == xds::DdsEncoding::Compressed);
    const std::vector<std::uint8_t> dds = xds::writeDds(texture);
    CHECK(std::memcmp(dds.data(), "DDS ", 4) == 0);
    CHECK_EQ(le32(dds.data() + 4), 124u);
    CHECK_EQ(le32(dds.data() + 12), 32u);
    CHECK_EQ(le32(dds.data() + 16), 64u);
    CHECK_EQ(le32(dds.data() + 20), 16u * 8 * 16);
    CHECK_EQ(le32(dds.data() + 28), 7u);
    CHECK(std::memcmp(dds.data() + 84, "DXT5", 4) == 0);

    std::size_t expectedSize = 128;
    std::array<std::size_t, 7> levelStart{};
    for (std::uint32_t level = 0; level < 7; ++level) {
        levelStart[level] = expectedSize;
        const xds::LevelLayout& layout = texture.levelLayout(level);
        expectedSize += std::size_t{layout.blocksWide} * layout.blocksHigh * 16;
    }
    CHECK_EQ(dds.size(), expectedSize);
    // Level 2 is 16x8 texels, 4x2 blocks, so block (3, 1) is the eighth.
    const std::uint8_t* block31 = dds.data() + levelStart[2] + 7 * 16;
    CHECK_EQ(block31[0], 2);
    CHECK_EQ(block31[1], 3);
    CHECK_EQ(block31[2], 1);
}

TEST_CASE("DXN goes to DDS as BC5 under a DX10 header")
{
    synthetic::Spec spec;
    spec.format = 49;
    spec.width = 8;
    spec.height = 8;
    spec.endian = xds::Endian::Swap8In16;
    const auto block = [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::vector<std::uint8_t>(16, 1); };
    const std::vector<std::uint8_t> dds = xds::writeDds(xds::Texture::parse(synthetic::build(spec, block)));
    CHECK(std::memcmp(dds.data() + 84, "DX10", 4) == 0);
    CHECK_EQ(le32(dds.data() + 128), 83u);
    CHECK_EQ(le32(dds.data() + 132), 3u);
    CHECK_EQ(le32(dds.data() + 140), 1u);
    CHECK_EQ(dds.size(), std::size_t{128 + 20 + 4 * 16});
}

TEST_CASE("DDS output falls back to BGRA8 for swizzled textures")
{
    synthetic::Spec spec;
    spec.format = 6;
    spec.width = 4;
    spec.height = 2;
    spec.swizzle = kArgb;
    const auto texel = [](std::uint32_t, std::uint32_t, std::uint32_t) {
        return std::vector<std::uint8_t>{10, 20, 30, 40};
    };
    const xds::Texture texture = xds::Texture::parse(synthetic::build(spec, texel));
    CHECK(xds::preferredDdsEncoding(texture.info()) == xds::DdsEncoding::Bgra8);
    CHECK_THROWS(xds::writeDds(texture, xds::DdsEncoding::Compressed));
    const std::vector<std::uint8_t> dds = xds::writeDds(texture);
    CHECK_EQ(dds.size(), std::size_t{128 + 4 * 2 * 4});
    CHECK_EQ(le32(dds.data() + 88), 32u);
    CHECK_EQ(le32(dds.data() + 92), 0x00FF0000u);
    // RGBA (30, 20, 10, 40) is stored as B, G, R, A.
    CHECK_EQ(dds[128], 10);
    CHECK_EQ(dds[129], 20);
    CHECK_EQ(dds[130], 30);
    CHECK_EQ(dds[131], 40);
}

TEST_CASE("PNG output round-trips through zlib")
{
    xds::Image image;
    image.width = 3;
    image.height = 2;
    for (std::uint8_t i = 0; i < 24; ++i) {
        image.rgba.push_back(i);
    }
    const std::vector<std::uint8_t> png = xds::writePng(image);
    const std::uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    CHECK(std::memcmp(png.data(), signature, 8) == 0);
    CHECK(std::memcmp(png.data() + 12, "IHDR", 4) == 0);
    CHECK_EQ(be32(png.data() + 16), 3u);
    CHECK_EQ(be32(png.data() + 20), 2u);

    const std::size_t idat = 8 + 25;
    CHECK(std::memcmp(png.data() + idat + 4, "IDAT", 4) == 0);
    const std::uint32_t length = be32(png.data() + idat);
    const auto crc = static_cast<std::uint32_t>(crc32(0, png.data() + idat + 4, length + 4));
    CHECK_EQ(be32(png.data() + idat + 8 + length), crc);

    std::vector<std::uint8_t> raw(2 * (1 + 12));
    uLongf rawSize = static_cast<uLongf>(raw.size());
    CHECK_EQ(uncompress(raw.data(), &rawSize, png.data() + idat + 8, length), Z_OK);
    CHECK_EQ(rawSize, static_cast<uLongf>(raw.size()));
    CHECK_EQ(raw[0], 0);
    CHECK_EQ(raw[1], 0);
    CHECK_EQ(raw[13], 0);
    CHECK_EQ(raw[14], 12);
    CHECK_EQ(raw[25], 23);

    CHECK_THROWS(xds::writePng(xds::Image{}));
}

TEST_CASE("malformed and unsupported files are rejected")
{
    const synthetic::Spec spec;
    CHECK_THROWS(xds::Texture::parse(std::vector<std::uint8_t>(20)));
    CHECK_THROWS(xds::Texture::parse(synthetic::file(spec, 100)));

    synthetic::Spec cube = spec;
    cube.dimension = 3;
    CHECK_THROWS(xds::Texture::parse(synthetic::file(cube, 1 << 20)));

    synthetic::Spec unsupported = spec;
    unsupported.format = 26;
    CHECK(!xds::isFormatSupported(26));
    CHECK_THROWS(xds::Texture::parse(synthetic::file(unsupported, 1 << 20)));

    synthetic::Spec narrow = spec;
    narrow.width = 100;
    narrow.pitch = 64;
    CHECK_THROWS(xds::Texture::parse(synthetic::file(narrow, 1 << 20)));

    synthetic::Spec tooManyLevels = spec;
    tooManyLevels.maxLevel = 9;
    CHECK_THROWS(xds::Texture::parse(synthetic::file(tooManyLevels, 1 << 20)));

    CHECK(xds::looksLikeXds(synthetic::file(spec, 0)));
    std::vector<std::uint8_t> notTexture = synthetic::file(spec, 0);
    notTexture[3] = 1;
    CHECK(!xds::looksLikeXds(notTexture));
    CHECK(!xds::looksLikeXds(std::vector<std::uint8_t>(51, 0)));
    CHECK_THROWS(xds::Texture::load("/nonexistent/texture.xds"));
}

int main()
{
    return check::runAll();
}
