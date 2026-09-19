#include <epix/sprite_render/tilemap_chunk.hpp>

#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <epix/ecs/world/entity_ref.hpp>
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

epix::sprite_render::TileData epix::sprite_render::TileData::from_tileset_index(
    std::uint16_t tileset_index) noexcept {
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
    // copies everything it needs out of the entity before touching the world
    // again: the insert below moves the entity between archetypes.
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
            spdlog::warn("[tilemap] Invalid tile data length for tilemap chunk {:#x} of size ({}, {}). Expected {}, "
                         "got {}",
                         entity.index, chunk.chunk_size.x, chunk.chunk_size.y, expected_tile_data_length,
                         tile_data.tiles.size());
            return;
        }

        packed_tile_data.reserve(tile_data.tiles.size());
        for (const auto& tile : tile_data.tiles) packed_tile_data.emplace_back(tile);
    }

    const auto tile_data_image = make_chunk_tile_data_image(chunk.chunk_size, packed_tile_data);

    const auto mesh_size = chunk.chunk_size * chunk.tile_display_size;
    // Bevy reads the cache but never fills it, so a chunk of a new size always
    // allocates a mesh here (`tilemap_chunk/mod.rs:162-167`).
    const auto find_cached_mesh = [&world, &mesh_size]() -> std::optional<assets::Handle<mesh::Mesh>> {
        const auto cache = world.get_resource<TilemapChunkMeshCache>();
        if (!cache) return std::nullopt;
        const auto it = cache->get().meshes.find(mesh_size);
        if (it == cache->get().meshes.end()) return std::nullopt;
        return it->second;
    };

    auto mesh = find_cached_mesh();
    if (!mesh) {
        auto meshes = world.get_resource_mut<assets::Assets<mesh::Mesh>>();
        if (!meshes) {
            spdlog::error("[tilemap] Assets<Mesh> not found; the tilemap chunk mesh was not created.");
            return;
        }
        mesh = meshes->get().emplace(mesh::Mesh(mesh::Rectangle::from_size(glm::vec2(mesh_size))));
    }

    auto images = world.get_resource_mut<assets::Assets<image::Image>>();
    if (!images) {
        spdlog::error("[tilemap] Assets<Image> not found; the tile data image was not created.");
        return;
    }
    const auto tile_data = images->get().emplace(tile_data_image);

    auto materials = world.get_resource_mut<assets::Assets<TilemapChunkMaterial>>();
    if (!materials) {
        spdlog::error("[tilemap] Assets<TilemapChunkMaterial> not found; the tilemap chunk material was not created.");
        return;
    }
    const auto material = materials->get().emplace(
        TilemapChunkMaterial{.mode = chunk.alpha_mode, .tileset = chunk.tileset, .tile_data = tile_data});

    // Bevy defers this through `world.commands()` because it holds a
    // `DeferredWorld`; Epix's hooks receive the world itself and the archetype
    // machinery re-reads the entity's location after a hook, so the insert can
    // run directly, as `Parent::on_insert` does.
    world.entity_mut(entity).insert(mesh::Mesh2d{*mesh}, MeshMaterial2d<TilemapChunkMaterial>{material});
}

void epix::sprite_render::update_tilemap_chunk_indices(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, const TilemapChunk&, const TilemapChunkTileData&,
                                     const MeshMaterial2d<TilemapChunkMaterial>&>,
                     epix::ecs::Filter<epix::ecs::Modified<TilemapChunkTileData>>>
        query,
    epix::ecs::ResMut<assets::Assets<TilemapChunkMaterial>> materials,
    epix::ecs::ResMut<assets::Assets<image::Image>> images) {
    // Bevy `update_tilemap_chunk_indices` (`tilemap_chunk/mod.rs:185-239`).
    for (auto&& [chunk_entity, chunk, tile_data, material] : query.iter()) {
        const auto expected_tile_data_length =
            static_cast<std::size_t>(chunk.chunk_size.x) * static_cast<std::size_t>(chunk.chunk_size.y);
        if (tile_data.tiles.size() != expected_tile_data_length) {
            spdlog::warn("[tilemap] Invalid TilemapChunkTileData length for tilemap chunk {:#x} of size ({}, {}). "
                         "Expected {}, got {}",
                         chunk_entity.index, chunk.chunk_size.x, chunk.chunk_size.y, expected_tile_data_length,
                         tile_data.tiles.size());
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
        // Bevy clears and re-extends the payload; the packed data always has the
        // length the image was created with, so writing it in place is the same
        // write (and reaching a different length would be a bug worth reporting).
        auto data        = tile_data_image->get().raw_view_mut();
        const auto bytes = std::as_bytes(std::span{packed_tile_data});
        if (data.size() != bytes.size()) {
            spdlog::warn("[tilemap] TilemapChunkMaterial tile data image is {} bytes but the chunk needs {} for "
                         "tilemap chunk {:#x}",
                         data.size(), bytes.size(), chunk_entity.index);
            continue;
        }
        std::memcpy(data.data(), bytes.data(), bytes.size());
    }
}

void epix::sprite_render::TilemapChunkPlugin::attach(epix::app::App& app) {
    // Bevy `Plugin for TilemapChunkPlugin::build` (`tilemap_chunk/mod.rs:33-38`).
    app.world_mut().init_resource<TilemapChunkMeshCache>();
    app.add_systems(epix::app::Update, epix::ecs::into(update_tilemap_chunk_indices)
                                           .set_name("update tilemap chunk indices"));
}
