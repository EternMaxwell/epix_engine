#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/color.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>
#endif

#include <epix/sprite_render/material2d.hpp>

namespace epix::sprite_render {
/** @brief Plugin that handles the initialization and updating of tilemap chunks
 *  (Bevy `TilemapChunkPlugin`, `tilemap_chunk/mod.rs:29-38`): adds the mesh cache
 *  and the system that keeps a chunk's tile-data image in sync with its tiles. */
EPIX_EXPORT struct EPIX_API TilemapChunkPlugin {
    /** @brief Register the plugin (Bevy `Plugin::build`). */
    void attach(app::App& app);
};

/** @brief Hash for the chunk-size key of `TilemapChunkMeshCache`. Bevy's
 *  `HashMap<UVec2, _>` uses Rust's `Hash for UVec2`; C++ needs an explicit hash
 *  for the vector key. */
EPIX_EXPORT struct EPIX_API TilemapChunkMeshCacheHash {
    std::size_t operator()(const glm::uvec2& key) const noexcept;
};

/** @brief A resource storing the meshes for each tilemap chunk size (Bevy
 *  `TilemapChunkMeshCache`, `mod.rs:40-43`). Bevy's newtype derefs to its map;
 *  Epix names the member instead. */
EPIX_EXPORT struct TilemapChunkMeshCache {
    /** @brief Chunk size in world units to the quad mesh covering it. */
    std::unordered_map<glm::uvec2, assets::Handle<mesh::Mesh>, TilemapChunkMeshCacheHash> meshes;
};

/** @brief Data for a single tile in the tilemap chunk (Bevy `TileData`,
 *  `mod.rs:88-118`). Bevy stores a tagged `Color`; this port stores an
 *  `epix::color::Color`. */
EPIX_EXPORT struct EPIX_API TileData {
    /** @brief The index of the tile in the corresponding tileset array texture. */
    std::uint16_t tileset_index = 0;
    /** @brief The colour tint of the tile. White leaves the sampled texture
     *  colour unchanged. */
    ::epix::color::Color color = ::epix::color::Color::WHITE;
    /** @brief The visibility of the tile. */
    bool visible = true;

    /** @brief Creates a new `TileData` with the given tileset index and default
     *  values (Bevy `TileData::from_tileset_index`). */
    static TileData from_tileset_index(std::uint16_t tileset_index) noexcept;

    bool operator==(const TileData&) const noexcept = default;
};

/** @brief A component representing a chunk of a tilemap (Bevy `TilemapChunk`,
 *  `mod.rs:45-86`). Each chunk is a rectangular section of tiles that is
 *  rendered as a single mesh.
 *
 *  Bevy's component is `#[component(immutable)]`; Epix has no immutable
 *  components, so the struct is only conventionally immutable here. Its
 *  `on_insert` hook is the static member the component registry discovers. */
EPIX_EXPORT struct EPIX_API TilemapChunk {
    /** @brief The size of the chunk in tiles. */
    glm::uvec2 chunk_size{0u};
    /** @brief The size to use for each tile, not to be confused with the size of
     *  a tile in the tileset image. */
    glm::uvec2 tile_display_size{0u};
    /** @brief Handle to the tileset image containing all tile textures. */
    assets::Handle<image::Image> tileset{image::DEFAULT_IMAGE_HANDLE};
    /** @brief The alpha mode to use for the tilemap chunk. */
    AlphaMode2d alpha_mode{AlphaMode2dOpaque{}};

    /** @brief The transform of one tile, in the chunk's own space (Bevy
     *  `TilemapChunk::calculate_tile_transform`, `mod.rs:62-86`).
     *
     *  Tile (0, 0) sits at the chunk's bottom-left corner; x grows to the right
     *  and y upwards, so the caller's `position` is a tile index inside the
     *  chunk, not a world position. */
    transform::Transform calculate_tile_transform(glm::uvec2 position) const noexcept;

    /** @brief Bevy's `on_insert_tilemap_chunk` hook (`mod.rs:126-183`): builds
     *  the chunk's tile-data image and material and attaches the `Mesh2d` and
     *  `MeshMaterial2d<TilemapChunkMaterial>` the chunk renders with. */
    static void on_insert(ecs::World& world, ecs::HookContext ctx);
};

/** @brief Component storing the data of tiles within a chunk (Bevy
 *  `TilemapChunkTileData`, `mod.rs:120-124`). Each index corresponds to a
 *  specific tile in the tileset; `std::nullopt` indicates an empty tile.
 *
 *  Bevy's newtype derefs to its `Vec`; Epix names the member instead. */
EPIX_EXPORT struct EPIX_API TilemapChunkTileData {
    /** @brief Row-major tiles, `chunk_size.x * chunk_size.y` of them, starting
     *  at the chunk's bottom-left tile. */
    std::vector<std::optional<TileData>> tiles;

    /** @brief The tile at `position` inside a chunk of `tilemap_size` tiles, or
     *  nothing when the position is out of range or the tile is empty (Bevy
     *  `TilemapChunkTileData::tile_data_from_tile_pos`, `mod.rs:241-251`). */
    std::optional<std::reference_wrapper<const TileData>> tile_data_from_tile_pos(glm::uvec2 tilemap_size,
                                                                                  glm::uvec2 position) const noexcept;
};

/** @brief Packed per-tile data for the `Rgba16Uint` tile-data texture (Bevy
 *  `PackedTileData`, `tilemap_chunk_material.rs:52-69`).
 *
 *  The member order is the GPU ABI the shader reads: red is the tileset index,
 *  green and blue carry the four tint bytes, alpha carries the flags.
 *  Bevy keeps these fields private; Epix keeps them public so the layout is
 *  inspectable. */
EPIX_EXPORT struct EPIX_API PackedTileData {
    /** @brief The tileset array layer, in the red channel. */
    std::uint16_t tileset_index = 0;
    /** @brief The tint's sRGB bytes, in the green and blue channels. */
    std::array<std::uint8_t, 4> color{0, 0, 0, 0};
    /** @brief The visibility flag, in the alpha channel. */
    std::uint16_t flags = 0;

    /** @brief The "no tile" sentinel: tileset index `u16::MAX`, which the
     *  fragment shader discards (Bevy `PackedTileData::empty`). */
    static PackedTileData empty() noexcept;
    /** @brief The zeroed value (Bevy's `Zeroable`), which is *not* `empty()`. */
    PackedTileData() = default;
    /** @brief Bevy's `From<TileData>`: the tint is converted to sRGB bytes and
     *  `visible` becomes the flag. */
    PackedTileData(const TileData& tile) noexcept;
    /** @brief Bevy's `From<Option<TileData>>`: an empty tile packs as
     *  `empty()`. */
    PackedTileData(const std::optional<TileData>& maybe_tile) noexcept;
};
static_assert(sizeof(PackedTileData) == 8u);

/** @brief The `Rgba16Uint` image holding one texel of `PackedTileData` per tile
 *  (Bevy `make_chunk_tile_data_image`, `tilemap_chunk_material.rs:95-114`). */
EPIX_EXPORT EPIX_API image::Image make_chunk_tile_data_image(glm::uvec2 size, std::span<const PackedTileData> data);

/** @brief Material used for rendering tilemap chunks (Bevy `TilemapChunkMaterial`,
 *  `tilemap_chunk_material.rs:27-50`): a tileset array texture sampled by the
 *  per-tile data texture, drawn in one call per chunk.
 *
 *  Bevy's `alpha_mode` field and its `Material2d::alpha_mode` method cannot share
 *  a name in C++, so the field is `mode` here and `alpha_mode()` stays the
 *  accessor the pipeline calls, as `ColorMaterial` does. */
EPIX_EXPORT struct TilemapChunkMaterial : Material2d<TilemapChunkMaterial> {
    /** @brief How the material's output is composited. */
    AlphaMode2d mode = AlphaMode2d{AlphaMode2dOpaque{}};
    /** @brief The tileset array texture. */
    assets::Handle<image::Image> tileset{image::DEFAULT_IMAGE_HANDLE};
    /** @brief The `Rgba16Uint` per-tile data texture. */
    assets::Handle<image::Image> tile_data{image::DEFAULT_IMAGE_HANDLE};

    /** @brief This material's fragment shader (Bevy returns the embedded
     *  `tilemap_chunk_material.wgsl` path). */
    static std::optional<std::string_view> fragment_shader() {
        return std::string_view{"embedded://sprite/tilemap_chunk_material.slang"};
    }
    /** @brief This material's alpha mode (Bevy `Material2d::alpha_mode`). */
    AlphaMode2d alpha_mode() const { return mode; }
};

/** @brief Plugin that adds support for tilemap chunk materials (Bevy
 *  `TilemapChunkMaterialPlugin`, `tilemap_chunk_material.rs:12-21`). */
EPIX_EXPORT struct EPIX_API TilemapChunkMaterialPlugin {
    /** @brief Register the plugin (Bevy `Plugin::build`). */
    void attach(app::App& app);
};

/** @brief Update the tile data of every chunk whose tiles changed (Bevy
 *  `update_tilemap_chunk_indices`, `mod.rs:185-239`).
 *
 *  Bevy takes `images.get_mut(&material.tile_data)` to trigger the asset change
 *  event that makes the render world re-upload the texture; the tuple copy below
 *  is the same write, because a chunk's tile data always has exactly the length
 *  its image was created with. */
EPIX_EXPORT EPIX_API void update_tilemap_chunk_indices(
    ecs::Query<ecs::Item<ecs::Entity,
                         const TilemapChunk&,
                         const TilemapChunkTileData&,
                         const MeshMaterial2d<TilemapChunkMaterial>&>,
               ecs::Modified<TilemapChunkTileData>> query,
    ecs::ResMut<assets::Assets<TilemapChunkMaterial>> materials,
    ecs::ResMut<assets::Assets<image::Image>> images);

}  // namespace epix::sprite_render

