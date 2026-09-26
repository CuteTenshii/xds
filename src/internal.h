#pragma once

#include <xds/xds.h>

#include <array>
#include <bit>
#include <cstdint>
#include <span>

namespace xds::detail {

inline std::uint32_t be32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
        | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

inline std::uint16_t le16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

inline std::uint32_t le32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void putLe16(std::uint8_t* p, std::uint16_t v)
{
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

inline void putLe32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

inline void putBe32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

inline std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

inline std::uint32_t log2Ceil(std::uint32_t value)
{
    return value <= 1 ? 0 : static_cast<std::uint32_t>(std::bit_width(value - 1));
}

inline std::uint32_t nextPow2(std::uint32_t value)
{
    return std::bit_ceil(value == 0 ? 1u : value);
}

/// Converts the texture's GPU byte order to little-endian in place.
void swapEndian(std::span<std::uint8_t> data, Endian endian);

/// One decoded 4x4 block (or one texel for 8_8_8_8) as X, Y, Z, W
/// components before the fetch swizzle. Index = y * 4 + x.
using Texels = std::array<std::array<std::uint8_t, 4>, 16>;

/// Decodes one little-endian block of `format` into its component values.
/// Formats with a single component replicate it to all four; DXN's missing
/// components are 0 (Z) and 255 (W).
void decodeBlock(std::uint32_t format, const std::uint8_t* block, Texels& out);

} // namespace xds::detail
