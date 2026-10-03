#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/sprite_render.hpp>
#include <epix/task.hpp>
#include <epix/transform.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace epix::app;
using namespace epix::assets;
using namespace epix::shader;

namespace sprite_render = epix::sprite_render;
namespace render        = epix::render;
namespace image         = epix::image;
namespace mesh          = epix::mesh;
namespace ecs           = epix::ecs;

namespace {
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} g_io_task_pool_init;

App make_shader_asset_app() {
    App app = App::create();

    AssetPlugin asset_plugin;
    asset_plugin.mode = AssetServerMode::Unprocessed;
    app.add_plugins(asset_plugin);

    ShaderPlugin shader_plugin;
    app.add_plugins(shader_plugin);

    return app;
}

/** @brief The plugin set a tilemap chunk needs: the asset and mesh stores plus
 *  the two tilemap plugins. */
App make_tilemap_app() {
    auto app = make_shader_asset_app();
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(mesh::MeshPlugin{});
    app.add_plugins(sprite_render::TilemapChunkPlugin{});
    app.add_plugins(sprite_render::TilemapChunkMaterialPlugin{});
    return app;
}

void flush_load_tasks(App& app) {
    static constexpr int LARGE_ITERATION_COUNT = 10000;
    for (int i = 0; i < LARGE_ITERATION_COUNT; ++i) {
        app.run_schedule(PreUpdate);
        app.run_schedule(PostUpdate);
        app.run_schedule(Last);
        std::this_thread::yield();
    }
}

/** @brief A 4-layer 8x8 tileset, which is what a chunk binds as an array
 *  texture. */
Handle<image::Image> make_tileset(App& app) {
    const std::vector<std::uint8_t> pixels(8u * 8u * 4u * 4u, 255);
    std::vector<std::byte> bytes(pixels.size());
    std::memcpy(bytes.data(), pixels.data(), pixels.size());
    image::Image image{wgpu::Extent3D{8u, 8u, 4u}, wgpu::TextureDimension::e2D, std::move(bytes),
                       wgpu::TextureFormat::eRGBA8UnormSrgb,
                       static_cast<RenderAssetUsages>(RenderAssetUsages::MAIN_WORLD |
                                                      RenderAssetUsages::RENDER_WORLD)};
    return app.resource_mut<Assets<image::Image>>().emplace(std::move(image));
}

std::vector<std::optional<sprite_render::TileData>> make_tiles() {
    std::vector<std::optional<sprite_render::TileData>> tiles(4);
    tiles[0] = sprite_render::TileData::from_tileset_index(1);
    tiles[1] = std::nullopt;
    auto invisible = sprite_render::TileData::from_tileset_index(2);
    invisible.visible = false;
    tiles[2] = invisible;
    auto tinted    = sprite_render::TileData::from_tileset_index(3);
    tinted.color   = ::epix::color::Color::linear_rgba(1.0f, 0.0f, 0.0f, 1.0f);
    tiles[3]       = tinted;
    return tiles;
}

struct RetryProbe {
    bool ran     = false;
    bool retried = false;
};

/** @brief Ask the material for its bind group with no prepared GPU images at
 *  all, which is the state a texture-loading frame starts in. */
void probe_tilemap_bind_group(ecs::Res<render::RenderAssets<render::texture::GpuImage>> images,
                              ecs::ResMut<RetryProbe> probe) {
    using Material = sprite_render::TilemapChunkMaterial;
    using Group    = render::render_resource::AsBindGroup<Material>;
    typename Group::Param param{images};
    const auto prepared = Group::unprepared_bind_group(wgpu::Device{}, wgpu::BindGroupLayout{}, Material{}, param);

    probe->ran     = true;
    probe->retried = !prepared.has_value() &&
                     std::holds_alternative<render::render_resource::RetryBindGroupNextUpdate>(prepared.error());
}
}  // namespace

// Bevy `TilemapChunkPlugin::build` initializes the mesh cache and adds the
// update system (`tilemap_chunk/mod.rs:33-38`).
TEST(TilemapChunkPlugin, RegistersTheMeshCache) {
    auto app = make_tilemap_app();

    ASSERT_TRUE(app.world().get_resource<sprite_render::TilemapChunkMeshCache>().has_value());
    EXPECT_TRUE(app.resource<sprite_render::TilemapChunkMeshCache>().meshes.empty());
}