namespace epix::render::render_resource {
/** @brief `TilemapChunkMaterial`'s bind group: a tileset array texture at 0, its
 *  sampler at 1 and the `Rgba16Uint` tile-data texture at 2, as Bevy's derive
 *  declares (`tilemap_chunk_material.rs:31-36`). The material has no uniform, so
 *  its `Data` is the unit type Bevy's derive defaults to. */
template <>
struct EPIX_API AsBindGroup<epix::sprite_render::TilemapChunkMaterial> {
    using Data = std::monostate;
    /** @brief The prepared images both textures are resolved from. */
    using Param = std::tuple<epix::ecs::Res<RenderAssets<texture::GpuImage>>>;

    static constexpr std::string_view label() { return "tilemap_chunk_material"; }

    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() {
        const auto entries =
            BindGroupLayoutEntries<>::sequential(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment,
                                                 binding_types::texture_2d_array(wgpu::TextureSampleType::eFloat),
                                                 binding_types::sampler(wgpu::SamplerBindingType::eFiltering),
                                                 binding_types::texture_2d(wgpu::TextureSampleType::eUint));
        return {entries.begin(), entries.end()};
    }

    static Data bind_group_data(const epix::sprite_render::TilemapChunkMaterial&) { return {}; }

    /** @brief Resolve both declared textures from their prepared GPU images. A
     *  texture that is not prepared yet retries the next update, as Bevy's
     *  derive does (`as_bind_group.rs:568`). */
    static std::expected<UnpreparedBindGroup, AsBindGroupError> unprepared_bind_group(
        const wgpu::Device& device,
        const wgpu::BindGroupLayout& layout,
        const epix::sprite_render::TilemapChunkMaterial& material,
        const Param& param);
};
}  // namespace epix::render::render_resource
