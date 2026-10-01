// Visual verification for Bevy's tilemap chunks (S14). Mirrors Bevy's
// `examples/2d/tilemap_chunk.rs`: one 64x64 chunk of 8x8-pixel tiles drawn as a
// single mesh, two markers placed with `TilemapChunk::calculate_tile_transform`
// (one of them moving), and a timer that churns 50 random tiles every 0.1 s so
// the tile-data image rewrite path is visible in the capture.
//
// The tileset is an array texture: four 8x8 tile images stacked vertically in a
// committed 8x32 PNG, loaded with `ImageLoaderSettings::array_layout =
// RowCount{4}` exactly as Bevy's example loads `textures/array_texture.png`.

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <vector>

using namespace epix;
using namespace epix::ecs;

namespace {
constexpr glm::uvec2 kChunkSize{64u, 64u};
constexpr glm::uvec2 kTileDisplaySize{8u, 8u};
/** @brief Tiles in the tileset array texture, i.e. the fixture's rows. */
constexpr std::uint32_t kTileCount = 4;
/** @brief The tileset fixture on disk, relative to the repository root. */
constexpr std::string_view kTilesetFixturePath = "assets/tests/image/tilemap_tiles_8x32.png";
/** @brief The same fixture as the asset server names it (its root is `assets/`). */
constexpr std::string_view kTilesetAssetPath = "tests/image/tilemap_tiles_8x32.png";
constexpr std::uint32_t kTilesetTileSize = 8;

/** @brief The tileset the logging system reports once it has been prepared. */
struct TilesetHandle {
    assets::AssetId<image::Image> id;
};

/** @brief Repeating timer for the tile churn (Bevy's `UpdateTimer`). */
struct UpdateTimer {
    time::Timer timer{time::Timer::from_seconds(0.1f, time::TimerMode::Repeating)};
};

/** @brief A deterministic PRNG; Bevy's example seeds a `ChaCha8Rng` instead. */
struct SeededRng {
    std::uint32_t state = 42u;

    std::uint32_t range(std::uint32_t bound) {
        state = state * 1664525u + 1013904223u;
        return (state >> 8u) % bound;
    }
};

/** @brief Marks the marker that follows the chunk's bottom row. */
struct MovePlayer {};

/** @brief One tile per tileset image, in the order the fragment shader indexes
 *  them. */
constexpr std::array<glm::vec3, kTileCount> kTileColors{{
    {0.85f, 0.25f, 0.25f},
    {0.25f, 0.75f, 0.35f},
    {0.30f, 0.45f, 0.90f},
    {0.90f, 0.80f, 0.30f},
}};

/** @brief Write the committed 8x32 tileset fixture through the image module's
 *  save path, so the example cannot drift from the bytes it loads. Each tile is
 *  a 4x4 checkerboard of its colour, which makes both the tile index and the
 *  nearest sampling visible in the capture. */
bool write_tileset_fixture() {
    auto fixture = image::Image::create2d(kTilesetTileSize, kTilesetTileSize * kTileCount,
                                          wgpu::TextureFormat::eRGBA8UnormSrgb);
    for (std::uint32_t tile = 0; tile < kTileCount; ++tile) {
        for (std::uint32_t y = 0; y < kTilesetTileSize; ++y) {
            for (std::uint32_t x = 0; x < kTilesetTileSize; ++x) {
                const auto bright = ((x / 2u + y / 2u) % 2u) == 0u ? 1.0f : 0.55f;
                (void)fixture.set_color_at(x,
                                           tile * kTilesetTileSize + y,
                                           ::epix::color::Color::linear_rgba(kTileColors[tile].r * bright,
                                                                             kTileColors[tile].g * bright,
                                                                             kTileColors[tile].b * bright,
                                                                             1.0f));
            }
        }
    }
    if (const auto saved = image::Image::save(std::string(kTilesetFixturePath), fixture); !saved.has_value()) {
        spdlog::error("[tilemap_chunk] could not write the tileset fixture to {}", kTilesetFixturePath);
        return false;
    }
    return true;
}

std::vector<std::optional<sprite_render::TileData>> make_tile_data(SeededRng& rng) {
    std::vector<std::optional<sprite_render::TileData>> tiles;
    tiles.reserve(static_cast<std::size_t>(kChunkSize.x) * kChunkSize.y);
    for (std::size_t index = 0; index < static_cast<std::size_t>(kChunkSize.x) * kChunkSize.y; ++index) {
        // Bevy's example draws 0..5 and treats 0 as an empty tile, so three
        // quarters of the chunk carries a tile.
        const auto value = rng.range(5u);
        tiles.push_back(value == 0u ? std::nullopt
                                    : std::optional<sprite_render::TileData>{
                                          sprite_render::TileData::from_tileset_index(
                                              static_cast<std::uint16_t>(value - 1u))});
    }
    return tiles;
}

/** @brief Rewrite 50 random tiles whenever the timer fires (Bevy's
 *  `update_tilemap`). */
void update_tilemap(Res<time::Time<>> time,
                    Query<Item<Mut<sprite_render::TilemapChunkTileData>, Mut<UpdateTimer>>> query,
                    ResMut<SeededRng> rng) {
    for (auto&& [tile_data, timer] : query.iter()) {
        auto& value = timer.get_mut().timer;
        value.tick(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<float>(time->delta_secs())));
        if (!value.just_finished()) continue;

        auto& tiles = tile_data.get_mut().tiles;
        for (std::uint32_t draw = 0; draw < 50u; ++draw) {
            // Bevy's example indexes 0..5 here, which can name a layer its
            // four-tile tileset does not have; this stays inside the array.
            const auto index = rng->range(static_cast<std::uint32_t>(tiles.size()));
            tiles[index]     = sprite_render::TileData::from_tileset_index(
                static_cast<std::uint16_t>(rng->range(kTileCount)));
        }
    }
}

