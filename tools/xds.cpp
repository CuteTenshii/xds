#include <xds/xds.h>

#include <charconv>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int usage()
{
    std::cerr << "usage:\n"
                 "  xds info <file.xds>...\n"
                 "  xds dds <file.xds> [output.dds] [--compressed | --bgra8]\n"
                 "  xds png <file.xds> [output.png] [--level N]\n"
                 "\n"
                 "dds keeps every mip level. By default it keeps the compressed blocks when a\n"
                 "DDS format shows the texture the way the game samples it, and otherwise\n"
                 "writes decoded 32-bit BGRA.\n";
    return 2;
}

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        throw xds::Error("cannot write " + path.string());
    }
}

std::filesystem::path defaultOutput(const std::filesystem::path& input, std::string_view extension)
{
    std::filesystem::path output = input;
    output.replace_extension(extension);
    return output;
}

void printInfo(const std::string& path)
{
    const xds::Texture texture = xds::Texture::load(path);
    const xds::TextureInfo& info = texture.info();
    std::cout << path << ": " << xds::describe(info) << '\n';
    std::cout << "  format " << info.format << ", endian " << static_cast<int>(info.endian) << ", pitch "
              << info.pitch << ", packed mips " << (info.packedMips ? "yes" : "no") << ", base 0x" << std::hex
              << info.baseAddress << ", mips 0x" << info.mipAddress << std::dec << '\n';
    std::cout << "  DDS output: "
              << (xds::preferredDdsEncoding(info) == xds::DdsEncoding::Compressed ? "compressed" : "BGRA8") << '\n';
    for (std::uint32_t level = 0; level < texture.levelCount(); ++level) {
        const xds::LevelLayout& layout = texture.levelLayout(level);
        std::cout << "  level " << level << ": " << layout.width << 'x' << layout.height << " at 0x" << std::hex
                  << layout.surfaceOffset << std::dec;
        if (layout.packed) {
            std::cout << " (packed, block " << layout.offsetXBlocks << ',' << layout.offsetYBlocks << ')';
        }
        std::cout << '\n';
    }
}

int run(const std::vector<std::string>& args)
{
    if (args.size() < 2) {
        return usage();
    }
    const std::string& command = args[0];

    if (command == "info") {
        int failures = 0;
        for (std::size_t i = 1; i < args.size(); ++i) {
            try {
                printInfo(args[i]);
            } catch (const xds::Error& e) {
                std::cerr << args[i] << ": " << e.what() << '\n';
                ++failures;
            }
        }
        return failures == 0 ? 0 : 1;
    }

    std::vector<std::string> positional;
    std::optional<xds::DdsEncoding> encoding;
    std::uint32_t level = 0;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--compressed") {
            encoding = xds::DdsEncoding::Compressed;
        } else if (arg == "--bgra8") {
            encoding = xds::DdsEncoding::Bgra8;
        } else if (arg == "--level") {
            if (i + 1 >= args.size()) {
                return usage();
            }
            const std::string& value = args[++i];
            const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), level);
            if (ec != std::errc() || end != value.data() + value.size()) {
                std::cerr << "invalid level: " << value << '\n';
                return 2;
            }
        } else if (arg.starts_with("--")) {
            std::cerr << "unknown option: " << arg << '\n';
            return usage();
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.empty() || positional.size() > 2) {
        return usage();
    }
    const std::filesystem::path input = positional[0];

    if (command == "dds") {
        if (level != 0) {
            return usage();
        }
        const xds::Texture texture = xds::Texture::load(input.string());
        const std::vector<std::uint8_t> dds =
            encoding ? xds::writeDds(texture, *encoding) : xds::writeDds(texture);
        writeFile(positional.size() > 1 ? std::filesystem::path(positional[1]) : defaultOutput(input, ".dds"), dds);
        return 0;
    }
    if (command == "png") {
        if (encoding) {
            return usage();
        }
        const xds::Texture texture = xds::Texture::load(input.string());
        const std::vector<std::uint8_t> png = xds::writePng(texture.decodeLevel(level));
        writeFile(positional.size() > 1 ? std::filesystem::path(positional[1]) : defaultOutput(input, ".png"), png);
        return 0;
    }
    return usage();
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    try {
        return run(args);
    } catch (const std::exception& e) {
        std::cerr << "xds: " << e.what() << '\n';
        return 1;
    }
}
