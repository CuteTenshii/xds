// Runs every .xds file under $XDS_SAMPLE_DIR through the library and checks
// that each mip level looks like a half-size copy of the level above it. A
// level read from the wrong place (a bad packed-tail offset, a wrong surface
// size) fails that comparison, so this validates the layout against real
// game data. Without the variable set, the test reports that it was skipped.
//
// Levels with both sides of at least kLargeLevel texels must all match.
// Smaller levels are a handful of DXT blocks, where 1-bit DXT1 alpha and
// per-level re-encoding can legitimately stray from a box filter, so they
// are judged together: a layout bug would break nearly all of them.

#include <xds/xds.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

/// Largest mean per-channel difference, out of 255, accepted between a level
/// and the box-filtered level above it. DXT re-encoding of each level and the
/// filters the art tools used leave differences well below this, while a
/// misplaced level is essentially uncorrelated noise.
constexpr double kMaxMeanDifference = 24.0;
/// Returned by ctest's SKIP_RETURN_CODE when no sample directory is given.
constexpr int kSkipped = 77;
constexpr std::uint32_t kLargeLevel = 32;
constexpr double kMinSmallLevelPassRate = 0.99;

double meanDifference(const xds::Image& upper, const xds::Image& lower)
{
    double total = 0;
    std::size_t count = 0;
    const std::uint32_t scaleX = upper.width > 1 ? 2 : 1;
    const std::uint32_t scaleY = upper.height > 1 ? 2 : 1;
    for (std::uint32_t y = 0; y < lower.height; ++y) {
        for (std::uint32_t x = 0; x < lower.width; ++x) {
            for (std::uint32_t c = 0; c < 4; ++c) {
                double sum = 0;
                std::uint32_t samples = 0;
                for (std::uint32_t dy = 0; dy < scaleY; ++dy) {
                    for (std::uint32_t dx = 0; dx < scaleX; ++dx) {
                        const std::uint32_t ux = std::min(x * scaleX + dx, upper.width - 1);
                        const std::uint32_t uy = std::min(y * scaleY + dy, upper.height - 1);
                        sum += upper.rgba[(static_cast<std::size_t>(uy) * upper.width + ux) * 4 + c];
                        ++samples;
                    }
                }
                const double expected = sum / samples;
                total += std::abs(expected - lower.rgba[(static_cast<std::size_t>(y) * lower.width + x) * 4 + c]);
                ++count;
            }
        }
    }
    return count == 0 ? 0 : total / static_cast<double>(count);
}

} // namespace

int main()
{
    const char* dir = std::getenv("XDS_SAMPLE_DIR");
    if (dir == nullptr || *dir == '\0') {
        std::cout << "XDS_SAMPLE_DIR is not set; skipping the real-data check\n";
        return kSkipped;
    }

    std::size_t files = 0;
    std::size_t errors = 0;
    std::size_t largeChecked = 0;
    std::size_t largeFailed = 0;
    std::size_t smallChecked = 0;
    std::size_t smallFailed = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension != ".xds") {
            continue;
        }
        ++files;
        const std::string path = entry.path().string();
        try {
            const xds::Texture texture = xds::Texture::load(path);
            xds::Image upper = texture.decodeLevel(0);
            for (std::uint32_t level = 1; level < texture.levelCount(); ++level) {
                xds::Image lower = texture.decodeLevel(level);
                const double difference = meanDifference(upper, lower);
                const bool large = lower.width >= kLargeLevel && lower.height >= kLargeLevel;
                const bool matches = difference <= kMaxMeanDifference;
                (large ? largeChecked : smallChecked) += 1;
                if (!matches) {
                    (large ? largeFailed : smallFailed) += 1;
                    std::cout << (large ? "MISMATCH " : "note: ") << path << " level " << level << " ("
                              << lower.width << 'x' << lower.height << "): mean difference " << difference << " ["
                              << xds::describe(texture.info()) << "]\n";
                }
                upper = std::move(lower);
            }
            (void)xds::writeDds(texture);
        } catch (const std::exception& e) {
            ++errors;
            std::cout << "ERROR " << path << ": " << e.what() << '\n';
        }
    }

    const double smallPassRate = smallChecked == 0
        ? 1.0
        : 1.0 - static_cast<double>(smallFailed) / static_cast<double>(smallChecked);
    std::cout << files << " files, " << errors << " errors\n"
              << largeChecked << " large levels compared, " << largeFailed << " mismatched\n"
              << smallChecked << " small levels compared, " << smallFailed << " mismatched (pass rate "
              << smallPassRate * 100 << "%)\n";
    if (files == 0) {
        std::cout << "no .xds files found under " << dir << '\n';
        return 1;
    }
    return errors == 0 && largeFailed == 0 && smallPassRate >= kMinSmallLevelPassRate ? 0 : 1;
}