// Bevy `TilemapChunkMaterialPlugin::build` embeds
// `tilemap_chunk_material.wgsl` and installs `Material2dPlugin<..>`
// (`tilemap_chunk_material.rs:15-21`).
TEST(TilemapChunkMaterialPlugin, RegistersTheMaterialAssetAndEmbeddedShader) {
    auto app = make_tilemap_app();
    flush_load_tasks(app);

    EXPECT_TRUE(app.world().get_resource<Assets<sprite_render::TilemapChunkMaterial>>().has_value());

    auto& server = app.resource<AssetServer>();
    const auto loaded =
        server.load<Shader>(AssetPath(std::string("embedded://sprite/tilemap_chunk_material.slang")));
    flush_load_tasks(app);

    auto handle = server.get_handle<Shader>(AssetPath(std::string("embedded://sprite/tilemap_chunk_material.slang")));
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(handle->id(), loaded.id());
    EXPECT_TRUE(server.is_loaded_with_dependencies(handle->id()));

    auto shader = app.resource<Assets<Shader>>().get(handle->id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_EQ(shader->get().path, std::string_view{"embedded://sprite/tilemap_chunk_material.slang"});
}

// Bevy's derive declares the tileset as `texture(0, dimension = "2d_array")`,
// its sampler at 1 and the tile data as `texture(2, sample_type = "u_int")`,
// each visible to the vertex and fragment stages
// (`tilemap_chunk_material.rs:31-36`).
TEST(TilemapChunkMaterialBindGroup, DeclaresBevyBindingLayout) {
    using Material = sprite_render::TilemapChunkMaterial;
    using Group    = render::render_resource::AsBindGroup<Material>;
    static_assert(std::same_as<Group::Data, std::monostate>);

    EXPECT_EQ(Group::label(), std::string_view{"tilemap_chunk_material"});

    const auto entries = Group::layout_entries();
    ASSERT_EQ(entries.size(), 3u);
    const auto stages = wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment;

    EXPECT_EQ(entries[0].binding, 0u);
    EXPECT_EQ(entries[0].visibility, stages);
    EXPECT_EQ(entries[0].texture.viewDimension, wgpu::TextureViewDimension::e2DArray);
    EXPECT_EQ(entries[0].texture.sampleType, wgpu::TextureSampleType::eFloat);
    EXPECT_EQ(entries[0].sampler.type, wgpu::SamplerBindingType::eBindingNotUsed);

    EXPECT_EQ(entries[1].binding, 1u);
    EXPECT_EQ(entries[1].visibility, stages);
    EXPECT_EQ(entries[1].sampler.type, wgpu::SamplerBindingType::eFiltering);
    EXPECT_EQ(entries[1].texture.sampleType, wgpu::TextureSampleType::eBindingNotUsed);

    EXPECT_EQ(entries[2].binding, 2u);
    EXPECT_EQ(entries[2].visibility, stages);
    EXPECT_EQ(entries[2].texture.viewDimension, wgpu::TextureViewDimension::e2D);
    EXPECT_EQ(entries[2].texture.sampleType, wgpu::TextureSampleType::eUint);
    EXPECT_EQ(entries[2].texture.multisampled, wgpu::Bool(false));
}

// Both declared textures are required, so an unprepared texture retries the next
// update rather than falling back to a placeholder (Bevy `as_bind_group.rs:568`,
// which the `#[texture]` derive uses).
TEST(TilemapChunkMaterialBindGroup, RetriesUntilBothTexturesArePrepared) {
    auto app = App::create();
    app.world_mut().init_resource<render::RenderAssets<render::texture::GpuImage>>();
    app.world_mut().init_resource<RetryProbe>();
    app.add_systems(Update, ecs::into(probe_tilemap_bind_group));

    app.run_schedule(Update);

    const auto& probe = app.resource<RetryProbe>();
    EXPECT_TRUE(probe.ran);
    EXPECT_TRUE(probe.retried);
}

// Bevy's `PackedTileData` is the `Rgba16Uint` texel the fragment shader reads:
// the tileset index in red, the four tint bytes in green and blue, and the flags
// in alpha (`tilemap_chunk_material.rs:52-93`).
TEST(PackedTileData, MatchesTheShaderAbi) {
    using Tile        = sprite_render::TileData;
    using Packed      = sprite_render::PackedTileData;
    EXPECT_EQ(sizeof(Packed), 8u);
    EXPECT_EQ(offsetof(Packed, tileset_index), 0u);
    EXPECT_EQ(offsetof(Packed, color), 2u);
    EXPECT_EQ(offsetof(Packed, flags), 6u);

    const auto empty = Packed::empty();
    EXPECT_EQ(empty.tileset_index, std::numeric_limits<std::uint16_t>::max());
    EXPECT_EQ(empty.color, (std::array<std::uint8_t, 4>{0, 0, 0, 0}));
    EXPECT_EQ(empty.flags, 0u);

    // `From<Option<TileData>>` for an empty tile is Bevy's `empty()`.
    const std::optional<Tile> no_tile;
    const Packed packed_none{no_tile};
    EXPECT_EQ(packed_none.tileset_index, std::numeric_limits<std::uint16_t>::max());
    EXPECT_EQ(packed_none.flags, 0u);

    // `TileData::default()` is `Color::WHITE` and visible.
    const Packed white{Tile{}};
    EXPECT_EQ(white.tileset_index, 0u);
    EXPECT_EQ(white.color, (std::array<std::uint8_t, 4>{255, 255, 255, 255}));
    EXPECT_EQ(white.flags, 1u);

    // The tint is linear in Epix, so it is encoded to sRGB before packing:
    // Bevy's `Color::to_srgba().to_u8_array()` on `LinearRgba(0.5)` is 188.
    auto tint      = Tile::from_tileset_index(3);
    tint.color     = ::epix::color::Color::linear_rgba(0.5f, 0.5f, 0.5f, 0.5f);
    tint.visible   = false;
    const Packed packed{tint};
    EXPECT_EQ(packed.tileset_index, 3u);
    EXPECT_EQ(packed.color[0], 188u);
    EXPECT_EQ(packed.color[3], 128u);
    EXPECT_EQ(packed.flags, 0u);
}

// Bevy `make_chunk_tile_data_image` builds an `Rgba16Uint`, one-texel-per-tile
// 2D image with a nearest sampler and the packed slice as its payload
// (`tilemap_chunk_material.rs:95-114`).
TEST(MakeChunkTileDataImage, BuildsTheChunkTexture) {
    const auto tiles = make_tiles();
    std::vector<sprite_render::PackedTileData> packed;
    for (const auto& tile : tiles) packed.emplace_back(tile);

    const auto image = sprite_render::make_chunk_tile_data_image(glm::uvec2(2u, 2u), packed);

    EXPECT_EQ(image.texture_format(), wgpu::TextureFormat::eRGBA16Uint);
    EXPECT_EQ(image.width(), 2u);
    EXPECT_EQ(image.height(), 2u);
    EXPECT_EQ(image.depth_or_layers(), 1u);
    EXPECT_EQ(image.type(), image::ImageType::e2D);
    EXPECT_EQ(image.data_order, image::TextureDataOrder::LayerMajor);
    EXPECT_FALSE(image.copy_on_resize());
    EXPECT_FALSE(image.texture_view_descriptor.has_value());
    EXPECT_TRUE(image.has_data());

    const auto bytes = image.raw_view();
    ASSERT_EQ(bytes.size(), packed.size() * sizeof(sprite_render::PackedTileData));
    EXPECT_EQ(std::memcmp(bytes.data(), packed.data(), bytes.size()), 0);

    ASSERT_NE(image.sampler_descriptor(), nullptr);
    EXPECT_EQ(image.sampler_descriptor()->mag_filter, image::ImageFilterMode::Nearest);
    EXPECT_EQ(image.sampler_descriptor()->min_filter, image::ImageFilterMode::Nearest);

    // The main world keeps the pixels, which is what lets
    // `update_tilemap_chunk_indices` rewrite them in place.
    EXPECT_NE(static_cast<std::uint8_t>(image.usage() & RenderAssetUsages::MAIN_WORLD), 0u);
    EXPECT_NE(static_cast<std::uint8_t>(image.usage() & RenderAssetUsages::RENDER_WORLD), 0u);
}

// Bevy `TilemapChunk::calculate_tile_transform` (`tilemap_chunk/mod.rs:62-86`):
// tile (0, 0) sits at the chunk's bottom-left corner, and the chunk is centred
// on the entity's origin.
TEST(TilemapChunk, CalculateTileTransformMatchesBevy) {
    const sprite_render::TilemapChunk chunk{.chunk_size = {4u, 2u}, .tile_display_size = {10u, 20u}};

    const auto bottom_left = chunk.calculate_tile_transform(glm::uvec2(0u, 0u));
    EXPECT_EQ(bottom_left.translation, (glm::vec3(-15.0f, -10.0f, 0.0f)));

    const auto top_right = chunk.calculate_tile_transform(glm::uvec2(3u, 1u));
    EXPECT_EQ(top_right.translation, (glm::vec3(15.0f, 10.0f, 0.0f)));

    const auto next = chunk.calculate_tile_transform(glm::uvec2(1u, 0u));
    EXPECT_EQ(next.translation - bottom_left.translation, (glm::vec3(10.0f, 0.0f, 0.0f)));
}

// Bevy `TilemapChunkTileData::tile_data_from_tile_pos`
// (`tilemap_chunk/mod.rs:241-251`): row-major from the chunk's bottom-left, an
// out-of-range position and an empty tile both answer nothing.
TEST(TilemapChunkTileData, LooksTilesUpRowMajor) {
    sprite_render::TilemapChunkTileData data{.tiles = make_tiles()};

    const auto first = data.tile_data_from_tile_pos(glm::uvec2(2u, 2u), glm::uvec2(0u, 0u));
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->get().tileset_index, 1u);

    const auto second_row = data.tile_data_from_tile_pos(glm::uvec2(2u, 2u), glm::uvec2(0u, 1u));
    ASSERT_TRUE(second_row.has_value());
    EXPECT_EQ(second_row->get().tileset_index, 2u);

    EXPECT_FALSE(data.tile_data_from_tile_pos(glm::uvec2(2u, 2u), glm::uvec2(1u, 0u)).has_value());
    // Bevy's lookup is a bare row-major index, so tile (2, 0) reads the second
    // row's first tile rather than failing, and only an index past the vector
    // answers nothing.
    EXPECT_TRUE(data.tile_data_from_tile_pos(glm::uvec2(2u, 2u), glm::uvec2(2u, 0u)).has_value());
    EXPECT_FALSE(data.tile_data_from_tile_pos(glm::uvec2(2u, 2u), glm::uvec2(0u, 2u)).has_value());
}

