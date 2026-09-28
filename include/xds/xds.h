#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// Reader for Xbox 360 texture files (.xds), as shipped by Forza Horizon and
/// other titles built on the XDK's D3D.
///
/// An .xds file is a D3DBaseTexture header (52 bytes, big-endian) followed by
/// the texture memory exactly as the GPU reads it: tiled, byte-swapped and
/// laid out with Xenos mip packing. This library turns that into plain
/// row-major data, RGBA images, DDS files and PNG files.
namespace xds {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr std::size_t kHeaderSize = 52;

/// GPUTEXTUREFORMAT values that this library can decode. The fetch constant
/// holds a 6-bit format; values outside this list are reported by number and
/// name but rejected when decoding.
enum class Format : std::uint8_t {
    k8_8_8_8 = 6,
    kDxt1 = 18,
    kDxt2_3 = 19,
    kDxt4_5 = 20,
    kDxn = 49,
    k8_8_8_8_As16_16_16_16 = 50,
    kDxt1_As16_16_16_16 = 51,
    kDxt2_3_As16_16_16_16 = 52,
    kDxt4_5_As16_16_16_16 = 53,
    kDxt3A = 58,
    kDxt5A = 59,
};

/// Byte order of the texture memory relative to little-endian texels.
enum class Endian : std::uint8_t {
    None = 0,
    Swap8In16 = 1,
    Swap8In32 = 2,
    Swap16In32 = 3,
};

enum class Dimension : std::uint8_t {
    OneD = 0,
    TwoD = 1,
    ThreeD = 2,
    Cube = 3,
};

/// Source of one output channel in the fetch constant's swizzle.
enum class Swizzle : std::uint8_t {
    X = 0,
    Y = 1,
    Z = 2,
    W = 3,
    Zero = 4,
    One = 5,
};

/// Everything in the header that describes the texture. Addresses are byte
/// offsets into the texture data that follows the header.
struct TextureInfo {
    std::uint32_t format = 0;
    Endian endian = Endian::None;
    Dimension dimension = Dimension::TwoD;
    bool tiled = false;
    bool packedMips = false;
    bool signedFormat = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    /// Row pitch of the top level, in texels.
    std::uint32_t pitch = 0;
    std::uint32_t baseAddress = 0;
    std::uint32_t mipAddress = 0;
    std::uint32_t minMipLevel = 0;
    std::uint32_t maxMipLevel = 0;
    std::array<Swizzle, 4> swizzle{Swizzle::X, Swizzle::Y, Swizzle::Z, Swizzle::W};
    int expAdjust = 0;

    std::uint32_t levelCount() const { return maxMipLevel + 1; }
};

/// The raw 52-byte header: the D3DResource fields, then the six-dword GPU
/// texture fetch constant that TextureInfo is decoded from.
struct Header {
    std::uint32_t common = 0;
    std::uint32_t referenceCount = 0;
    std::uint32_t fence = 0;
    std::uint32_t readFence = 0;
    std::uint32_t identifier = 0;
    std::uint32_t baseFlush = 0;
    std::uint32_t mipFlush = 0;
    std::array<std::uint32_t, 6> fetchConstant{};
};

/// Where one mip level lives in the texture data.
///
/// Levels at or past the first one whose shorter side is 16 texels or less
/// share a single surface (the packed mip tail), each at its own block offset
/// inside it.
struct LevelLayout {
    std::uint32_t level = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t blocksWide = 0;
    std::uint32_t blocksHigh = 0;
    std::uint64_t surfaceOffset = 0;
    std::uint64_t surfaceSize = 0;
    /// Row pitch of the surface in blocks. For linear surfaces this is the
    /// byte pitch divided by the block size.
    std::uint32_t surfacePitchBlocks = 0;
    std::uint32_t surfaceHeightBlocks = 0;
    std::uint32_t offsetXBlocks = 0;
    std::uint32_t offsetYBlocks = 0;
    bool packed = false;
};

/// One mip level as row-major blocks in little-endian byte order: what a
/// DDS file or a PC GPU expects for the same format.
struct Surface {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t blocksWide = 0;
    std::uint32_t blocksHigh = 0;
    std::uint32_t bytesPerBlock = 0;
    std::vector<std::uint8_t> data;
};

/// 8-bit RGBA pixels, row-major, no padding between rows.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

class Texture {
public:
    /// Parses an .xds file held in memory. Throws Error if the header is not
    /// a 2D texture this library can lay out, or if the data is too short
    /// for the levels the header declares.
    static Texture parse(std::span<const std::uint8_t> file);
    static Texture parse(std::vector<std::uint8_t>&& file);
    /// Like parse(), reading the file first. Throws Error if it cannot be read.
    static Texture load(const std::string& path);
    /// A texture from its description and its texture memory, for data that
    /// comes without an .xds header, such as textures a game packs in
    /// containers of its own. `info` holds what the fetch constant would:
    /// `baseAddress` and `mipAddress` are byte offsets into `data`, which is
    /// tiled and byte-swapped as the GPU reads it. Throws Error in the same
    /// cases as parse(). header() then holds `info` encoded as a fetch
    /// constant (see encodeFetchConstant()), with the other fields of a
    /// texture resource zero.
    static Texture fromData(const TextureInfo& info, std::span<const std::uint8_t> data);
    static Texture fromData(const TextureInfo& info, std::vector<std::uint8_t>&& data);

