#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/image.hpp>
#include <format>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace image  = epix::image;
namespace assets = epix::assets;

namespace {

/** @brief The fixture's `image`: a deterministic payload whose byte `i` is `offset + i`. */
image::Image make_image(
    std::uint32_t width, std::uint32_t height, wgpu::TextureFormat format, std::uint8_t offset) {
    auto result = image::Image::create2d(width, height, format);
    auto bytes  = result.raw_view_mut();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(static_cast<std::uint8_t>(offset + index));
    }
    return result;
}

/** @brief The fixture's `uninitialized`: an image whose payload was already transferred. */
image::Image uninitialized(std::uint32_t width, std::uint32_t height, wgpu::TextureFormat format) {
    auto result = make_image(width, height, format, 0);
    result.take_data();
    return result;
}

/** @brief A stable digest of the atlas payload: the wrapping byte sum, then a few sampled
 * bytes, exactly as the fixture computes it. */
std::string digest(const image::Image& atlas) {
    const auto data   = atlas.raw_view();
    std::uint32_t sum = 0;
    for (const auto byte : data) {
        sum += static_cast<std::uint8_t>(byte);
    }

    const std::array<std::size_t, 8> samples{0, 1, 2, 3, 4, 8, 16, data.size() - 1};
    std::string result = std::format("{}", sum);
    for (const auto index : samples) {
        result += std::format(" {}", static_cast<std::uint8_t>(data[index]));
    }
    return result;
}

/** @brief The fixture's `report`: the builder's result in the exact shape the Rust dump uses. */
std::string report(const std::string& name,
                   image::TextureAtlasBuilder& builder,
                   const std::vector<std::pair<std::string, assets::AssetId<image::Image>>>& ids = {}) {
    std::string output = "scenario " + name + "\n";

    auto built = builder.build();
    if (!built) {
        return output + "  error " + built.error().to_string() + "\n";
    }

    const auto& [layout, sources, atlas] = *built;
    output += std::format("  size {} {}\n", layout.size.x, layout.size.y);
    output += std::format("  rects {}\n", layout.textures.size());
    for (std::size_t index = 0; index < layout.textures.size(); ++index) {
        const auto& rect = layout.textures[index];
        output += std::format("  rect {} {} {} {} {}\n", index, rect.min.x, rect.min.y, rect.max.x, rect.max.y);
    }
    for (const auto& entry : ids) {
        const auto resolved = sources.texture_index(entry.second);
        output +=
            resolved ? std::format("  id {} {}\n", entry.first, *resolved) : std::format("  id {} none\n", entry.first);
    }
    output += std::format("  atlas {} {} {}\n", atlas.width(), atlas.height(), atlas.raw_view().size());
    output += "  digest " + digest(atlas) + "\n";
    return output;
}

assets::AssetId<image::Image> make_id(std::uint8_t tag) {
    auto uuid = uuids::uuid::from_string(std::format("00000000-0000-0000-0000-0000000000{:02x}", tag));
    return assets::AssetId<image::Image>(uuid.value());
}

}  // namespace

