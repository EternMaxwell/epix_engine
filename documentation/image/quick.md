# Image module

`epix.image` supplies CPU image data plus asset loading support.

```cpp
import epix.image;
using namespace epix::image;
```

## Setup and loading

```cpp
app.add_plugins(ImagePlugin{}); // also adds AssetPlugin, registers Image and ImageLoader
auto texture = app.resource<assets::AssetServer>().load<Image>("textures/icon.png");
```

`ImageLoader` supports `png`, `jpg`, `jpeg`, `bmp`, `tga`, `hdr`, `pic`, `psd`, `gif`, `ppm`,
`pgm`, and `pnm`. Loaded three-channel images are converted to the corresponding four-channel
format for WebGPU, and loaded images default to `ImageUsage::Render`.

Loader failures use Bevy's typed errors. `ImageLoader::load` returns
`expected<Image, ImageLoaderError>`, whose alternatives are `Io` (a failed `assets::Reader`),
`FileTexture` (a `FileTextureError` carrying a `TextureError` and the offending path), and
`ArrayLayout`. `TextureError` matches Bevy's alternatives and text, so an unknown extension reports
`InvalidImageExtension`, a container this decoder cannot read (Ktx2, Dds, Basis, Ico, Qoi, Tiff,
WebP, Farbfeld) reports `UnsupportedTextureFormat` with Bevy's format name, and a failed decode
reports `ImageError` with the decoder's message. `to_string()` renders the same text Bevy's
`Display` produces, and `to_exception_ptr()` turns either error into an exception.

## Formats and dimensions

`Format` covers 8-bit (`Grey8`, `GreyAlpha8`, `RGBA8`), 16-bit (`Grey16`, `RGBA16`), and float
(`Grey32F`, `RGBA32F`) pixels. There is deliberately no three-channel format: WebGPU has none, and
Bevy's `Image::from_dynamic` widens three-channel decoder output to four channels with an opaque
alpha, so loaded PNG/JPEG/HDR data arrives as the four-channel equivalent. `getFormatInfo()` returns
channel count, bytes per channel, flags, and `pixelSize()`.

`ImageType` is `e1D`, `e2D`, `e2DArray`, or `e3D`. `ImageUsage` is the bitmask `Main`, `Render`, or
`Both`; inspect/set it with `usage()` and `set_usage()`.

## Creating and editing

Use `create()`, `create1d()`, `create2d()`, `create2d_array()`, or `create3d()` for zeroed storage.
Each also has a range-taking overload that returns `optional<Image>` and rejects a byte-size
mismatch.

```cpp
Image image = Image::create2d(256, 256, Format::RGBA8);
std::array<float, 4> red{1, 0, 0, 1};
if (auto written = image.write(10, 20, red); !written) { /* inspect error */ }
```

Metadata accessors are `width()`, `height()`, `depth_or_layers()`, `depth()`, `layers()`, `type()`,
`format()`, and `format_info()`. `raw_view()`/`raw_view_mut()` expose bytes. `sample()` has 1D, 2D,
and 3D overloads and returns normalized four-channel floats. `write()` accepts normalized float
channels; `write_raw()` requires exactly one format-sized pixel. Errors distinguish out-of-bounds
and data-size mismatch.

`convert(format)`, `resize(width,height)`, and `blur(radius)` return new images. `Image::load()` and
`Image::save()` are synchronous filesystem helpers; application asset loading should normally use
`AssetServer` so handles, caching, and events remain available.

