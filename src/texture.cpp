#include "internal.h"

#include <xds/xds.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

namespace xds {

namespace {

/// Tiled surfaces are allocated in whole 32x32-block tiles.
constexpr std::uint32_t kTileBlocks = 32;
/// Every surface (a level, or the packed mip tail) starts on a 4 KiB page.
constexpr std::uint64_t kSurfaceAlignment = 4096;
constexpr std::uint32_t kLinearRowAlignment = 256;
/// Levels whose shorter side is at most 2^4 texels go into the packed tail.
constexpr std::uint32_t kPackedTailLog2 = 4;
constexpr std::uint32_t kNoPackedTail = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kMaxDimension = 8192;

std::uint32_t log2Exact(std::uint32_t value)
{
    return static_cast<std::uint32_t>(std::countr_zero(value));
}

/// Position of `level` inside the packed mip tail, in texels, following the
/// hardware's arrangement: the first three packed levels step along one axis
/// and the rest along the other, depending on which side is longer.
std::pair<std::uint32_t, std::uint32_t> packedMipOffset(std::uint32_t width, std::uint32_t height,
    std::uint32_t level)
{
    const std::uint32_t log2Width = detail::log2Ceil(width);
    const std::uint32_t log2Height = detail::log2Ceil(height);
    const std::uint32_t log2Size = std::min(log2Width, log2Height);
    const std::uint32_t tailBase = log2Size > kPackedTailLog2 ? log2Size - kPackedTailLog2 : 0;
    const std::uint32_t packedIndex = level - tailBase;
    const bool wide = log2Width > log2Height;
    if (packedIndex < 3) {
        const std::uint32_t step = 16u >> packedIndex;
        return wide ? std::pair{0u, step} : std::pair{step, 0u};
    }
    if (wide) {
        return {(1u << (log2Width - tailBase)) >> (packedIndex - 2), 0u};
    }
    return {0u, (1u << (log2Height - tailBase)) >> (packedIndex - 2)};
}

struct SurfaceGeometry {
    std::uint32_t pitchBlocks = 0;
    std::uint32_t heightBlocks = 0;
    std::uint64_t size = 0;
};

/// Size of a surface allocated for `storageWidthBlocks` x
/// `storageHeightBlocks`. A linear surface with `exactPitch` uses that width
/// as its row pitch as-is, since it came from the fetch constant.
SurfaceGeometry surfaceGeometry(bool tiled, std::uint32_t storageWidthBlocks, std::uint32_t storageHeightBlocks,
    std::uint32_t bytesPerBlock, bool exactPitch)
{
    SurfaceGeometry geometry;
    geometry.heightBlocks = static_cast<std::uint32_t>(detail::alignUp(storageHeightBlocks, kTileBlocks));
    if (!tiled && exactPitch) {
        geometry.pitchBlocks = storageWidthBlocks;
    } else if (tiled) {
        geometry.pitchBlocks = static_cast<std::uint32_t>(detail::alignUp(storageWidthBlocks, kTileBlocks));
    } else {
        const std::uint64_t rowBytes =
            detail::alignUp(static_cast<std::uint64_t>(storageWidthBlocks) * bytesPerBlock, kLinearRowAlignment);
        geometry.pitchBlocks = static_cast<std::uint32_t>(rowBytes / bytesPerBlock);
    }
    geometry.size = detail::alignUp(
        static_cast<std::uint64_t>(geometry.pitchBlocks) * geometry.heightBlocks * bytesPerBlock, kSurfaceAlignment);
    return geometry;
}

std::uint32_t blocksFor(std::uint32_t texels, std::uint32_t edge)
{
    return (texels + edge - 1) / edge;
}

/// Bytes from the start of the texture data up to the end of the last block
/// the level occupies. Data may end there: the game's own containers store
/// small tiled textures only up to their last block, not to the end of the
/// tile.
std::uint64_t levelExtent(const LevelLayout& layout, bool tiled, std::uint32_t bytesPerBlock)
{
    const std::uint32_t firstRow = layout.offsetYBlocks;
    const std::uint32_t lastRow = layout.offsetYBlocks + layout.blocksHigh - 1;
    if (tiled) {
        // Each band of 32 block rows is stored after the ones above it, so
        // the block stored last is in the level's last band.
        const std::uint32_t log2Bytes = static_cast<std::uint32_t>(std::countr_zero(bytesPerBlock));
        const std::uint32_t bandStart = std::max(firstRow, lastRow / kTileBlocks * kTileBlocks);
        std::uint64_t end = 0;
        for (std::uint32_t y = bandStart; y <= lastRow; ++y) {
            for (std::uint32_t x = layout.offsetXBlocks; x < layout.offsetXBlocks + layout.blocksWide; ++x) {
                end = std::max<std::uint64_t>(end,
                    static_cast<std::uint64_t>(tiledOffset2D(x, y, layout.surfacePitchBlocks, log2Bytes)) + bytesPerBlock);
            }
        }
        return layout.surfaceOffset + end;
    }
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(layout.surfacePitchBlocks) * bytesPerBlock;
    return layout.surfaceOffset + static_cast<std::uint64_t>(lastRow) * rowBytes
        + (static_cast<std::uint64_t>(layout.offsetXBlocks) + layout.blocksWide) * bytesPerBlock;
}

} // namespace

std::uint32_t tiledOffset2D(std::uint32_t x, std::uint32_t y, std::uint32_t pitchBlocks,
    std::uint32_t log2BytesPerBlock)
{
    const std::uint32_t macroOuter = ((y >> 5) * (pitchBlocks >> 5)) << (log2BytesPerBlock + 7);
    const std::uint32_t microOuter = ((y & 6) << 2) << log2BytesPerBlock;
    const std::uint32_t outer = macroOuter + ((microOuter & ~0xFu) << 1) + (microOuter & 0xFu)
        + ((y & 8) << (3 + log2BytesPerBlock)) + ((y & 1) << 4);
    const std::uint32_t macroInner = (x >> 5) << (log2BytesPerBlock + 7);
    const std::uint32_t microInner = (x & 7) << log2BytesPerBlock;
    const std::uint32_t offset = outer + macroInner + ((microInner & ~0xFu) << 1) + (microInner & 0xFu);
    return ((offset & ~0x1FFu) << 3) + ((offset & 0x1C0u) << 2) + (offset & 0x3Fu) + ((y & 16) << 7)
        + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

Texture Texture::parse(std::span<const std::uint8_t> file)
{
    return parse(std::vector<std::uint8_t>(file.begin(), file.end()));
}

Texture Texture::parse(std::vector<std::uint8_t>&& file)
{
    Texture texture;
    texture.m_bytes = std::move(file);
    texture.m_dataOffset = kHeaderSize;
    texture.readFileHeader();
    texture.layOut();
    return texture;
}

Texture Texture::fromData(const TextureInfo& info, std::span<const std::uint8_t> data)
{
    return fromData(info, std::vector<std::uint8_t>(data.begin(), data.end()));
}

Texture Texture::fromData(const TextureInfo& info, std::vector<std::uint8_t>&& data)
{
    Texture texture;
    texture.m_bytes = std::move(data);
    texture.m_dataOffset = 0;
    texture.m_header.common = detail::kCommonTypeTexture;
    texture.m_header.fetchConstant = encodeFetchConstant(info);
    texture.m_info = info;
    texture.layOut();
    return texture;
}

Texture Texture::load(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw Error("cannot open " + path);
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        throw Error("cannot read " + path);
    }
    return parse(std::move(data));
}

std::span<const std::uint8_t> Texture::textureData() const
{
    return std::span<const std::uint8_t>(m_bytes).subspan(m_dataOffset);
}

const LevelLayout& Texture::levelLayout(std::uint32_t level) const
{
    if (level >= m_levels.size()) {
        throw Error("level " + std::to_string(level) + " does not exist; the texture has "
            + std::to_string(m_levels.size()));
    }
    return m_levels[level];
}

void Texture::readFileHeader()
{
    m_header = readHeader(m_bytes);
    if ((m_header.fetchConstant[0] & 3) != detail::kFetchTypeTexture) {
        throw Error("header does not hold a texture fetch constant");
    }
    m_info = decodeFetchConstant(m_header.fetchConstant);
}

void Texture::layOut()
{
    const TextureInfo& info = m_info;
    if (info.width == 0 || info.height == 0) {
        throw Error("texture size " + std::to_string(info.width) + "x" + std::to_string(info.height) + " is empty");
    }
    if (info.dimension != Dimension::TwoD || info.depth != 1) {
        static constexpr std::string_view kNames[] = {"1D", "2D", "3D", "cube"};
        const std::string kind = info.dimension == Dimension::TwoD
            ? "stacked 2D"
            : std::string(kNames[static_cast<std::size_t>(info.dimension)]);
        throw Error(kind + " textures are not supported, only single 2D textures");
    }
    if (!isFormatSupported(info.format)) {
        throw Error("unsupported texture format " + std::to_string(info.format) + " ("
            + std::string(formatName(info.format)) + ")");
    }
    if (info.minMipLevel != 0) {
        throw Error("textures whose smallest stored level is not 0 are not supported (min level "
            + std::to_string(info.minMipLevel) + ")");
    }
    if (info.width > kMaxDimension || info.height > kMaxDimension) {
        throw Error("texture size " + std::to_string(info.width) + "x" + std::to_string(info.height)
            + " is larger than the GPU allows");
    }
    if (info.maxMipLevel > detail::log2Ceil(std::max(info.width, info.height))) {
        throw Error("texture declares " + std::to_string(info.levelCount()) + " levels, more than a "
            + std::to_string(info.width) + "x" + std::to_string(info.height) + " texture can have");
    }

    const std::uint32_t edge = blockEdge(info.format);
    const std::uint32_t blockBytes = bytesPerBlock(info.format);
    const std::uint32_t pitchBlocks0 = info.pitch / edge;
    const std::uint32_t width0Blocks = blocksFor(info.width, edge);
    if (pitchBlocks0 < width0Blocks) {
        throw Error("row pitch of " + std::to_string(info.pitch) + " texels is narrower than the "
            + std::to_string(info.width) + "-texel texture");
    }

    const std::uint32_t minLog2 = std::min(detail::log2Ceil(info.width), detail::log2Ceil(info.height));
    const std::uint32_t packedStart = !info.packedMips ? kNoPackedTail
        : minLog2 > kPackedTailLog2                    ? minLog2 - kPackedTailLog2
                                                       : 0;
    const std::uint32_t storageWidth = detail::nextPow2(info.width);
    const std::uint32_t storageHeight = detail::nextPow2(info.height);

    // Separate mip surfaces must not overlap the top level. When the top
    // level is itself packed, every level shares its surface and the mip
    // address goes unused.
    if (info.levelCount() > 1 && packedStart != 0) {
        const std::uint32_t topRows = blocksFor(info.height, edge);
        const std::uint64_t topEnd = info.baseAddress
            + surfaceGeometry(info.tiled, pitchBlocks0, topRows, blockBytes, true).size;
        if (info.mipAddress >= info.baseAddress && info.mipAddress < topEnd) {
            throw Error("mip levels at offset " + std::to_string(info.mipAddress) + " overlap the top level");
        }
    }

    std::uint64_t mipCursor = info.mipAddress;
    LevelLayout tail;
    m_levels.clear();
    for (std::uint32_t level = 0; level < info.levelCount(); ++level) {
        LevelLayout layout;
        layout.level = level;
        layout.width = std::max(1u, info.width >> level);
        layout.height = std::max(1u, info.height >> level);
        layout.blocksWide = blocksFor(layout.width, edge);
        layout.blocksHigh = blocksFor(layout.height, edge);
        layout.packed = level >= packedStart;

        if (level <= packedStart) {
            // The level has a surface of its own, or starts the packed tail.
            SurfaceGeometry geometry;
            if (level == 0) {
                // Packed or not, the top level's pitch comes from the fetch
                // constant; only a packed top level rounds its height up to
                // a power of two like the smaller levels do.
                const std::uint32_t rows = layout.packed ? storageHeight : info.height;
                geometry = surfaceGeometry(info.tiled, pitchBlocks0, blocksFor(rows, edge), blockBytes, true);
                layout.surfaceOffset = info.baseAddress;
            } else {
                // Levels below the top are stored at power-of-two sizes.
                const std::uint32_t levelWidth = std::max(1u, storageWidth >> level);
                const std::uint32_t levelHeight = std::max(1u, storageHeight >> level);
                geometry = surfaceGeometry(
                    info.tiled, blocksFor(levelWidth, edge), blocksFor(levelHeight, edge), blockBytes, false);
                layout.surfaceOffset = mipCursor;
                mipCursor += geometry.size;
            }
            layout.surfacePitchBlocks = geometry.pitchBlocks;
            layout.surfaceHeightBlocks = geometry.heightBlocks;
            layout.surfaceSize = geometry.size;
            if (level == packedStart) {
                tail = layout;
            }
        } else {
            layout.surfaceOffset = tail.surfaceOffset;
            layout.surfacePitchBlocks = tail.surfacePitchBlocks;
            layout.surfaceHeightBlocks = tail.surfaceHeightBlocks;
            layout.surfaceSize = tail.surfaceSize;
        }

        if (layout.packed) {
            const auto [x, y] = packedMipOffset(info.width, info.height, level);
            layout.offsetXBlocks = x / edge;
            layout.offsetYBlocks = y / edge;
        }

        const std::uint64_t extent = levelExtent(layout, info.tiled, blockBytes);
        const std::size_t available = textureData().size();
        if (extent > available) {
            throw Error("texture data is " + std::to_string(available) + " bytes but level " + std::to_string(level)
                + " needs " + std::to_string(extent));
        }
        m_levels.push_back(layout);
    }
}

Surface Texture::extractLevel(std::uint32_t level) const
{
    const LevelLayout& layout = levelLayout(level);
    const std::uint32_t blockBytes = bytesPerBlock(m_info.format);
    const std::uint32_t log2Bytes = log2Exact(blockBytes);
    const std::span<const std::uint8_t> data = textureData();

    Surface surface;
    surface.width = layout.width;
    surface.height = layout.height;
    surface.blocksWide = layout.blocksWide;
    surface.blocksHigh = layout.blocksHigh;
    surface.bytesPerBlock = blockBytes;
    surface.data.resize(static_cast<std::size_t>(layout.blocksWide) * layout.blocksHigh * blockBytes);

    std::uint8_t* out = surface.data.data();
    for (std::uint32_t by = 0; by < layout.blocksHigh; ++by) {
        const std::uint32_t y = by + layout.offsetYBlocks;
        for (std::uint32_t bx = 0; bx < layout.blocksWide; ++bx) {
            const std::uint32_t x = bx + layout.offsetXBlocks;
            const std::uint64_t within = m_info.tiled
                ? tiledOffset2D(x, y, layout.surfacePitchBlocks, log2Bytes)
                : (static_cast<std::uint64_t>(y) * layout.surfacePitchBlocks + x) * blockBytes;
            const std::uint64_t offset = layout.surfaceOffset + within;
            if (offset + blockBytes > data.size()) {
                throw Error("level " + std::to_string(level) + " reads past the end of the texture data");
            }
            std::memcpy(out, data.data() + offset, blockBytes);
            out += blockBytes;
        }
    }
    detail::swapEndian(surface.data, m_info.endian);
    return surface;
}

Image Texture::decodeLevel(std::uint32_t level) const
{
    const Surface surface = extractLevel(level);
    const std::uint32_t edge = blockEdge(m_info.format);

    Image image;
    image.width = surface.width;
    image.height = surface.height;
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4);

    detail::Texels texels{};
    const std::uint8_t* block = surface.data.data();
    for (std::uint32_t by = 0; by < surface.blocksHigh; ++by) {
        for (std::uint32_t bx = 0; bx < surface.blocksWide; ++bx) {
            detail::decodeBlock(m_info.format, block, texels);
            block += surface.bytesPerBlock;
            for (std::uint32_t ty = 0; ty < edge; ++ty) {
                const std::uint32_t py = by * edge + ty;
                if (py >= image.height) {
                    break;
                }
                for (std::uint32_t tx = 0; tx < edge; ++tx) {
                    const std::uint32_t px = bx * edge + tx;
                    if (px >= image.width) {
                        break;
                    }
                    const auto& components = texels[ty * 4 + tx];
                    std::uint8_t* pixel = image.rgba.data() + (static_cast<std::size_t>(py) * image.width + px) * 4;
                    for (std::size_t channel = 0; channel < 4; ++channel) {
                        const Swizzle source = m_info.swizzle[channel];
                        pixel[channel] = source == Swizzle::Zero ? 0
                            : source == Swizzle::One             ? 255
                                                                 : components[static_cast<std::size_t>(source)];
                    }
                }
            }
        }
    }
    return image;
}

} // namespace xds