    const Header& header() const { return m_header; }
    const TextureInfo& info() const { return m_info; }
    std::uint32_t levelCount() const { return m_info.levelCount(); }
    const LevelLayout& levelLayout(std::uint32_t level) const;

    /// Untiles and byte-swaps one level. The result keeps the GPU format,
    /// so compressed levels stay compressed.
    Surface extractLevel(std::uint32_t level) const;
    /// Decodes one level to RGBA with the fetch constant's swizzle applied,
    /// which is how the texture looks when the game samples it.
    Image decodeLevel(std::uint32_t level) const;

    std::span<const std::uint8_t> textureData() const;

private:
    Texture() = default;
    /// Reads the header of the .xds file in m_bytes.
    void readFileHeader();
    /// Checks m_info and lays out its levels in textureData().
    void layOut();

    /// The whole .xds file, or only the texture data for fromData().
    std::vector<std::uint8_t> m_bytes;
    /// Where the texture data starts in m_bytes.
    std::size_t m_dataOffset = 0;
    Header m_header;
    TextureInfo m_info;
    std::vector<LevelLayout> m_levels;
};

/// True if the data starts with a header this library recognizes as an
/// Xbox 360 texture. Cheap enough for MIME sniffing.
bool looksLikeXds(std::span<const std::uint8_t> data);

/// Decodes the header fields without touching the texture data.
Header readHeader(std::span<const std::uint8_t> data);
TextureInfo decodeFetchConstant(const std::array<std::uint32_t, 6>& fetchConstant);
/// The fetch constant that decodeFetchConstant() turns back into `info`.
/// Fields the fetch constant has no room for are cut to fit: sizes and
/// addresses to their bit widths, `expAdjust` to 6 bits, and
/// `signedFormat` sets all four channels signed.
std::array<std::uint32_t, 6> encodeFetchConstant(const TextureInfo& info);

/// The XDK name of a GPUTEXTUREFORMAT value, such as "DXT4_5_AS_16_16_16_16".
std::string_view formatName(std::uint32_t format);
bool isFormatSupported(std::uint32_t format);
/// Texel block edge (1 or 4) and bytes per block for a supported format.
std::uint32_t blockEdge(std::uint32_t format);
std::uint32_t bytesPerBlock(std::uint32_t format);
/// The swizzle as four characters from "XYZW01", such as "ZYXW".
std::string swizzleString(const std::array<Swizzle, 4>& swizzle);

/// Byte offset of block (x, y) in a tiled 2D surface whose row pitch is
/// `pitchBlocks` blocks (a multiple of 32) and whose blocks are
/// 2^log2BytesPerBlock bytes: the GPU's XGAddress2DTiledOffset.
std::uint32_t tiledOffset2D(std::uint32_t x, std::uint32_t y, std::uint32_t pitchBlocks,
    std::uint32_t log2BytesPerBlock);

/// How writeDds() stores a texture.
enum class DdsEncoding : std::uint8_t {
    /// The untiled blocks as they are: DXT1, DXT3, DXT5, ATI1 (BC4), or BC5
    /// under a DX10 header.
    Compressed,
    /// Decoded, swizzled 32-bit A8R8G8B8.
    Bgra8,
};

/// Compressed when a DDS FourCC reproduces the texture as the GPU samples
/// it (a block format with the identity swizzle); otherwise Bgra8.
DdsEncoding preferredDdsEncoding(const TextureInfo& info);

/// A DDS file with every mip level.
std::vector<std::uint8_t> writeDds(const Texture& texture);
std::vector<std::uint8_t> writeDds(const Texture& texture, DdsEncoding encoding);

/// A PNG file (8-bit RGBA, non-interlaced).
std::vector<std::uint8_t> writePng(const Image& image);

/// A one-line summary such as
/// "DXT4_5_AS_16_16_16_16 512x128, 10 levels, tiled, swizzle XYZW".
std::string describe(const TextureInfo& info);

} // namespace xds
