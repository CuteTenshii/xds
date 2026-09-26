#include "internal.h"

#include <xds/xds.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>

namespace xds {

namespace {

void appendChunk(std::vector<std::uint8_t>& out, const char (&type)[5], std::span<const std::uint8_t> payload)
{
    if (payload.size() > std::numeric_limits<std::int32_t>::max()) {
        throw Error("PNG chunk is too large");
    }
    const std::size_t start = out.size();
    out.resize(start + 8);
    detail::putBe32(out.data() + start, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(out.data() + start + 4, type, 4);
    out.insert(out.end(), payload.begin(), payload.end());

    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, out.data() + start + 4, 4);
    // zlib's length parameter is 32 bits wide, so feed large payloads in
    // pieces.
    std::size_t done = 0;
    while (done < payload.size()) {
        const auto piece = static_cast<uInt>(std::min<std::size_t>(payload.size() - done, 1u << 30));
        crc = crc32(crc, payload.data() + done, piece);
        done += piece;
    }
    const std::size_t crcAt = out.size();
    out.resize(crcAt + 4);
    detail::putBe32(out.data() + crcAt, static_cast<std::uint32_t>(crc));
}

} // namespace

std::vector<std::uint8_t> writePng(const Image& image)
{
    if (image.width == 0 || image.height == 0) {
        throw Error("cannot write an empty image as PNG");
    }
    const std::size_t rowBytes = static_cast<std::size_t>(image.width) * 4;
    if (image.rgba.size() != rowBytes * image.height) {
        throw Error("image buffer is " + std::to_string(image.rgba.size()) + " bytes, expected "
            + std::to_string(rowBytes * image.height));
    }

    // Each row is prefixed with filter type 0 (none).
    std::vector<std::uint8_t> raw((rowBytes + 1) * image.height);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        std::uint8_t* row = raw.data() + y * (rowBytes + 1);
        row[0] = 0;
        std::memcpy(row + 1, image.rgba.data() + y * rowBytes, rowBytes);
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()),
            Z_DEFAULT_COMPRESSION)
        != Z_OK) {
        throw Error("zlib failed to compress the PNG image data");
    }
    compressed.resize(compressedSize);

    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::array<std::uint8_t, 13> ihdr{};
    detail::putBe32(ihdr.data(), image.width);
    detail::putBe32(ihdr.data() + 4, image.height);
    // 8-bit RGBA; compression, filter and interlace methods stay 0.
    ihdr[8] = 8;
    ihdr[9] = 6;
    appendChunk(out, "IHDR", ihdr);
    appendChunk(out, "IDAT", compressed);
    appendChunk(out, "IEND", {});
    return out;
}

} // namespace xds
