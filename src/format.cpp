#include "internal.h"

#include <xds/xds.h>

#include <array>
#include <string>

namespace xds {

namespace {

constexpr std::array<std::string_view, 64> kFormatNames{
    "1_REVERSE",
    "1",
    "8",
    "1_5_5_5",
    "5_6_5",
    "6_5_5",
    "8_8_8_8",
    "2_10_10_10",
    "8_A",
    "8_B",
    "8_8",
    "Cr_Y1_Cb_Y0_REP",
    "Y1_Cr_Y0_Cb_REP",
    "16_16_EDRAM",
    "8_8_8_8_A",
    "4_4_4_4",
    "10_11_11",
    "11_11_10",
    "DXT1",
    "DXT2_3",
    "DXT4_5",
    "16_16_16_16_EDRAM",
    "24_8",
    "24_8_FLOAT",
    "16",
    "16_16",
    "16_16_16_16",
    "16_EXPAND",
    "16_16_EXPAND",
    "16_16_16_16_EXPAND",
    "16_FLOAT",
    "16_16_FLOAT",
    "16_16_16_16_FLOAT",
    "32",
    "32_32",
    "32_32_32_32",
    "32_FLOAT",
    "32_32_FLOAT",
    "32_32_32_32_FLOAT",
    "32_AS_8",
    "32_AS_8_8",
    "16_MPEG",
    "16_16_MPEG",
    "8_INTERLACED",
    "32_AS_8_INTERLACED",
    "32_AS_8_8_INTERLACED",
    "16_INTERLACED",
    "16_MPEG_INTERLACED",
    "16_16_MPEG_INTERLACED",
    "DXN",
    "8_8_8_8_AS_16_16_16_16",
    "DXT1_AS_16_16_16_16",
    "DXT2_3_AS_16_16_16_16",
    "DXT4_5_AS_16_16_16_16",
    "2_10_10_10_AS_16_16_16_16",
    "10_11_11_AS_16_16_16_16",
    "11_11_10_AS_16_16_16_16",
    "32_32_32_FLOAT",
    "DXT3A",
    "DXT5A",
    "CTX1",
    "DXT3A_AS_1_1_1_1",
    "8_8_8_8_GAMMA_EDRAM",
    "2_10_10_10_FLOAT_EDRAM",
};

using detail::kCommonTypeTexture;
using detail::kFetchTypeTexture;
constexpr std::uint32_t kCommonTypeMask = 0xF;

std::int32_t signExtend(std::uint32_t value, unsigned bits)
{
    const std::uint32_t sign = 1u << (bits - 1);
    return static_cast<std::int32_t>((value ^ sign) - sign);
}

Error unsupportedFormat(std::uint32_t format)
{
    return Error("unsupported texture format " + std::to_string(format) + " (" + std::string(formatName(format)) + ")");
}

} // namespace

std::string_view formatName(std::uint32_t format)
{
    return format < kFormatNames.size() ? kFormatNames[format] : std::string_view("?");
}

bool isFormatSupported(std::uint32_t format)
{
    if (format >= kFormatNames.size()) {
        return false;
    }
    switch (static_cast<Format>(format)) {
    case Format::k8_8_8_8:
    case Format::kDxt1:
    case Format::kDxt2_3:
    case Format::kDxt4_5:
    case Format::kDxn:
    case Format::k8_8_8_8_As16_16_16_16:
    case Format::kDxt1_As16_16_16_16:
    case Format::kDxt2_3_As16_16_16_16:
    case Format::kDxt4_5_As16_16_16_16:
    case Format::kDxt3A:
    case Format::kDxt5A:
        return true;
    }
    return false;
}

std::uint32_t blockEdge(std::uint32_t format)
{
    if (!isFormatSupported(format)) {
        throw unsupportedFormat(format);
    }
    switch (static_cast<Format>(format)) {
    case Format::k8_8_8_8:
    case Format::k8_8_8_8_As16_16_16_16:
        return 1;
    case Format::kDxt1:
    case Format::kDxt2_3:
    case Format::kDxt4_5:
    case Format::kDxn:
    case Format::kDxt1_As16_16_16_16:
    case Format::kDxt2_3_As16_16_16_16:
    case Format::kDxt4_5_As16_16_16_16:
    case Format::kDxt3A:
    case Format::kDxt5A:
        return 4;
    }
    throw unsupportedFormat(format);
}

std::uint32_t bytesPerBlock(std::uint32_t format)
{
    if (!isFormatSupported(format)) {
        throw unsupportedFormat(format);
    }
    switch (static_cast<Format>(format)) {
    case Format::k8_8_8_8:
    case Format::k8_8_8_8_As16_16_16_16:
        return 4;
    case Format::kDxt1:
    case Format::kDxt1_As16_16_16_16:
    case Format::kDxt3A:
    case Format::kDxt5A:
        return 8;
    case Format::kDxt2_3:
    case Format::kDxt4_5:
    case Format::kDxn:
    case Format::kDxt2_3_As16_16_16_16:
    case Format::kDxt4_5_As16_16_16_16:
        return 16;
    }
    throw unsupportedFormat(format);
}

std::string swizzleString(const std::array<Swizzle, 4>& swizzle)
{
    static constexpr std::string_view kNames = "XYZW01";
    std::string result;
    for (const Swizzle s : swizzle) {
        const auto index = static_cast<std::size_t>(s);
        result += index < kNames.size() ? kNames[index] : '?';
    }
    return result;
}

Header readHeader(std::span<const std::uint8_t> data)
{
    if (data.size() < kHeaderSize) {
        throw Error("file is " + std::to_string(data.size()) + " bytes, shorter than the " + std::to_string(kHeaderSize)
            + "-byte texture header");
    }
    Header header;
    header.common = detail::be32(data.data());
    header.referenceCount = detail::be32(data.data() + 4);
    header.fence = detail::be32(data.data() + 8);
    header.readFence = detail::be32(data.data() + 12);
    header.identifier = detail::be32(data.data() + 16);
    header.baseFlush = detail::be32(data.data() + 20);
    header.mipFlush = detail::be32(data.data() + 24);
    for (std::size_t i = 0; i < header.fetchConstant.size(); ++i) {
        header.fetchConstant[i] = detail::be32(data.data() + 28 + 4 * i);
    }
    return header;
}

TextureInfo decodeFetchConstant(const std::array<std::uint32_t, 6>& fetch)
{
    // Field positions follow the Xenos GPUTEXTURE_FETCH_CONSTANT layout.
    TextureInfo info;
    const std::uint32_t w0 = fetch[0];
    const std::uint32_t w1 = fetch[1];
    const std::uint32_t w2 = fetch[2];
    const std::uint32_t w3 = fetch[3];
    const std::uint32_t w4 = fetch[4];
    const std::uint32_t w5 = fetch[5];

    info.signedFormat = ((w0 >> 2) & 0xFF) != 0;
    info.pitch = ((w0 >> 22) & 0x1FF) << 5;
    info.tiled = (w0 >> 31) != 0;

    info.format = w1 & 0x3F;
    info.endian = static_cast<Endian>((w1 >> 6) & 3);
    const bool stacked = ((w1 >> 10) & 1) != 0;
    info.baseAddress = w1 & 0xFFFFF000u;

    info.dimension = static_cast<Dimension>((w5 >> 9) & 3);
    switch (info.dimension) {
    case Dimension::OneD:
        info.width = (w2 & 0xFFFFFF) + 1;
        info.height = 1;
        break;
    case Dimension::TwoD:
        info.width = (w2 & 0x1FFF) + 1;
        info.height = ((w2 >> 13) & 0x1FFF) + 1;
        info.depth = stacked ? ((w2 >> 26) & 0x3F) + 1 : 1;
        break;
    case Dimension::ThreeD:
        info.width = (w2 & 0x7FF) + 1;
        info.height = ((w2 >> 11) & 0x7FF) + 1;
        info.depth = ((w2 >> 22) & 0x3FF) + 1;
        break;
    case Dimension::Cube:
        info.width = (w2 & 0x1FFF) + 1;
        info.height = ((w2 >> 13) & 0x1FFF) + 1;
        info.depth = 6;
        break;
    }

    const std::uint32_t swizzle = (w3 >> 1) & 0xFFF;
    for (std::size_t i = 0; i < 4; ++i) {
        const std::uint32_t source = (swizzle >> (3 * i)) & 7;
        // Codes 6 and 7 have no defined meaning; read them as 1 rather than
        // rejecting the texture.
        info.swizzle[i] = static_cast<Swizzle>(source > 5 ? 5 : source);
    }
    info.expAdjust = signExtend((w3 >> 13) & 0x3F, 6);

    info.minMipLevel = (w4 >> 2) & 0xF;
    info.maxMipLevel = (w4 >> 6) & 0xF;

    info.packedMips = ((w5 >> 11) & 1) != 0;
    info.mipAddress = w5 & 0xFFFFF000u;
    return info;
}

std::array<std::uint32_t, 6> encodeFetchConstant(const TextureInfo& info)
{
    // The inverse of decodeFetchConstant(), field for field.
    const std::uint32_t signedChannels = info.signedFormat ? 0x55u : 0u;
    const std::uint32_t w0 = kFetchTypeTexture | (signedChannels << 2) | (((info.pitch >> 5) & 0x1FF) << 22)
        | (info.tiled ? 1u << 31 : 0u);

    const bool stacked = info.dimension == Dimension::TwoD && info.depth > 1;
    const std::uint32_t w1 = (info.format & 0x3F) | ((static_cast<std::uint32_t>(info.endian) & 3) << 6)
        | (stacked ? 1u << 10 : 0u) | (info.baseAddress & 0xFFFFF000u);

    const std::uint32_t width = info.width == 0 ? 0 : info.width - 1;
    const std::uint32_t height = info.height == 0 ? 0 : info.height - 1;
    const std::uint32_t depth = info.depth == 0 ? 0 : info.depth - 1;
    std::uint32_t w2 = 0;
    switch (info.dimension) {
    case Dimension::OneD:
        w2 = width & 0xFFFFFF;
        break;
    case Dimension::TwoD:
        w2 = (width & 0x1FFF) | ((height & 0x1FFF) << 13) | (stacked ? (depth & 0x3F) << 26 : 0u);
        break;
    case Dimension::ThreeD:
        w2 = (width & 0x7FF) | ((height & 0x7FF) << 11) | ((depth & 0x3FF) << 22);
        break;
    case Dimension::Cube:
        w2 = (width & 0x1FFF) | ((height & 0x1FFF) << 13);
        break;
    }

    std::uint32_t swizzle = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        swizzle |= static_cast<std::uint32_t>(info.swizzle[i]) << (3 * i);
    }
    const std::uint32_t w3 = (swizzle << 1) | ((static_cast<std::uint32_t>(info.expAdjust) & 0x3F) << 13);
    const std::uint32_t w4 = ((info.minMipLevel & 0xF) << 2) | ((info.maxMipLevel & 0xF) << 6);
    const std::uint32_t w5 = ((static_cast<std::uint32_t>(info.dimension) & 3) << 9)
        | (info.packedMips ? 1u << 11 : 0u) | (info.mipAddress & 0xFFFFF000u);
    return {w0, w1, w2, w3, w4, w5};
}

bool looksLikeXds(std::span<const std::uint8_t> data)
{
    if (data.size() < kHeaderSize) {
        return false;
    }
    const Header header = readHeader(data);
    if ((header.common & kCommonTypeMask) != kCommonTypeTexture) {
        return false;
    }
    if ((header.fetchConstant[0] & 3) != kFetchTypeTexture) {
        return false;
    }
    const TextureInfo info = decodeFetchConstant(header.fetchConstant);
    return info.maxMipLevel >= info.minMipLevel && info.pitch != 0;
}

std::string describe(const TextureInfo& info)
{
    std::string text(formatName(info.format));
    text += ' ' + std::to_string(info.width) + 'x' + std::to_string(info.height);
    if (info.dimension == Dimension::ThreeD || info.depth > 1) {
        text += 'x' + std::to_string(info.depth);
    }
    const std::uint32_t levels = info.levelCount();
    text += ", " + std::to_string(levels) + (levels == 1 ? " level" : " levels");
    text += info.tiled ? ", tiled" : ", linear";
    text += ", swizzle " + swizzleString(info.swizzle);
    return text;
}

} // namespace xds
