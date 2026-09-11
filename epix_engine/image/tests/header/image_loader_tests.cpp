#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/image/image_loader.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexec/execution.hpp>
#include <string>
#include <system_error>
#include <vector>

using namespace epix;

namespace {

/** @brief A reader that always fails, so the `Io` alternative is reachable. */
struct FailingReader : assets::Reader {
    STDEXEC::task<std::expected<std::size_t, std::error_code>> read_to_end(std::vector<std::uint8_t>&) override {
        co_return std::unexpected(std::make_error_code(std::errc::io_error));
    }
};

/** @brief A reader over bytes already held in memory. */
struct BytesReader : assets::Reader {
    std::vector<std::uint8_t> bytes;
    explicit BytesReader(std::vector<std::uint8_t> value) : bytes(std::move(value)) {}

    STDEXEC::task<std::expected<std::size_t, std::error_code>> read_to_end(std::vector<std::uint8_t>& buf) override {
        buf = bytes;
        co_return bytes.size();
    }
};

/** @brief Directory for the temporary fixtures this file writes. */
std::filesystem::path fixture_dir() {
    const auto dir = std::filesystem::temp_directory_path() / "epix_image_loader_tests";
    std::filesystem::create_directories(dir);
    return dir;
}

/** @brief Write `bytes` to `name` inside the fixture directory and return the
 * resulting path. */
std::filesystem::path write_fixture(const std::string& name, std::string_view bytes) {
    const auto path = fixture_dir() / name;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    return path;
}

/** @brief A real PNG written through the module's own save path, so loader
 * tests never depend on a checked-in fixture. */
std::filesystem::path write_png_fixture() {
    auto image = image::Image::create2d(4, 2, image::Format::RGBA8);
    for (std::uint32_t y = 0; y < 2; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            const auto written =
                image.set_color_at(x, y, glm::vec4(static_cast<float>(x) / 3.0f, static_cast<float>(y), 0.25f, 1.0f));
            EXPECT_TRUE(written.has_value());
        }
    }
    const auto path = fixture_dir() / "core_loader_fixture.png";
    EXPECT_TRUE(image::Image::save(path, image).has_value());
    return path;
}

std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

/** @brief An app that owns an `AssetServer`, enough to build a `LoadContext`. */
struct LoaderTestApp {
    app::App app = app::App::create();
    LoaderTestApp() { app.add_plugins(assets::AssetPlugin{}); }
    const assets::AssetServer& server() { return app.world().resource<assets::AssetServer>(); }
};

/** @brief Run `ImageLoader::load` for `path` over `reader`. */
std::expected<image::Image, image::ImageLoaderError> load_through_loader(LoaderTestApp& fixture,
                                                                         assets::Reader& reader,
                                                                         const std::string& path) {
    assets::LoadContext context(fixture.server(), assets::AssetPath(path));
    auto result = STDEXEC::sync_wait(image::ImageLoader::load(reader, {}, context));
    if (!result.has_value()) {
        ADD_FAILURE() << "ImageLoader::load did not complete";
        return std::unexpected(
            image::ImageLoaderError{image::ImageLoaderError::Io{std::make_error_code(std::errc::io_error)}});
    }
    return std::move(std::get<0>(*result));
}

}  // namespace

