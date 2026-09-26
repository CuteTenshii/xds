# xds

A C++20 library for reading Xbox 360 texture files (`.xds`) as shipped by Forza Horizon, plus a command-line converter and a Qt image plugin so Gwenview (and any other Qt application) can open them.

An `.xds` file is a 52-byte big-endian `D3DBaseTexture` header followed by texture memory exactly as the Xenos GPU reads it: tiled, byte-swapped, with small mip levels packed together. The library untiles every mip level, decodes it to RGBA, and writes DDS or PNG.

The library itself depends only on the standard library and zlib, so it can go into archive tools that pull textures out of the game's zips as well as into the Qt plugin.

## Supported textures

2D textures, tiled or linear, with any number of mip levels, in these formats:

| Format                              | Seen in Forza Horizon as | DDS output                 |
|-------------------------------------|--------------------------|----------------------------|
| `8_8_8_8`, `8_8_8_8_AS_16_16_16_16` | UI art, light glows      | A8R8G8B8                   |
| `DXT1`, `DXT1_AS_16_16_16_16`       | most diffuse maps        | DXT1                       |
| `DXT2_3`, `DXT2_3_AS_16_16_16_16`   | map icons                | DXT3                       |
| `DXT4_5`, `DXT4_5_AS_16_16_16_16`   | most textures with alpha | DXT5                       |
| `DXN`                               | normal maps              | BC5 (DX10 header)          |
| `DXT5A`                             | masks                    | A8R8G8B8 (ATI1 on request) |
| `DXT3A`                             | UI patterns              | A8R8G8B8                   |

DDS files keep the compressed blocks when a DDS format shows the texture the way the game samples it: a block format with the identity swizzle. Anything else is decoded, with the swizzle applied, to 32-bit A8R8G8B8. `xds dds --compressed` keeps the raw blocks whenever DDS has a format for them. DXN is written as BC5 under a DX10 header because readers disagree about which channel comes first in the older `ATI2` FourCC. On the disc's 224 DXN normal maps the first block is X (red). KImageFormats 6.30 would load `ATI2` with red and green swapped, and cannot read DX10 BC5 at all. `xds dds --bgra8` writes a file that any DDS reader opens correctly.

Cube maps, volume textures, stacked arrays and the remaining GPU formats are rejected with an error naming what was found.

The layout rules (packed mip tail offsets, power-of-two mip storage, 4 KiB surface alignment, 256-byte linear rows) were checked against all 9,656 `.xds` files on the Forza Horizon disc: every file parses, every mip level of 32×32 texels or more matches a downscaled copy of the level above, and 99.8% of smaller levels do (the rest are DXT1 cut-out alpha, which cannot match a box filter).

One behavior is an assumption rather than something checked against the console: `DXT3A` and `DXT5A` hold one component, and the decoder copies it into all four before the swizzle. That makes the `111W` swizzle these masks use come out as white with the mask as alpha.

## Build

Requires CMake 3.21 or newer, a C++20 compiler and zlib. The Qt plugin also needs Qt 6.2 or newer (Core, Gui, and Test for its test).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DXDS_BUILD_QT_PLUGIN=ON
cmake --build build
ctest --test-dir build
```

Options: `XDS_BUILD_TOOLS` (default `ON`), `XDS_BUILD_TESTS` (default `ON`), `XDS_BUILD_QT_PLUGIN` (default `OFF`).

To also check the library against real game files, point `XDS_SAMPLE_DIR` at a directory of `.xds` files (searched recursively). Without it the test is reported as skipped.

```sh
XDS_SAMPLE_DIR=/path/to/extracted/textures ctest --test-dir build -R realdata --output-on-failure
```

## Command-line tool

```sh
xds info treadmark.xds                 # format, size, swizzle and where each mip level lives
xds dds treadmark.xds                  # treadmark.dds, every mip level
xds dds wfp.xds out.dds --compressed   # fails: 8_8_8_8 has no compressed form
xds png treadmark.xds --level 2        # treadmark.png, 128x32
```

## Library

```cpp
#include <xds/xds.h>

const xds::Texture texture = xds::Texture::load("treadmark.xds");
const xds::TextureInfo& info = texture.info();      // format, size, swizzle, levels...

xds::Image top = texture.decodeLevel(0);             // RGBA8, swizzle applied
xds::Surface raw = texture.extractLevel(1);          // untiled little-endian DXT5 blocks
std::vector<std::uint8_t> dds = xds::writeDds(texture);
std::vector<std::uint8_t> png = xds::writePng(top);
```

`Texture::parse()` takes the file's bytes from memory, for data read out of an archive. Errors are thrown as `xds::Error`. `xds::looksLikeXds()` checks a header cheaply, for format sniffing.

With CMake, either add this directory with `add_subdirectory` and link `xds::xds`, or install it and use `find_package(xds)`.

## Gwenview

Gwenview opens any image type that Qt's image plugins can read, and recognizes files by MIME type, so it needs two things installed: the plugin and the `image/x-xds` MIME type.

System-wide:

```sh
cmake -S . -B build -DXDS_BUILD_QT_PLUGIN=ON -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
sudo update-mime-database /usr/share/mime
```

This installs `imageformats/libxds.so` into Qt's plugin directory and `x-xds.xml` into `share/mime/packages`. Without the MIME type, `.xds` files are `application/octet-stream` and Gwenview will not open them even with the plugin present.

For the current user only, install into a prefix you own and tell Qt where the plugin is:

```sh
cmake --install build --prefix ~/.local
update-mime-database ~/.local/share/mime
export QT_PLUGIN_PATH=~/.local/lib/qt6/plugins   # e.g. in ~/.config/plasma-workspace/env/
```

Gwenview shows the top mip level, swizzled the way the game samples it.

## License

This library is licensed under the MIT License. See [LICENSE](LICENSE)