/** @brief Sweep the moving marker between tile (0, 0) and tile (63, 0) (Bevy's
 *  `move_player`), which is what makes `calculate_tile_transform`'s x axis
 *  visible. */
void move_player(Res<time::Time<>> time,
                 Query<Mut<transform::Transform>, With<MovePlayer>> player,
                 Query<const sprite_render::TilemapChunk&> chunks) {
    const auto t = (std::sin(time->elapsed_secs()) + 1.0f) / 2.0f;
    for (auto&& chunk : chunks.iter()) {
        const auto origin = chunk.calculate_tile_transform(glm::uvec2(0u, 0u)).translation.x;
        const auto destination =
            chunk.calculate_tile_transform(glm::uvec2(kChunkSize.x - 1u, 0u)).translation.x;
        for (auto&& transform : player.iter()) {
            transform.get_mut().translation.x = origin + (destination - origin) * t;
        }
    }
}

/** @brief Log the tile at (3, 4) whenever it changes (Bevy's `log_tile`), which
 *  is what gives the log a record of `tile_data_from_tile_pos`. */
void log_tile(Query<Item<const sprite_render::TilemapChunk&, const sprite_render::TilemapChunkTileData&>> tilemaps,
              Local<std::uint16_t> last) {
    for (auto&& [chunk, data] : tilemaps.iter()) {
        const auto tile = data.tile_data_from_tile_pos(chunk.chunk_size, glm::uvec2(3u, 4u));
        if (!tile) return;
        if (tile->get().tileset_index == *last) return;
        *last = tile->get().tileset_index;
        spdlog::info("[tilemap_chunk] tile (3, 4) is now tileset index {}", tile->get().tileset_index);
    }
}

/** @brief Report the loaded tileset's descriptor, so the log records that the
 *  array texture really is a four-layer 8x8 image. */
void log_loaded_tileset(EventReader<assets::AssetEvent<image::Image>> events,
                        Res<assets::Assets<image::Image>> images,
                        Res<TilesetHandle> tileset) {
    for (const auto& event : events.read()) {
        if (event.type != assets::AssetEvent<image::Image>::Type::Added) continue;
        if (event.id != tileset->id) continue;
        const auto image = images->get(event.id);
        if (!image) continue;
        spdlog::info("[tilemap_chunk] loaded tileset {}x{} with {} layers, texture format {}",
                     image->get().width(), image->get().height(), image->get().depth_or_layers(),
                     static_cast<int>(image->get().texture_format()));
    }
}