// Bevy `TextureError`'s alternatives and Display text (image.rs:1974-2007).
TEST(TextureError, AlternativesMatchBevyDisplayText) {
    using Error     = image::TextureError;
    const auto text = [](const Error& error) { return image::to_string(error); };

    EXPECT_EQ(text(Error{Error::InvalidImageMimeType{"image/unknown"}}), "invalid image mime type: image/unknown");
    EXPECT_EQ(text(Error{Error::InvalidImageExtension{"xyz"}}), "invalid image extension: xyz");
    EXPECT_EQ(text(Error{Error::ImageError{"boom"}}), "failed to load an image: boom");
    EXPECT_EQ(text(Error{Error::UnsupportedTextureFormat{"Ktx2"}}), "unsupported texture format: Ktx2");
    EXPECT_EQ(text(Error{Error::SuperCompressionNotSupported{"Zstd"}}), "supercompression not supported: Zstd");
    EXPECT_EQ(text(Error{Error::SuperDecompressionError{"zstd stream"}}), "failed to decompress an image: zstd stream");
    EXPECT_EQ(text(Error{Error::InvalidData{"nope"}}), "invalid data: nope");
    EXPECT_EQ(text(Error{Error::TranscodeError{"nope"}}), "transcode error: nope");
    EXPECT_EQ(text(Error{Error::IncompleteCubemap{}}), "only cubemaps with six faces are supported");

    // `FormatRequiresTranscodingError` embeds Rust's `Debug` of TranscodeFormat.
    EXPECT_EQ(text(Error{Error::FormatRequiresTranscodingError{image::TranscodeFormat{}}}),
              "format requires transcoding: Etc1s");
    EXPECT_EQ(text(Error{Error::FormatRequiresTranscodingError{
                  image::TranscodeFormat{image::TranscodeFormat::Uastc{image::DataFormat::Rgba}}}}),
              "format requires transcoding: Uastc(Rgba)");
    EXPECT_EQ(text(Error{Error::FormatRequiresTranscodingError{
                  image::TranscodeFormat{image::TranscodeFormat::R8UnormSrgb{}}}}),
              "format requires transcoding: R8UnormSrgb");
    EXPECT_EQ(text(Error{Error::FormatRequiresTranscodingError{
                  image::TranscodeFormat{image::TranscodeFormat::Rg8UnormSrgb{}}}}),
              "format requires transcoding: Rg8UnormSrgb");
    EXPECT_EQ(
        text(Error{Error::FormatRequiresTranscodingError{image::TranscodeFormat{image::TranscodeFormat::Rgb8{}}}}),
        "format requires transcoding: Rgb8");

    // Every alternative is distinguishable, and the exception carries the text.
    EXPECT_NE(Error{Error::InvalidData{"a"}}, Error{Error::InvalidData{"b"}});
    EXPECT_NE(Error{Error::IncompleteCubemap{}}, Error{Error::InvalidData{""}});
    EXPECT_THROW(std::rethrow_exception(image::to_exception_ptr(Error{Error::ImageError{"boom"}})), std::runtime_error);
}

// Bevy `TextureReinterpretationError`'s Display text (image.rs:1950-1962).
TEST(TextureReinterpretationError, AlternativesMatchBevyDisplayText) {
    using Error = image::TextureReinterpretationError;
    EXPECT_EQ(image::to_string(Error{Error::WrongDimension{}}), "must be a 2d image");
    EXPECT_EQ(image::to_string(Error{Error::InvalidLayerCount{}}), "must not already be a layered image");
    EXPECT_EQ(image::to_string(Error{Error::HeightNotDivisibleByLayers{.height = 7, .layer_count = 3}}),
              "can not evenly divide height = 7 by layers = 3");
    EXPECT_EQ(image::to_string(
                  Error{Error::IncompatibleSizes{.old = wgpu::Extent3D{1, 2, 1}, .new_size = wgpu::Extent3D{3, 4, 5}}}),
              "incompatible sizes: old = Extent3d { width: 1, height: 2, depth_or_array_layers: 1 } new = "
              "Extent3d { width: 3, height: 4, depth_or_array_layers: 5 }");
}

// Bevy `FileTextureError` Display: "Error reading image file {path}: {error}."
TEST(FileTextureError, DisplayTextWrapsTheInnerError) {
    const image::FileTextureError error{image::TextureError{image::TextureError::InvalidImageExtension{"xyz"}},
                                        "textures/thing.xyz"};
    EXPECT_EQ(image::to_string(error), "Error reading image file textures/thing.xyz: invalid image extension: xyz.");
}