// Bevy's `on_insert_tilemap_chunk` hook builds the chunk's tile-data image and
// material and attaches the mesh and material components
// (`tilemap_chunk/mod.rs:126-183`).
TEST(TilemapChunk, OnInsertAttachesTheChunkMeshAndMaterial) {
    auto app    = make_tilemap_app();
    auto tileset = make_tileset(app);

    const auto entity = app.world_mut()
                            .spawn(sprite_render::TilemapChunk{.chunk_size        = {2u, 2u},
                                                               .tile_display_size = {8u, 8u},
                                                               .tileset           = tileset},
                                   sprite_render::TilemapChunkTileData{.tiles = make_tiles()})
                            .id();

    const auto chunk_entity = app.world().entity(entity);
    ASSERT_TRUE(chunk_entity.contains<mesh::Mesh2d>());
    ASSERT_TRUE(chunk_entity.contains<sprite_render::MeshMaterial2d<sprite_render::TilemapChunkMaterial>>());
    // `Mesh2d` requires a transform.
    EXPECT_TRUE(chunk_entity.contains<epix::transform::Transform>());

    const auto material_id =
        chunk_entity.get<sprite_render::MeshMaterial2d<sprite_render::TilemapChunkMaterial>>()->get().id();
    const auto& materials = app.resource<Assets<sprite_render::TilemapChunkMaterial>>();
    const auto material   = materials.get(material_id);
    ASSERT_TRUE(material.has_value());
    EXPECT_EQ(material->get().tileset, tileset);
    EXPECT_TRUE(std::holds_alternative<sprite_render::AlphaMode2dOpaque>(material->get().alpha_mode()));
    EXPECT_TRUE(material->get().tile_data.id() != tileset.id());

    const auto tile_data_image =
        app.resource<Assets<image::Image>>().get(material->get().tile_data.id());
    ASSERT_TRUE(tile_data_image.has_value());
    EXPECT_EQ(tile_data_image->get().texture_format(), wgpu::TextureFormat::eRGBA16Uint);
    EXPECT_EQ(tile_data_image->get().width(), 2u);
    EXPECT_EQ(tile_data_image->get().height(), 2u);

    // The texels are the tiles in row-major order, so the empty tile is the
    // sentinel and the invisible one carries no flag.
    const auto bytes = tile_data_image->get().raw_view();
    ASSERT_EQ(bytes.size(), 4u * sizeof(sprite_render::PackedTileData));
    std::array<sprite_render::PackedTileData, 4> texels{};
    std::memcpy(texels.data(), bytes.data(), bytes.size());
    EXPECT_EQ(texels[0].tileset_index, 1u);
    EXPECT_EQ(texels[0].flags, 1u);
    EXPECT_EQ(texels[1].tileset_index, std::numeric_limits<std::uint16_t>::max());
    EXPECT_EQ(texels[2].tileset_index, 2u);
    EXPECT_EQ(texels[2].flags, 0u);
    EXPECT_EQ(texels[3].tileset_index, 3u);
    EXPECT_EQ(texels[3].color, (std::array<std::uint8_t, 4>{255, 0, 0, 255}));

    // The chunk is one quad of `chunk_size * tile_display_size` world units.
    const auto mesh_handle = chunk_entity.get<mesh::Mesh2d>()->get().handle;
    const auto chunk_mesh  = app.resource<Assets<mesh::Mesh>>().get(mesh_handle.id());
    ASSERT_TRUE(chunk_mesh.has_value());
    const auto* positions =
        chunk_mesh->get().attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        chunk_mesh->get().attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(uvs, nullptr);
    EXPECT_EQ(*positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                              {8.0f, 8.0f, 0.0f},
                              {-8.0f, 8.0f, 0.0f},
                              {-8.0f, -8.0f, 0.0f},
                              {8.0f, -8.0f, 0.0f},
                          }));
    EXPECT_EQ(*uvs, (std::vector<mesh::VertexAttributeValues::Float32x2Value>{
                        {1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}}));
}