struct TilemapChunkVisualTestPlugin {
    void attach(app::App& app) {
        app.add_systems(app::Update,
                        ecs::into(update_tilemap, move_player, log_tile)
                            .set_names(std::array{"update tilemap", "move player", "log tile"}));
        app.add_systems(app::Update, ecs::into(log_loaded_tileset).set_name("log loaded tileset"));
    }

    void ready(app::App& app) {
        auto& world = app.world_mut();
        auto& server = world.resource<assets::AssetServer>();
        auto& images = world.resource_mut<assets::Assets<image::Image>>();
        auto& meshes = world.resource_mut<assets::Assets<mesh::Mesh>>();
        auto& materials = world.resource_mut<assets::Assets<sprite_render::ColorMaterial>>();

        if (!write_tileset_fixture()) return;

        // Bevy's example loads the tileset with `array_layout =
        // Some(ImageArrayLayout::RowCount { rows: 4 })`.
        const auto tileset = server.load_with_settings<image::Image, image::ImageLoaderSettings>(
            assets::AssetPath(std::string(kTilesetAssetPath)), [](image::ImageLoaderSettings& settings) {
                settings.array_layout = image::ImageArrayLayout{image::ImageArrayLayout::RowCount{kTileCount}};
            });
        world.insert_resource(TilesetHandle{tileset.id()});

        world.spawn(camera::Camera2d{}, transform::Transform{});

        SeededRng rng;
        const sprite_render::TilemapChunk chunk{.chunk_size        = kChunkSize,
                                                .tile_display_size = kTileDisplaySize,
                                                .tileset           = tileset};
        world.spawn(chunk, sprite_render::TilemapChunkTileData{.tiles = make_tile_data(rng)}, UpdateTimer{});
        world.insert_resource(rng);

        // Two markers: the one at tile (0, 0) moves along the chunk's bottom row
        // and the one at tile (5, 6) stays put, which shows that the same
        // transform puts a tile's centre where the tileset samples it. Bevy's
        // example uses `RED_400`; white stays legible over this fixture's four
        // tile colours.
        const auto marker_material = materials.emplace(sprite_render::ColorMaterial{
            .color = ::epix::color::Color::linear_rgba(1.0f, 1.0f, 1.0f, 1.0f),
            .mode  = sprite_render::AlphaMode2d{sprite_render::AlphaMode2dOpaque{}}});
        const auto marker_mesh = meshes.emplace(mesh::make_box2d(static_cast<float>(kTileDisplaySize.x),
                                                                 static_cast<float>(kTileDisplaySize.y)));

        auto moving = chunk.calculate_tile_transform(glm::uvec2(0u, 0u));
        moving.translation.z = 1.0f;
        world.spawn(mesh::Mesh2d{marker_mesh},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{marker_material}, moving, MovePlayer{});

        auto fixed = chunk.calculate_tile_transform(glm::uvec2(5u, 6u));
        fixed.translation.z = 1.0f;
        world.spawn(mesh::Mesh2d{marker_mesh},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{marker_material}, fixed);

        spdlog::info("[tilemap_chunk] chunk of {}x{} tiles at {}x{} pixels; tileset {}",
                     kChunkSize.x, kChunkSize.y, kTileDisplaySize.x, kTileDisplaySize.y, kTilesetAssetPath);
    }
};
}  // namespace

int main() {
    window::Window primary_window;
    primary_window.title = "Tilemap Chunk | 64x64 tiles in one draw, 4-layer tileset, 2 markers";
    primary_window.size  = {960, 640};

    auto app = app::App::create();
    app.add_plugins(app::TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{.primary_window = primary_window,
                                          .exit_condition = window::ExitCondition::OnPrimaryClosed})
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
        .add_plugins(TilemapChunkVisualTestPlugin{});
    app.run();
}