// Bevy `ImageLoaderError` Display for all three alternatives (image_loader.rs:154-167).
TEST(ImageLoaderError, AlternativesMatchBevyDisplayText) {
    using Error   = image::ImageLoaderError;
    const auto io = Error{Error::Io{std::make_error_code(std::errc::io_error)}};
    EXPECT_EQ(image::to_string(io), std::format("Failed to load image bytes: {}",
                                                std::error_code{std::make_error_code(std::errc::io_error)}.message()));

    const auto file_error = Error{Error::FileTexture{
        image::FileTextureError{image::TextureError{image::TextureError::ImageError{"bad bytes"}}, "a.png"}}};
    EXPECT_EQ(image::to_string(file_error),
              "Could not load texture file: Error reading image file a.png: failed to load an image: bad bytes.");

    const auto layout = Error{
        Error::ArrayLayout{image::TextureReinterpretationError{image::TextureReinterpretationError::WrongDimension{}}}};
    EXPECT_EQ(image::to_string(layout), "Invalid array layout: must be a 2d image");

    EXPECT_NE(io, file_error);
    EXPECT_THROW(std::rethrow_exception(image::to_exception_ptr(file_error)), std::runtime_error);
}

// `Image::load` is the documented Epix filesystem helper: it stays
// decoder-driven and reports the typed failures a caller can act on.
TEST(ImageLoad, ReportsTypedFailuresAndLoadsRealFiles) {
    const auto png = write_png_fixture();
    auto loaded    = image::Image::load(png);
    ASSERT_TRUE(loaded.has_value()) << image::to_string(loaded.error());
    EXPECT_EQ(loaded->width(), 4u);
    EXPECT_EQ(loaded->height(), 2u);
    EXPECT_EQ(loaded->format(), image::Format::RGBA8);

    const auto missing = image::Image::load(fixture_dir() / "does_not_exist.png");
    ASSERT_FALSE(missing.has_value());
    const auto* io_error = std::get_if<image::TextureError::ImageError>(&missing.error().value);
    ASSERT_NE(io_error, nullptr);
    EXPECT_NE(io_error->message.find("does_not_exist.png"), std::string::npos);
    EXPECT_NE(io_error->message.find("No such file or directory"), std::string::npos);

    const auto garbage = write_fixture("garbage.png", "this is definitely not a png");
    const auto failed  = image::Image::load(garbage);
    ASSERT_FALSE(failed.has_value());
    EXPECT_NE(std::get_if<image::TextureError::ImageError>(&failed.error().value), nullptr);
    EXPECT_FALSE(image::to_string(failed.error()).empty());
}

