#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdint>
#include <epix/ecs/world/entity_ref.hpp>
#include <epix/sprite_render/tilemap_chunk.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <utility>
#include <vector>

std::size_t epix::sprite_render::TilemapChunkMeshCacheHash::operator()(const glm::uvec2& key) const noexcept {
    std::size_t seed = std::hash<std::uint32_t>{}(key.x);
    seed ^= std::hash<std::uint32_t>{}(key.y) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    return seed;
}

epix::sprite_render::TileData epix::sprite_render::TileData::from_tileset_index(std::uint16_t tileset_index) noexcept {
    TileData data;
    data.tileset_index = tileset_index;
    return data;
}

epix::transform::Transform epix::sprite_render::TilemapChunk::calculate_tile_transform(
    glm::uvec2 position) const noexcept {
    const auto size    = glm::vec2(chunk_size);
    const auto display = glm::vec2(tile_display_size);
    const auto tile    = glm::vec2(position);
    // Tile position times the display size, plus half a tile to correct the
    // centre, minus half the chunk so tile 0 starts at the chunk's left edge.
    return epix::transform::Transform::from_xyz(tile.x * display.x + display.x / 2.0f - display.x * size.x / 2.0f,
                                                tile.y * display.y + display.y / 2.0f - display.y * size.y / 2.0f,
                                                0.0f);
}

std::optional<std::reference_wrapper<const epix::sprite_render::TileData>>
epix::sprite_render::TilemapChunkTileData::tile_data_from_tile_pos(glm::uvec2 tilemap_size,
                                                                   glm::uvec2 position) const noexcept {
    const auto index = static_cast<std::size_t>(tilemap_size.x) * position.y + position.x;
    if (index >= tiles.size()) return std::nullopt;
    const auto& tile = tiles[index];
    if (!tile) return std::nullopt;
    return std::cref(*tile);
}

void epix::sprite_render::TilemapChunk::on_insert(epix::ecs::World& world, epix::ecs::HookContext ctx) {
    // Bevy `on_insert_tilemap_chunk` (`tilemap_chunk/mod.rs:126-183`). The hook
    // copies the chunk values before accessing resources, then defers the
    // generated bundle until the enclosing insertion's hooks have finished.
    const auto entity = ctx.entity;

    const auto maybe_chunk = world.entity_mut(entity).get<TilemapChunk>();
    if (!maybe_chunk) {
        spdlog::warn("[tilemap] TilemapChunk not found for tilemap chunk {:#x}", entity.index);
        return;
    }
    const TilemapChunk chunk = maybe_chunk->get();

    std::vector<PackedTileData> packed_tile_data;
    {
        const auto maybe_tile_data = world.entity_mut(entity).get<TilemapChunkTileData>();
        if (!maybe_tile_data) {
            // Bevy's own message names `TilemapChunkIndices`, which is that of an
            // earlier revision; the text is kept so a log line matches upstream.
            spdlog::warn("[tilemap] TilemapChunkIndices not found for tilemap chunk {:#x}", entity.index);
            return;
        }
        const auto& tile_data = maybe_tile_data->get();

        const auto expected_tile_data_length =
            static_cast<std::size_t>(chunk.chunk_size.x) * static_cast<std::size_t>(chunk.chunk_size.y);
        if (tile_data.tiles.size() != expected_tile_data_length) {
            spdlog::warn(
                "[tilemap] Invalid tile data length for tilemap chunk {:#x} of size ({}, {}). Expected {}, "
                "got {}",
                entity.index, chunk.chunk_size.x, chunk.chunk_size.y, expected_tile_data_length,
                tile_data.tiles.size());
            return;
        }

        packed_tile_data.reserve(tile_data.tiles.size());
        for (const auto& tile : tile_data.tiles) packed_tile_data.emplace_back(tile);
    }

    auto tile_data_image = make_chunk_tile_data_image(chunk.chunk_size, packed_tile_data);

    const auto& cache = world.resource<TilemapChunkMeshCache>();
    const auto mesh_size = chunk.chunk_size * chunk.tile_display_size;
    // Bevy reads the cache but never fills it, so a chunk of a new size always
    // allocates a mesh here (`tilemap_chunk/mod.rs:162-167`).
    const auto mesh_handle = [&]() -> assets::Handle<mesh::Mesh> {
        const auto it = cache.meshes.find(mesh_size);
        if (it != cache.meshes.end()) return it->second;
        return world.resource_mut<assets::Assets<mesh::Mesh>>().emplace(
            mesh::Mesh(mesh::Rectangle::from_size(glm::vec2(mesh_size))));
    }();
    const auto tile_data = world.resource_mut<assets::Assets<image::Image>>().emplace(std::move(tile_data_image));
    const auto material = world.resource_mut<assets::Assets<TilemapChunkMaterial>>().emplace(
        TilemapChunkMaterial{.mode = chunk.alpha_mode, .tileset = chunk.tileset, .tile_data = tile_data});

    // Preserve command/hook order and keep structural changes out of the active
    // bundle insertion. EntityCommands owns both handles until application.
    ecs::Commands commands{ecs::Deferred{world.command_queue()}, world.entities()};
    commands.entity(entity).insert(mesh::Mesh2d{mesh_handle}, MeshMaterial2d<TilemapChunkMaterial>{material});
}