// Bevy `update_tilemap_chunk_indices` (`tilemap_chunk/mod.rs:185-239`) rewrites
// the material's tile-data image whenever the component changes; reaching the
// image through `get_mut` is also what makes the render world re-upload it.
TEST(UpdateTilemapChunkIndices, RewritesTheTileDataImage) {
    auto app     = make_tilemap_app();
    auto tileset = make_tileset(app);

    const auto entity = app.world_mut()
                            .spawn(sprite_render::TilemapChunk{.chunk_size        = {2u, 2u},
                                                               .tile_display_size = {8u, 8u},
                                                               .tileset           = tileset},
                                   sprite_render::TilemapChunkTileData{.tiles = make_tiles()})
                            .id();
    app.run_schedule(Update);

    const auto tile_data_id =
        app.world()
            .entity(entity)
            .get<sprite_render::MeshMaterial2d<sprite_render::TilemapChunkMaterial>>()
            ->get()
            .id();

    const auto read_texels = [&app](const AssetId<sprite_render::TilemapChunkMaterial>& material_id) {
        const auto& materials = app.resource<Assets<sprite_render::TilemapChunkMaterial>>();
        const auto material   = materials.get(material_id);
        EXPECT_TRUE(material.has_value());
        const auto image = app.resource<Assets<image::Image>>().get(material->get().tile_data.id());
        EXPECT_TRUE(image.has_value());
        std::array<sprite_render::PackedTileData, 4> texels{};
        const auto bytes = image->get().raw_view();
        EXPECT_EQ(bytes.size(), texels.size() * sizeof(sprite_render::PackedTileData));
        std::memcpy(texels.data(), bytes.data(), std::min(bytes.size(), sizeof(texels)));
        return texels;
    };

    // The initial tiles reached the image.
    EXPECT_EQ(read_texels(tile_data_id)[3].tileset_index, 3u);

    {
        auto tile_data = app.world_mut().entity_mut(entity).get_mut<sprite_render::TilemapChunkTileData>();
        ASSERT_TRUE(tile_data.has_value());
        auto& tiles = tile_data->get_mut().tiles;
        tiles[0]    = sprite_render::TileData::from_tileset_index(9);
        tiles[1]    = sprite_render::TileData::from_tileset_index(9);
    }
    // Advance the change tick so the system's `Modified` filter sees the write.
    app.world_mut().flush();
    app.run_schedule(Update);

    const auto texels = read_texels(tile_data_id);
    EXPECT_EQ(texels[0].tileset_index, 9u);
    EXPECT_EQ(texels[1].tileset_index, 9u);
    EXPECT_EQ(texels[2].tileset_index, 2u);
}

// The hash exists only because C++ needs one for the `glm::uvec2` key; equal
// chunk sizes must land in the same bucket.
TEST(TilemapChunkMeshCacheHash, HashesEqualChunkSizesEqually) {
    const sprite_render::TilemapChunkMeshCacheHash hash;
    EXPECT_EQ(hash(glm::uvec2(64u, 64u)), hash(glm::uvec2(64u, 64u)));
    EXPECT_NE(hash(glm::uvec2(64u, 64u)), hash(glm::uvec2(64u, 32u)));
}