// WebGPU has no three-channel texture format, so Bevy's `Image::from_dynamic`
// widens `Rgb8` to `Rgba8` (and the 16-bit/float equivalents) while building the
// image, and Epix's `Format` has no three-channel alternative either. A decoder
// that returns three channels must therefore be widened before the `Image` is
// created.
TEST(ImageLoad, WidensThreeChannelImagesToFourChannels) {
    // A committed 4x2 colour-type-2 (truecolour RGB) PNG. Epix cannot produce
    // such a file itself, because it has no three-channel image format.
    const auto rgb_path = std::filesystem::path("assets/tests/image/rgb_8.png");
    ASSERT_TRUE(std::filesystem::exists(rgb_path)) << rgb_path.string();

    auto widened = image::Image::load(rgb_path);
    ASSERT_TRUE(widened.has_value()) << image::to_string(widened.error());
    EXPECT_EQ(widened->format(), image::Format::RGBA8);
    EXPECT_EQ(widened->texture_descriptor.format, wgpu::TextureFormat::eRGBA8Unorm);
    EXPECT_EQ(widened->width(), 4u);
    EXPECT_EQ(widened->height(), 2u);
    EXPECT_EQ(widened->raw_view().size(), 4u * 2u * 4u);
    // Every pixel gained an opaque alpha channel and kept its colour channels.
    for (std::uint32_t y = 0; y < 2; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            const auto color = widened->get_color_at(x, y);
            ASSERT_TRUE(color.has_value());
            EXPECT_FLOAT_EQ(color->a, 1.0f) << "at " << x << "," << y;
            EXPECT_NEAR(color->r, static_cast<float>(x) / 3.0f, 0.01f) << "at " << x << "," << y;
            EXPECT_NEAR(color->g, static_cast<float>(y), 0.01f) << "at " << x << "," << y;
        }
    }

    // The Radiance HDR path is the float three-channel case: the repository's
    // 1K equirectangular environment is RGBE, so it decodes as three channels.
    const auto hdr_path = std::filesystem::path("assets/examples/tonemapping/studio_small_01_1k.hdr");
    ASSERT_TRUE(std::filesystem::exists(hdr_path)) << hdr_path.string();
    auto hdr = image::Image::load(hdr_path);
    ASSERT_TRUE(hdr.has_value()) << image::to_string(hdr.error());
    EXPECT_EQ(hdr->format(), image::Format::RGBA32F);
    EXPECT_EQ(hdr->texture_descriptor.format, wgpu::TextureFormat::eRGBA32Float);
    EXPECT_EQ(hdr->width(), 1024u);
    EXPECT_EQ(hdr->height(), 512u);
    EXPECT_EQ(hdr->raw_view().size(), 1024u * 512u * 4u * sizeof(float));
    const auto hdr_color = hdr->get_color_at(0, 0);
    ASSERT_TRUE(hdr_color.has_value());
    EXPECT_FLOAT_EQ(hdr_color->a, 1.0f);

    // The invariant that broke: no loaded image may keep an undefined format.
    for (const auto* loaded : {&*widened, &*hdr}) {
        EXPECT_NE(loaded->texture_descriptor.format, wgpu::TextureFormat::eUndefined);
    }
}

// `Image::save` must not write a corrupt file when the image's format has no
// channel description. With the three-channel formats gone (Bevy has no such
// image format), the only way to reach that state is a descriptor with no pixel
// format at all, which is what this asserts.
TEST(ImageSave, RefusesAFormatWithoutAChannelDescription) {
    auto unknown                      = image::Image::create2d(2, 3, image::Format::RGBA8);
    unknown.texture_descriptor.format = wgpu::TextureFormat::eUndefined;
    EXPECT_EQ(unknown.format(), image::Format::Unknown);
    EXPECT_EQ(unknown.format_info().channels, 0u);

    const auto path = fixture_dir() / "core_loader_unknown_format.png";
    // Hermetic: a previous run may have left the file behind.
    std::filesystem::remove(path);

    EXPECT_FALSE(image::Image::save(path, unknown).has_value());
    EXPECT_FALSE(std::filesystem::exists(path));

    // A four-channel image still saves and reloads.
    const auto rgba = image::Image::create2d(2, 3, image::Format::RGBA8);
    ASSERT_TRUE(image::Image::save(path, rgba).has_value());
    EXPECT_TRUE(std::filesystem::exists(path));
    const auto reloaded = image::Image::load(path);
    ASSERT_TRUE(reloaded.has_value()) << image::to_string(reloaded.error());
    EXPECT_EQ(reloaded->format(), image::Format::RGBA8);
    std::filesystem::remove(path);
}

