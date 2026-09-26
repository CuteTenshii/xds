#include "internal.h"

#include <xds/xds.h>

#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace xds {

namespace {

constexpr std::uint32_t kDdsHeaderSize = 124;
constexpr std::uint32_t kPixelFormatSize = 32;

constexpr std::uint32_t kFlagCaps = 0x1;
constexpr std::uint32_t kFlagHeight = 0x2;
constexpr std::uint32_t kFlagWidth = 0x4;
constexpr std::uint32_t kFlagPitch = 0x8;
constexpr std::uint32_t kFlagPixelFormat = 0x1000;
constexpr std::uint32_t kFlagMipMapCount = 0x20000;
constexpr std::uint32_t kFlagLinearSize = 0x80000;

constexpr std::uint32_t kPixelAlpha = 0x1;
constexpr std::uint32_t kPixelFourCC = 0x4;
constexpr std::uint32_t kPixelRgb = 0x40;

constexpr std::uint32_t kDx10HeaderSize = 20;
constexpr std::uint32_t kDxgiFormatBc5Unorm = 83;
constexpr std::uint32_t kD3d10ResourceTexture2D = 3;

constexpr std::uint32_t kCapsComplex = 0x8;
constexpr std::uint32_t kCapsTexture = 0x1000;
constexpr std::uint32_t kCapsMipMap = 0x400000;

constexpr std::uint32_t fourCC(const char (&code)[5])
{
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[0]))
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[1])) << 8)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[2])) << 16)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[3])) << 24);
}

/// The FourCC that stores `format`'s blocks unchanged, or 0 if DDS has none.
std::uint32_t compressedFourCC(std::uint32_t format)
{
    switch (static_cast<Format>(format)) {
    case Format::kDxt1:
    case Format::kDxt1_As16_16_16_16:
        return fourCC("DXT1");
    case Format::kDxt2_3:
    case Format::kDxt2_3_As16_16_16_16:
        return fourCC("DXT3");
    case Format::kDxt4_5:
    case Format::kDxt4_5_As16_16_16_16:
        return fourCC("DXT5");
    case Format::kDxt5A:
        return fourCC("ATI1");
    case Format::kDxn:
        // "ATI2" is read with either block as red depending on the reader,
        // so DXN goes out as DX10 BC5, where the first block is red.
        return fourCC("DX10");
    case Format::k8_8_8_8:
    case Format::k8_8_8_8_As16_16_16_16:
    case Format::kDxt3A:
        return 0;
    }
    return 0;
}

bool isIdentitySwizzle(const std::array<Swizzle, 4>& swizzle)
{
    return swizzle[0] == Swizzle::X && swizzle[1] == Swizzle::Y && swizzle[2] == Swizzle::Z
        && swizzle[3] == Swizzle::W;
}

void append(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> bytes)
{
    out.insert(out.end(), bytes.begin(), bytes.end());
}

} // namespace

DdsEncoding preferredDdsEncoding(const TextureInfo& info)
{
    if (!isFormatSupported(info.format) || compressedFourCC(info.format) == 0 || !isIdentitySwizzle(info.swizzle)) {
        return DdsEncoding::Bgra8;
    }
    // DXT5A has one component that the GPU replicates to all four, while a
    // BC4 reader fills only red, so the file would not look like the game.
    if (static_cast<Format>(info.format) == Format::kDxt5A) {
        return DdsEncoding::Bgra8;
    }
    return DdsEncoding::Compressed;
}

std::vector<std::uint8_t> writeDds(const Texture& texture)
{
    return writeDds(texture, preferredDdsEncoding(texture.info()));
}

std::vector<std::uint8_t> writeDds(const Texture& texture, DdsEncoding encoding)
{
    const TextureInfo& info = texture.info();
    const std::uint32_t levels = texture.levelCount();
    const std::uint32_t code = compressedFourCC(info.format);
    if (encoding == DdsEncoding::Compressed && code == 0) {
        throw Error(std::string("DDS has no compressed format for ") + std::string(formatName(info.format)));
    }

    std::vector<std::uint8_t> out(4 + kDdsHeaderSize, 0);
    std::memcpy(out.data(), "DDS ", 4);
    std::uint8_t* h = out.data() + 4;
    std::uint32_t flags = kFlagCaps | kFlagHeight | kFlagWidth | kFlagPixelFormat;
    if (levels > 1) {
        flags |= kFlagMipMapCount;
    }
    std::uint32_t caps = kCapsTexture;
    if (levels > 1) {
        caps |= kCapsComplex | kCapsMipMap;
    }

    std::uint8_t* pf = h + 72;
    detail::putLe32(pf, kPixelFormatSize);
    if (encoding == DdsEncoding::Compressed) {
        const LevelLayout& top = texture.levelLayout(0);
        flags |= kFlagLinearSize;
        detail::putLe32(h + 16, top.blocksWide * top.blocksHigh * bytesPerBlock(info.format));
        detail::putLe32(pf + 4, kPixelFourCC);
        detail::putLe32(pf + 8, code);
    } else {
        flags |= kFlagPitch;
        detail::putLe32(h + 16, info.width * 4);
        detail::putLe32(pf + 4, kPixelRgb | kPixelAlpha);
        detail::putLe32(pf + 12, 32);
        detail::putLe32(pf + 16, 0x00FF0000);
        detail::putLe32(pf + 20, 0x0000FF00);
        detail::putLe32(pf + 24, 0x000000FF);
        detail::putLe32(pf + 28, 0xFF000000);
    }

    detail::putLe32(h, kDdsHeaderSize);
    detail::putLe32(h + 4, flags);
    detail::putLe32(h + 8, info.height);
    detail::putLe32(h + 12, info.width);
    detail::putLe32(h + 24, levels > 1 ? levels : 0);
    detail::putLe32(h + 104, caps);

    if (encoding == DdsEncoding::Compressed && code == fourCC("DX10")) {
        std::array<std::uint8_t, kDx10HeaderSize> dx10{};
        detail::putLe32(dx10.data(), kDxgiFormatBc5Unorm);
        detail::putLe32(dx10.data() + 4, kD3d10ResourceTexture2D);
        detail::putLe32(dx10.data() + 12, 1);
        append(out, dx10);
    }

    for (std::uint32_t level = 0; level < levels; ++level) {
        if (encoding == DdsEncoding::Compressed) {
            append(out, texture.extractLevel(level).data);
            continue;
        }
        Image image = texture.decodeLevel(level);
        for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            std::swap(image.rgba[i], image.rgba[i + 2]);
        }
        append(out, image.rgba);
    }
    return out;
}

} // namespace xds