void epix::sprite_render::update_tilemap_chunk_indices(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                     const TilemapChunk&,
                                     const TilemapChunkTileData&,
                                     const MeshMaterial2d<TilemapChunkMaterial>&>,
                     epix::ecs::Modified<TilemapChunkTileData>> query,
    epix::ecs::ResMut<assets::Assets<TilemapChunkMaterial>> materials,
    epix::ecs::ResMut<assets::Assets<image::Image>> images) {
    // Bevy `update_tilemap_chunk_indices` (`tilemap_chunk/mod.rs:185-239`).
    for (auto&& [chunk_entity, chunk, tile_data, material] : query.iter()) {
        const auto expected_tile_data_length =
            static_cast<std::size_t>(chunk.chunk_size.x) * static_cast<std::size_t>(chunk.chunk_size.y);
        if (tile_data.tiles.size() != expected_tile_data_length) {
            spdlog::warn(
                "[tilemap] Invalid TilemapChunkTileData length for tilemap chunk {:#x} of size ({}, {}). "
                "Expected {}, got {}",
                chunk_entity.index, chunk.chunk_size.x, chunk.chunk_size.y, tile_data.tiles.size(),
                expected_tile_data_length);
            continue;
        }

        std::vector<PackedTileData> packed_tile_data;
        packed_tile_data.reserve(tile_data.tiles.size());
        for (const auto& tile : tile_data.tiles) packed_tile_data.emplace_back(tile);

        // Getting the material mutably to trigger change detection, as Bevy does.
        auto material_asset = materials->get_mut(material.id());
        if (!material_asset) {
            spdlog::warn("[tilemap] TilemapChunkMaterial not found for tilemap chunk {:#x}", chunk_entity.index);
            continue;
        }
        auto tile_data_image = images->get_mut(material_asset->get().tile_data.id());
        if (!tile_data_image) {
            spdlog::warn("[tilemap] TilemapChunkMaterial tile data image not found for tilemap chunk {:#x}",
                         chunk_entity.index);
            continue;
        }
        auto* data = tile_data_image->get().data_mut();
        if (!data) {
            spdlog::warn(
                "[tilemap] TilemapChunkMaterial tile data image data not found for tilemap chunk {:#x}",
                chunk_entity.index);
            continue;
        }
        // CPU bytes are independent of the texture descriptor. Match Bevy's
        // clear/extend for any present payload, including an empty vector.
        const auto bytes = std::as_bytes(std::span{packed_tile_data});
        data->clear();
        data->insert(data->end(), bytes.begin(), bytes.end());
    }
}

void epix::sprite_render::TilemapChunkPlugin::attach(epix::app::App& app) {
    // Bevy `Plugin for TilemapChunkPlugin::build` (`tilemap_chunk/mod.rs:33-38`).
    app.world_mut().init_resource<TilemapChunkMeshCache>();
    app.add_systems(epix::app::Update,
                    epix::ecs::into(update_tilemap_chunk_indices).set_name("update tilemap chunk indices"));
}