// Both entry points share one decode path, so a filesystem load and an
// asset-server load agree on format and payload.
TEST(ImageLoad, FilesystemAndAssetServerLoadsAgree) {
    LoaderTestApp fixture;
    const auto rgb_path = std::filesystem::path("assets/tests/image/rgb_8.png");
    auto by_path        = image::Image::load(rgb_path);
    ASSERT_TRUE(by_path.has_value()) << image::to_string(by_path.error());

    BytesReader reader(read_file_bytes(rgb_path));
    auto by_loader = load_through_loader(fixture, reader, "tests/image/rgb_8.png");
    ASSERT_TRUE(by_loader.has_value()) << image::to_string(by_loader.error());

    EXPECT_EQ(by_loader->format(), by_path->format());
    EXPECT_EQ(by_loader->texture_descriptor.format, by_path->texture_descriptor.format);
    EXPECT_EQ(by_loader->width(), by_path->width());
    EXPECT_EQ(by_loader->height(), by_path->height());
    EXPECT_EQ(by_loader->raw_view().size(), by_path->raw_view().size());
}

// Bevy's `ImageLoader` classifies the extension before reading bytes
// (image_loader.rs:181-206 through `ImageType::to_image_format`).
TEST(ImageLoader, ClassifiesExtensionsBeforeDecoding) {
    LoaderTestApp fixture;

    const auto png     = write_png_fixture();
    const auto png_buf = read_file_bytes(png);
    BytesReader valid_reader(png_buf);
    auto loaded = load_through_loader(fixture, valid_reader, "core_loader_fixture.png");
    ASSERT_TRUE(loaded.has_value()) << image::to_string(loaded.error());
    // Bevy's loader widens RGB to RGBA and keeps render-world-only usage.
    EXPECT_EQ(loaded->format(), image::Format::RGBA8);
    EXPECT_EQ(loaded->width(), 4u);
    EXPECT_EQ(loaded->height(), 2u);

    // An unknown extension is `InvalidImageExtension` and never decoded.
    BytesReader unknown_reader(png_buf);
    const auto unknown = load_through_loader(fixture, unknown_reader, "thing.xyz");
    ASSERT_FALSE(unknown.has_value());
    const auto* file_error = std::get_if<image::ImageLoaderError::FileTexture>(&unknown.error().value);
    ASSERT_NE(file_error, nullptr);
    const auto* extension_error =
        std::get_if<image::TextureError::InvalidImageExtension>(&file_error->error.error.value);
    ASSERT_NE(extension_error, nullptr);
    EXPECT_EQ(extension_error->extension, "xyz");
    EXPECT_EQ(file_error->error.path, "thing.xyz");
    EXPECT_EQ(image::to_string(unknown.error()),
              "Could not load texture file: Error reading image file thing.xyz: invalid image extension: xyz.");

    // A container Epix's decoder cannot read keeps Bevy's format name.
    BytesReader ktx2_reader(png_buf);
    const auto ktx2 = load_through_loader(fixture, ktx2_reader, "lut.ktx2");
    ASSERT_FALSE(ktx2.has_value());
    const auto* ktx2_error = std::get_if<image::ImageLoaderError::FileTexture>(&ktx2.error().value);
    ASSERT_NE(ktx2_error, nullptr);
    const auto* format_error =
        std::get_if<image::TextureError::UnsupportedTextureFormat>(&ktx2_error->error.error.value);
    ASSERT_NE(format_error, nullptr);
    EXPECT_EQ(format_error->format, "Ktx2");

    // A format Bevy names but this decoder cannot read keeps Bevy's name even
    // when the loader does not advertise the extension, because Bevy routes
    // `.exr` through `ExrTextureLoader`, which Epix does not have yet.
    BytesReader exr_reader(png_buf);
    const auto exr = load_through_loader(fixture, exr_reader, "scene.exr");
    ASSERT_FALSE(exr.has_value());
    const auto* exr_file_error = std::get_if<image::ImageLoaderError::FileTexture>(&exr.error().value);
    ASSERT_NE(exr_file_error, nullptr);
    const auto* exr_format_error =
        std::get_if<image::TextureError::UnsupportedTextureFormat>(&exr_file_error->error.error.value);
    ASSERT_NE(exr_format_error, nullptr);
    EXPECT_EQ(exr_format_error->format, "OpenExr");

    // A missing extension behaves like an unknown one instead of panicking the
    // way Bevy's `extension().unwrap()` does.
    BytesReader no_extension_reader(png_buf);
    const auto no_extension = load_through_loader(fixture, no_extension_reader, "noextension");
    ASSERT_FALSE(no_extension.has_value());
    const auto* no_extension_file = std::get_if<image::ImageLoaderError::FileTexture>(&no_extension.error().value);
    ASSERT_NE(no_extension_file, nullptr);
    const auto* no_extension_inner =
        std::get_if<image::TextureError::InvalidImageExtension>(&no_extension_file->error.error.value);
    ASSERT_NE(no_extension_inner, nullptr);
    EXPECT_TRUE(no_extension_inner->extension.empty());
}

