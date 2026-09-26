#include "internal.h"

#include <xds/xds.h>

#include <string>
#include <utility>

namespace xds::detail {

namespace {

using Rgb = std::array<std::uint8_t, 3>;

Rgb expand565(std::uint16_t v)
{
    const auto r = static_cast<std::uint8_t>((v >> 11) & 31);
    const auto g = static_cast<std::uint8_t>((v >> 5) & 63);
    const auto b = static_cast<std::uint8_t>(v & 31);
    return {static_cast<std::uint8_t>((r << 3) | (r >> 2)), static_cast<std::uint8_t>((g << 2) | (g >> 4)),
        static_cast<std::uint8_t>((b << 3) | (b >> 2))};
}

Rgb blend(const Rgb& a, const Rgb& b, int weightA, int weightB)
{
    const int total = weightA + weightB;
    Rgb result{};
    for (std::size_t i = 0; i < 3; ++i) {
        result[i] = static_cast<std::uint8_t>((a[i] * weightA + b[i] * weightB + total / 2) / total);
    }
    return result;
}

/// The 8-byte color half of a DXT block. In DXT1, c0 <= c1 selects the
/// three-color mode whose fourth entry is transparent black; DXT2-5 always
/// use four colors.
void decodeColor(const std::uint8_t* block, bool dxt1, Texels& out)
{
    const std::uint16_t c0 = le16(block);
    const std::uint16_t c1 = le16(block + 2);
    const std::uint32_t indices = le32(block + 4);
    std::array<Rgb, 4> palette{expand565(c0), expand565(c1), Rgb{}, Rgb{}};
    std::array<std::uint8_t, 4> alpha{255, 255, 255, 255};
    if (!dxt1 || c0 > c1) {
        palette[2] = blend(palette[0], palette[1], 2, 1);
        palette[3] = blend(palette[0], palette[1], 1, 2);
    } else {
        palette[2] = blend(palette[0], palette[1], 1, 1);
        alpha[3] = 0;
    }
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint32_t index = (indices >> (2 * i)) & 3;
        out[i][0] = palette[index][0];
        out[i][1] = palette[index][1];
        out[i][2] = palette[index][2];
        if (dxt1) {
            out[i][3] = alpha[index];
        }
    }
}

/// Sixteen 4-bit values, as in DXT2/3 alpha and DXT3A.
std::array<std::uint8_t, 16> decodeExplicit4(const std::uint8_t* block)
{
    std::array<std::uint8_t, 16> values{};
    for (std::size_t i = 0; i < 16; ++i) {
        const int nibble = (block[i / 2] >> ((i % 2) * 4)) & 15;
        values[i] = static_cast<std::uint8_t>(nibble * 17);
    }
    return values;
}

/// Two 8-bit endpoints and sixteen 3-bit indices, as in DXT4/5 alpha, DXT5A
/// and each half of DXN.
std::array<std::uint8_t, 16> decodeInterpolated8(const std::uint8_t* block)
{
    const int a0 = block[0];
    const int a1 = block[1];
    std::array<int, 8> palette{a0, a1, 0, 0, 0, 0, 0, 0};
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) {
            palette[static_cast<std::size_t>(i) + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            palette[static_cast<std::size_t>(i) + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
        }
        palette[6] = 0;
        palette[7] = 255;
    }
    std::uint64_t indices = 0;
    for (std::size_t i = 0; i < 6; ++i) {
        indices |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
    }
    std::array<std::uint8_t, 16> values{};
    for (std::size_t i = 0; i < 16; ++i) {
        values[i] = static_cast<std::uint8_t>(palette[(indices >> (3 * i)) & 7]);
    }
    return values;
}

void replicate(const std::array<std::uint8_t, 16>& values, Texels& out)
{
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = {values[i], values[i], values[i], values[i]};
    }
}

} // namespace

void swapEndian(std::span<std::uint8_t> data, Endian endian)
{
    const std::size_t size = data.size();
    switch (endian) {
    case Endian::None:
        return;
    case Endian::Swap8In16:
        for (std::size_t i = 0; i + 1 < size; i += 2) {
            std::swap(data[i], data[i + 1]);
        }
        return;
    case Endian::Swap8In32:
        for (std::size_t i = 0; i + 3 < size; i += 4) {
            std::swap(data[i], data[i + 3]);
            std::swap(data[i + 1], data[i + 2]);
        }
        return;
    case Endian::Swap16In32:
        for (std::size_t i = 0; i + 3 < size; i += 4) {
            std::swap(data[i], data[i + 2]);
            std::swap(data[i + 1], data[i + 3]);
        }
        return;
    }
}

void decodeBlock(std::uint32_t format, const std::uint8_t* block, Texels& out)
{
    switch (static_cast<Format>(format)) {
    case Format::k8_8_8_8:
    case Format::k8_8_8_8_As16_16_16_16:
        // Component X is the least significant byte of the 32-bit texel.
        out[0] = {block[0], block[1], block[2], block[3]};
        return;
    case Format::kDxt1:
    case Format::kDxt1_As16_16_16_16:
        decodeColor(block, true, out);
        return;
    case Format::kDxt2_3:
    case Format::kDxt2_3_As16_16_16_16: {
        const auto alpha = decodeExplicit4(block);
        decodeColor(block + 8, false, out);
        for (std::size_t i = 0; i < 16; ++i) {
            out[i][3] = alpha[i];
        }
        return;
    }
    case Format::kDxt4_5:
    case Format::kDxt4_5_As16_16_16_16: {
        const auto alpha = decodeInterpolated8(block);
        decodeColor(block + 8, false, out);
        for (std::size_t i = 0; i < 16; ++i) {
            out[i][3] = alpha[i];
        }
        return;
    }
    case Format::kDxn: {
        const auto x = decodeInterpolated8(block);
        const auto y = decodeInterpolated8(block + 8);
        for (std::size_t i = 0; i < 16; ++i) {
            out[i] = {x[i], y[i], 0, 255};
        }
        return;
    }
    case Format::kDxt3A:
        replicate(decodeExplicit4(block), out);
        return;
    case Format::kDxt5A:
        replicate(decodeInterpolated8(block), out);
        return;
    }
    throw Error("unsupported texture format " + std::to_string(format) + " (" + std::string(formatName(format)) + ")");
}

} // namespace xds::detail