// Every scenario below is a recorded dump of the real `bevy_image` 0.18.0 builder (the
// `tmp/bevy-image-fixture` Rust project), so the port has to agree with Bevy on the atlas size the
// doubling loop settles on, the registered rectangles (including padding subtraction), the
// source-id map, the typed errors, and the copied atlas bytes.
TEST(TextureAtlasBuilder, MatchesBevyImage018Results) {
    std::string dump;

    {
        const auto a = make_image(2, 2, wgpu::TextureFormat::eRGBA8UnormSrgb, 1);
        const auto b = make_image(2, 2, wgpu::TextureFormat::eRGBA8UnormSrgb, 100);
        image::TextureAtlasBuilder builder;
        builder.initial_size(glm::uvec2(4, 4)).add_texture(std::nullopt, a).add_texture(std::nullopt, b);
        dump += report("pixel-2x2", builder);
    }

    {
        const auto a = make_image(2, 2, wgpu::TextureFormat::eRGBA8UnormSrgb, 7);
        const auto b = make_image(2, 2, wgpu::TextureFormat::eRGBA8UnormSrgb, 40);
        image::TextureAtlasBuilder builder;
        builder.initial_size(glm::uvec2(8, 8))
            .padding(glm::uvec2(1, 1))
            .add_texture(std::nullopt, a)
            .add_texture(std::nullopt, b);
        dump += report("padding-1", builder);
    }

    {
        const auto a = make_image(3, 3, wgpu::TextureFormat::eRGBA8UnormSrgb, 5);
        image::TextureAtlasBuilder builder;
        builder.add_texture(std::nullopt, a);
        dump += report("default-size", builder);
    }

    {
        std::vector<image::Image> textures;
        for (std::uint32_t index = 0; index < 5; ++index) {
            textures.push_back(make_image(8, 8, wgpu::TextureFormat::eRGBA8UnormSrgb,
                                          static_cast<std::uint8_t>(index * 10)));
        }
        image::TextureAtlasBuilder builder;
        builder.initial_size(glm::uvec2(16, 16));
        for (const auto& texture : textures) {
            builder.add_texture(std::nullopt, texture);
        }
        dump += report("grow-16-to-32", builder);
    }

    {
        std::vector<image::Image> textures;
        for (std::uint32_t index = 0; index < 5; ++index) {
            textures.push_back(make_image(8, 8, wgpu::TextureFormat::eRGBA8UnormSrgb,
                                          static_cast<std::uint8_t>(index * 10)));
        }
        image::TextureAtlasBuilder builder;
        builder.initial_size(glm::uvec2(16, 16)).max_size(glm::uvec2(16, 16));
        for (const auto& texture : textures) {
            builder.add_texture(std::nullopt, texture);
        }
        dump += report("max-size-limited", builder);
    }

    {
        const auto a = make_image(64, 64, wgpu::TextureFormat::eRGBA8UnormSrgb, 3);
        image::TextureAtlasBuilder builder;
        builder.initial_size(glm::uvec2(16, 16)).max_size(glm::uvec2(32, 32)).add_texture(std::nullopt, a);
        dump += report("too-large", builder);
    }

    {
        image::TextureAtlasBuilder builder;
        dump += report("no-textures", builder);
    }

    {
        const auto a      = make_image(8, 8, wgpu::TextureFormat::eRGBA8UnormSrgb, 11);
        const auto b      = make_image(8, 8, wgpu::TextureFormat::eRGBA8UnormSrgb, 22);
        const auto c      = make_image(8, 8, wgpu::TextureFormat::eRGBA8UnormSrgb, 33);
        const auto first  = make_id(1);
        const auto second = make_id(2);
        image::TextureAtlasBuilder builder;
        builder.initial_size(glm::uvec2(16, 16))
            .add_texture(first, a)
            .add_texture(std::nullopt, b)
            .add_texture(second, c);
        dump += report("source-ids", builder, {{"first", first}, {"second", second}});
    }

    {
        const auto a = make_image(2, 2, wgpu::TextureFormat::eR8Unorm, 9);
        image::TextureAtlasBuilder builder;
        builder.initial_size(glm::uvec2(4, 4)).add_texture(std::nullopt, a);
        dump += report("convert-r8", builder);
    }

    {
        const auto a = make_image(4, 4, wgpu::TextureFormat::eR8Unorm, 9);
        image::TextureAtlasBuilder builder;
        builder.initial_size(glm::uvec2(8, 8)).auto_format_conversion(false).add_texture(std::nullopt, a);
        dump += report("wrong-format", builder);
    }

    {
        const auto a = uninitialized(4, 4, wgpu::TextureFormat::eRGBA8UnormSrgb);
        image::TextureAtlasBuilder builder;
        builder.initial_size(glm::uvec2(8, 8)).add_texture(std::nullopt, a);
        dump += report("uninitialized-source", builder);
    }

    {
        const auto a = uninitialized(4, 4, wgpu::TextureFormat::eR8Unorm);
        image::TextureAtlasBuilder builder;
        builder.initial_size(glm::uvec2(8, 8)).add_texture(std::nullopt, a);
        dump += report("uninitialized-source-converted", builder);
    }

    const std::string expected =
        "scenario pixel-2x2\n"
        "  size 4 4\n"
        "  rects 2\n"
        "  rect 0 0 0 2 2\n"
        "  rect 1 2 0 4 2\n"
        "  atlas 4 4 64\n"
        "  digest 1856 1 2 3 4 5 100 9 0\n"
        "scenario padding-1\n"
        "  size 8 8\n"
        "  rects 2\n"
        "  rect 0 0 0 2 2\n"
        "  rect 1 3 0 5 2\n"
        "  atlas 8 8 256\n"
        "  digest 992 7 8 9 10 11 0 44 0\n"
        "scenario default-size\n"
        "  size 256 256\n"
        "  rects 1\n"
        "  rect 0 0 0 3 3\n"
        "  atlas 256 256 262144\n"
        "  digest 810 5 6 7 8 9 13 0 0\n"
        "scenario grow-16-to-32\n"
        "  size 32 32\n"
        "  rects 5\n"
        "  rect 0 0 0 8 8\n"
        "  rect 1 8 0 16 8\n"
        "  rect 2 16 0 24 8\n"
        "  rect 3 24 0 32 8\n"
        "  rect 4 0 8 8 16\n"
        "  atlas 32 32 4096\n"
        "  digest 163200 0 1 2 3 4 8 16 0\n"
        "scenario max-size-limited\n"
        "  error could not pack textures into an atlas within the given bounds\n"
        "scenario too-large\n"
        "  error could not pack textures into an atlas within the given bounds\n"
        "scenario no-textures\n"
        "  size 256 256\n"
        "  rects 0\n"
        "  atlas 256 256 262144\n"
        "  digest 0 0 0 0 0 0 0 0 0\n"
        "scenario source-ids\n"
        "  size 16 16\n"
        "  rects 3\n"
        "  rect 0 0 0 8 8\n"
        "  rect 1 8 0 16 8\n"
        "  rect 2 0 8 8 16\n"
        "  id first 0\n"
        "  id second 2\n"
        "  atlas 16 16 1024\n"
        "  digest 97920 11 12 13 14 15 19 27 0\n"
        "scenario convert-r8\n"
        "  size 4 4\n"
        "  rects 1\n"
        "  rect 0 0 0 2 2\n"
        "  atlas 4 4 64\n"
        "  digest 1146 9 9 9 255 10 0 11 0\n"
        "scenario wrong-format\n"
        "  error added a texture with the wrong format in an atlas\n"
        "scenario uninitialized-source\n"
        "  error cannot add uninitialized texture to atlas\n"
        "scenario uninitialized-source-converted\n"
        "  size 8 8\n"
        "  rects 1\n"
        "  rect 0 0 0 4 4\n"
        "  atlas 8 8 256\n"
        "  digest 0 0 0 0 0 0 0 0 0\n";

    EXPECT_EQ(dump, expected);
}

