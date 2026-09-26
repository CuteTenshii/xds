#pragma once

#include <xds/xds.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

/// Builds .xds files in memory for tests.
namespace synthetic {

struct Spec {
    std::uint32_t format = 6;
    std::uint32_t width = 32;
    std::uint32_t height = 32;
    std::uint32_t maxLevel = 0;
    xds::Endian endian = xds::Endian::Swap8In32;
    bool tiled = true;
    bool packedMips = true;
    /// Row pitch in texels; 0 picks the width rounded up to a whole tile.
    std::uint32_t pitch = 0;
    std::uint32_t mipAddress = 0;
    std::array<xds::Swizzle, 4> swizzle{xds::Swizzle::X, xds::Swizzle::Y, xds::Swizzle::Z, xds::Swizzle::W};
    std::uint32_t dimension = 1;
};

inline void putBe32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

inline std::array<std::uint32_t, 6> fetchConstant(const Spec& spec)
{
    const std::uint32_t edge = xds::isFormatSupported(spec.format) ? xds::blockEdge(spec.format) : 1;
    const std::uint32_t tileTexels = 32 * edge;
    const std::uint32_t pitch =
        spec.pitch != 0 ? spec.pitch : (spec.width + tileTexels - 1) / tileTexels * tileTexels;
    std::uint32_t swizzle = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        swizzle |= static_cast<std::uint32_t>(spec.swizzle[i]) << (3 * i);
    }
    return {
        2u | ((pitch >> 5) << 22) | (spec.tiled ? 1u << 31 : 0u),
        spec.format | (static_cast<std::uint32_t>(spec.endian) << 6),
        (spec.width - 1) | ((spec.height - 1) << 13),
        swizzle << 1,
        spec.maxLevel << 6,
        (spec.dimension << 9) | (spec.packedMips ? 1u << 11 : 0u) | spec.mipAddress,
    };
}

/// A file whose texture data is `dataSize` zero bytes.
inline std::vector<std::uint8_t> file(const Spec& spec, std::size_t dataSize)
{
    std::vector<std::uint8_t> bytes(xds::kHeaderSize + dataSize, 0);
    putBe32(bytes.data(), 3);
    putBe32(bytes.data() + 4, 1);
    putBe32(bytes.data() + 20, 0xFFFF0000u);
    putBe32(bytes.data() + 24, 0xFFFF0000u);
    const auto fetch = fetchConstant(spec);
    for (std::size_t i = 0; i < fetch.size(); ++i) {
        putBe32(bytes.data() + 28 + 4 * i, fetch[i]);
    }
    return bytes;
}

/// Converts little-endian block bytes to the file's byte order. Every
/// supported swap is its own inverse.
inline void toFileOrder(std::uint8_t* p, std::size_t size, xds::Endian endian)
{
    switch (endian) {
    case xds::Endian::None:
        return;
    case xds::Endian::Swap8In16:
        for (std::size_t i = 0; i + 1 < size; i += 2) {
            std::swap(p[i], p[i + 1]);
        }
        return;
    case xds::Endian::Swap8In32:
        for (std::size_t i = 0; i + 3 < size; i += 4) {
            std::swap(p[i], p[i + 3]);
            std::swap(p[i + 1], p[i + 2]);
        }
        return;
    case xds::Endian::Swap16In32:
        for (std::size_t i = 0; i + 3 < size; i += 4) {
            std::swap(p[i], p[i + 2]);
            std::swap(p[i + 1], p[i + 3]);
        }
        return;
    }
}

/// A complete file for `spec` in which block (bx, by) of each level holds
/// the little-endian bytes `block(level, bx, by)` returns. The layout comes
/// from the library, so this checks extraction and decoding, not layout.
inline std::vector<std::uint8_t> build(const Spec& spec,
    const std::function<std::vector<std::uint8_t>(std::uint32_t, std::uint32_t, std::uint32_t)>& block)
{
    // Parse once with ample zeroed data to learn the layout, then size the
    // file to exactly what the levels occupy. Mip levels go straight after
    // the top level's surface, as in the game's files.
    Spec placed = spec;
    if (placed.maxLevel > 0 && placed.mipAddress == 0) {
        Spec topOnly = spec;
        topOnly.maxLevel = 0;
        const xds::Texture top = xds::Texture::parse(file(topOnly, std::size_t{64} << 20));
        if (!top.levelLayout(0).packed) {
            placed.mipAddress = static_cast<std::uint32_t>(top.levelLayout(0).surfaceSize);
        }
    }
    const xds::Texture probe = xds::Texture::parse(file(placed, std::size_t{64} << 20));
    std::uint64_t end = 0;
    for (std::uint32_t level = 0; level < probe.levelCount(); ++level) {
        const xds::LevelLayout& layout = probe.levelLayout(level);
        end = std::max(end, layout.surfaceOffset + layout.surfaceSize);
    }
    std::vector<std::uint8_t> bytes = file(placed, static_cast<std::size_t>(end));
    const std::uint32_t blockBytes = xds::bytesPerBlock(spec.format);
    const auto log2Bytes = static_cast<std::uint32_t>(std::countr_zero(blockBytes));
    for (std::uint32_t level = 0; level < probe.levelCount(); ++level) {
        const xds::LevelLayout& layout = probe.levelLayout(level);
        for (std::uint32_t by = 0; by < layout.blocksHigh; ++by) {
            for (std::uint32_t bx = 0; bx < layout.blocksWide; ++bx) {
                std::vector<std::uint8_t> data = block(level, bx, by);
                toFileOrder(data.data(), data.size(), spec.endian);
                const std::uint32_t x = bx + layout.offsetXBlocks;
                const std::uint32_t y = by + layout.offsetYBlocks;
                const std::uint64_t within = spec.tiled
                    ? xds::tiledOffset2D(x, y, layout.surfacePitchBlocks, log2Bytes)
                    : (static_cast<std::uint64_t>(y) * layout.surfacePitchBlocks + x) * blockBytes;
                std::memcpy(bytes.data() + xds::kHeaderSize + layout.surfaceOffset + within, data.data(), data.size());
            }
        }
    }
    return bytes;
}

} // namespace synthetic
