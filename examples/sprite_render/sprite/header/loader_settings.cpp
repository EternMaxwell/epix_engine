// Visual verification for Bevy's `ImageLoaderSettings` surface (image tracker
// G22). One 8x8 sRGB palette PNG is decoded twice with different settings and
// each result is drawn as its own sprite, so a single rendered frame proves the
// settings reach the produced image and its GPU texture:
//
//   left  - `ImageLoaderSettings::default()`, i.e. `is_srgb: true`: the 8-bit
//           data is uploaded as `Rgba8UnormSrgb` and the GPU linearises on sample.
//   right - `is_srgb: false`: the same bytes are uploaded as `Rgba8Unorm`, so the
//           sRGB-encoded values are treated as already linear and every tile
//           samples brighter than its left-hand twin.
//
// A third configuration, `array_layout: RowCount{8}`, is decoded too; its result
// is a 2D array, which the 2D sprite pipeline cannot bind, so its descriptor is
// reported through the log rather than drawn.
//
// Both configurations are listed in the window title and printed as a descriptor
// line, so the log and the pixels are two independent records of the behaviour.

#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <expected>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexec/execution.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

using namespace epix;

namespace {

constexpr glm::uvec2 kWindowSize{1000, 420};
/** @brief The fixture lives under the asset directory so it is also a committed
 * repository asset that other loader checks can reuse. */
constexpr std::string_view kFixturePath = "assets/tests/image/srgb_palette_8x8.png";
/** @brief Drawn size of one 8x8 palette tile, in pixels. */
constexpr float kTileSize = 240.0f;
/** @brief Horizontal distance between the two tiles. */
constexpr float kTileOffset = 260.0f;

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

/** @brief Read the fixture back through `ImageLoader`'s settings surface, or
 * report the typed failure and return nothing.
 *
 * The loader — not `Image::from_buffer` — is the entry point that applies every
 * setting, because `array_layout` is reported through the typed
 * `ImageLoaderError::ArrayLayout` and therefore lives on the loader's error
 * surface. This calls it exactly the way the asset server does. */
std::optional<image::Image> decode_with(const assets::AssetServer& server,
                                       const std::vector<std::uint8_t>& bytes,
                                       std::string_view name,
                                       const image::ImageLoaderSettings& settings) {
    struct MemoryReader : assets::Reader {
        std::vector<std::uint8_t> payload;
        explicit MemoryReader(std::vector<std::uint8_t> value) : payload(std::move(value)) {}
        STDEXEC::task<std::expected<std::size_t, std::error_code>> read_to_end(
            std::vector<std::uint8_t>& buffer) override {
            buffer = payload;
            co_return payload.size();
        }
    };

    MemoryReader reader(bytes);
    assets::LoadContext context(server, assets::AssetPath(std::string(kFixturePath)));
    auto result = STDEXEC::sync_wait(image::ImageLoader::load(reader, settings, context));
    if (!result.has_value()) {
        spdlog::error("[loader_settings] {} did not complete", name);
        return std::nullopt;
    }
    auto decoded = std::move(std::get<0>(*result));
    if (!decoded) {
        spdlog::error("[loader_settings] {} failed: {}", name, image::to_string(decoded.error()));
        return std::nullopt;
    }
    // The palette's column 1 (pure green) is the sample this example reports, so
    // the log contains the decorative `is_srgb` difference as numbers instead of
    // only as pixels.
    const auto sample = decoded->get_color_at(1, 0);
    spdlog::info("[loader_settings] {}: {}x{} texture format {} dimension {} depth_or_layers {}; pixel(1,0) = "
                 "({:.3f}, {:.3f}, {:.3f}, {:.3f})",
                 name, decoded->width(), decoded->height(), static_cast<int>(decoded->texture_descriptor.format),
                 static_cast<int>(decoded->type()), decoded->depth_or_layers(), sample ? sample->r : -1.0f,
                 sample ? sample->g : -1.0f, sample ? sample->b : -1.0f, sample ? sample->a : -1.0f);
    return std::move(*decoded);
}

struct LoaderSettingsVisualTestPlugin {
    void ready(app::App& app) {
        auto& world        = app.world_mut();
        auto& images       = world.resource_mut<assets::Assets<image::Image>>();
        const auto& server = world.resource<assets::AssetServer>();

        world.spawn(camera::Camera2d{}, transform::Transform{});

        // The fixture is written through the module's own save path, so the
        // example cannot drift from what the loader is expected to read and needs
        // no separately generated binary. It is committed as well, so a reader can
        // see the exact input; rewriting it keeps a stale copy from masking a
        // regression in `Image::save`.
        auto fixture = image::Image::create2d(8, 8, wgpu::TextureFormat::eRGBA8Unorm);
        constexpr std::array<std::array<std::uint8_t, 3>, 8> palette{{
            {255, 32, 32},
            {32, 255, 32},
            {32, 32, 255},
            {255, 255, 32},
            {255, 32, 255},
            {32, 255, 255},
            {240, 240, 240},
            {16, 16, 16},
        }};
        for (std::uint32_t y = 0; y < 8; ++y) {
            for (std::uint32_t x = 0; x < 8; ++x) {
                const auto& rgb = palette[x];
                (void)fixture.set_color_at(x, y,
                                           glm::vec4(static_cast<float>(rgb[0]) / 255.0f,
                                                     static_cast<float>(rgb[1]) / 255.0f,
                                                     static_cast<float>(rgb[2]) / 255.0f, 1.0f));
            }
        }
        if (!image::Image::save(std::string(kFixturePath), fixture).has_value()) {
            spdlog::error("[loader_settings] could not write the fixture to {}", kFixturePath);
            return;
        }

        const auto bytes = read_file(std::string(kFixturePath));
        if (bytes.empty()) {
            spdlog::error("[loader_settings] the fixture at {} is empty", kFixturePath);
            return;
        }

        // `ImageLoaderSettings::default()`: Bevy's default is `is_srgb: true`.
        if (auto srgb = decode_with(server, bytes, "default (is_srgb = true)", image::ImageLoaderSettings{})) {
            const auto handle  = images.emplace(std::move(*srgb));
            auto sprite        = sprite::Sprite::from_image(handle);
            sprite.custom_size = glm::vec2(kTileSize);
            world.spawn(std::move(sprite), transform::Transform::from_xyz(-kTileOffset, 0.0f, 0.0f));
        }

        // The same bytes, sampled as linear: the sRGB-encoded values are treated
        // as already linear, so this tile is visibly brighter.
        if (auto linear =
                decode_with(server, bytes, "is_srgb = false", image::ImageLoaderSettings{.is_srgb = false})) {
            const auto handle  = images.emplace(std::move(*linear));
            auto sprite        = sprite::Sprite::from_image(handle);
            sprite.custom_size = glm::vec2(kTileSize);
            world.spawn(std::move(sprite), transform::Transform::from_xyz(kTileOffset, 0.0f, 0.0f));
        }

        // `array_layout` reinterprets the 8x8 image as eight 8x1 layers. The 2D
        // sprite pipeline binds a plain 2D texture, so this case is reported from
        // its descriptor instead of being drawn.
        (void)decode_with(server, bytes, "array_layout = RowCount(8)",
                          image::ImageLoaderSettings{
                              .array_layout = image::ImageArrayLayout{image::ImageArrayLayout::RowCount{8}}});
        (void)decode_with(server, bytes, "texture_format = BGRA8Unorm, is_srgb = false",
                          image::ImageLoaderSettings{.texture_format = wgpu::TextureFormat::eBGRA8Unorm,
                                                     .is_srgb        = false});
    }
};

}  // namespace

int main() {
    window::Window primary_window;
    primary_window.title = "Image Loader Settings | left: Rgba8UnormSrgb (is_srgb = true) | right: Rgba8Unorm (is_srgb = false)";
    primary_window.size  = {static_cast<int>(kWindowSize.x), static_cast<int>(kWindowSize.y)};

    auto app = app::App::create();
    app.add_plugins(app::TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{
            .primary_window = primary_window,
            .exit_condition = window::ExitCondition::OnPrimaryClosed,
        })
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(sdl3::SDL3Plugin{})
        .add_plugins(sdl3::SDL3RenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin::default_nearest())
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(sprite_render::SpriteRenderPlugin{})
        .add_plugins(LoaderSettingsVisualTestPlugin{});
    app.run();
}