TEST(TextureAtlasBuilderError, DisplayMatchesBevy) {
    EXPECT_EQ((image::TextureAtlasBuilderError{image::texture_atlas_builder_error::NotEnoughSpace{}}).to_string(),
              "could not pack textures into an atlas within the given bounds");
    EXPECT_EQ((image::TextureAtlasBuilderError{image::texture_atlas_builder_error::WrongFormat{}}).to_string(),
              "added a texture with the wrong format in an atlas");
    EXPECT_EQ((image::TextureAtlasBuilderError{image::texture_atlas_builder_error::UninitializedAtlas{}}).to_string(),
              "cannot add texture to uninitialized atlas texture");
    EXPECT_EQ(
        (image::TextureAtlasBuilderError{image::texture_atlas_builder_error::UninitializedSourceTexture{}}).to_string(),
        "cannot add uninitialized texture to atlas");
    EXPECT_EQ((image::TextureAtlasBuilderError{image::TextureAccessError{
                   image::TextureAccessError::UnsupportedTextureFormat{wgpu::TextureFormat::eUndefined}}})
                  .to_string(),
              "texture access error: unsupported texture format: Undefined");
}

TEST(TextureAtlasBuilder, ReportsUnsupportedAtlasFormat) {
    const auto a = make_image(2, 2, wgpu::TextureFormat::eRGBA8UnormSrgb, 1);

    image::TextureAtlasBuilder builder;
    builder.initial_size(glm::uvec2(4, 4)).format(wgpu::TextureFormat::eBC1RGBAUnorm).add_texture(std::nullopt, a);

    const auto built = builder.build();
    ASSERT_FALSE(built.has_value());
    EXPECT_EQ(built.error(),
              (image::TextureAtlasBuilderError{image::TextureAccessError{
                  image::TextureAccessError::UnsupportedTextureFormat{wgpu::TextureFormat::eBC1RGBAUnorm}}}));
}