// Bevy's loader reports a decode failure as
// `ImageLoaderError::FileTexture(FileTextureError { error, path })`.
TEST(ImageLoader, ReportsDecodeFailuresWithThePath) {
    LoaderTestApp fixture;
    const std::array<std::uint8_t, 9> garbage{1, 2, 3, 4, 5, 6, 7, 8, 9};
    BytesReader reader(std::vector<std::uint8_t>(garbage.begin(), garbage.end()));

    const auto result = load_through_loader(fixture, reader, "broken.png");
    ASSERT_FALSE(result.has_value());
    const auto* file_error = std::get_if<image::ImageLoaderError::FileTexture>(&result.error().value);
    ASSERT_NE(file_error, nullptr);
    EXPECT_EQ(file_error->error.path, "broken.png");
    const auto* decode_error = std::get_if<image::TextureError::ImageError>(&file_error->error.error.value);
    ASSERT_NE(decode_error, nullptr);
    EXPECT_NE(image::to_string(result.error()).find("broken.png"), std::string::npos);
}

// A failing `Reader` surfaces as the `Io` alternative that Bevy maps from
// `std::io::Error`.
TEST(ImageLoader, ReportsReaderFailuresAsIo) {
    LoaderTestApp fixture;
    FailingReader reader;

    const auto result = load_through_loader(fixture, reader, "readable.png");
    ASSERT_FALSE(result.has_value());
    const auto* io_error = std::get_if<image::ImageLoaderError::Io>(&result.error().value);
    ASSERT_NE(io_error, nullptr);
    EXPECT_EQ(io_error->code, std::make_error_code(std::errc::io_error));
    EXPECT_NE(image::to_string(result.error()).find("Failed to load image bytes"), std::string::npos);
}

// `ImageLoader` keeps advertising exactly the extensions it can decode, and the
// classification helper accepts every one of them.
TEST(ImageLoader, AdvertisedExtensionsAreClassified) {
    for (const auto extension : image::ImageLoader::extensions()) {
        EXPECT_FALSE(extension.empty());
        BytesReader reader(std::vector<std::uint8_t>{});
        LoaderTestApp fixture;
        assets::LoadContext context(fixture.server(), assets::AssetPath(std::format("thing.{}", extension)));
        auto result = STDEXEC::sync_wait(image::ImageLoader::load(reader, {}, context));
        ASSERT_TRUE(result.has_value());
        const auto error = std::move(std::get<0>(*result));
        // The empty payload fails to decode, but never with a classification
        // error: the extension is accepted before the bytes are read.
        ASSERT_FALSE(error.has_value());
        const auto* file_error = std::get_if<image::ImageLoaderError::FileTexture>(&error.error().value);
        ASSERT_NE(file_error, nullptr) << extension;
        EXPECT_EQ(std::get_if<image::TextureError::InvalidImageExtension>(&file_error->error.error.value), nullptr)
            << extension;
        EXPECT_EQ(std::get_if<image::TextureError::UnsupportedTextureFormat>(&file_error->error.error.value), nullptr)
            << extension;
    }
}
