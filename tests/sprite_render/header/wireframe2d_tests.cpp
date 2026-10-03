#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/sprite_render.hpp>
#include <epix/task.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

using namespace epix::app;
using namespace epix::assets;
using namespace epix::shader;

namespace sprite_render = epix::sprite_render;
namespace render        = epix::render;

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

void flush_load_tasks(App& app) {
    static constexpr int LARGE_ITERATION_COUNT = 10000;
    for (int i = 0; i < LARGE_ITERATION_COUNT; ++i) {
        app.run_schedule(PreUpdate);
        app.run_schedule(PostUpdate);
        app.run_schedule(Last);
        std::this_thread::yield();
    }
}

/** @brief Two distinct material ids and two distinct mesh ids, taken from real
 *  asset stores so no id is invented. */
struct TestAssets {
    Assets<sprite_render::Wireframe2dMaterial> materials;
    Assets<epix::mesh::Mesh> meshes;

    AssetId<sprite_render::Wireframe2dMaterial> first_material;
    AssetId<sprite_render::Wireframe2dMaterial> second_material;
    AssetId<epix::mesh::Mesh> first_mesh;
    AssetId<epix::mesh::Mesh> second_mesh;

    TestAssets()
        : first_material(materials.emplace(sprite_render::Wireframe2dMaterial{}).id()),
          second_material(materials.emplace(sprite_render::Wireframe2dMaterial{}).id()),
          first_mesh(meshes
                         .emplace(epix::mesh::Mesh(wgpu::PrimitiveTopology::eTriangleList,
                                                   RenderAssetUsages::RENDER_WORLD))
                         .id()),
          second_mesh(meshes
                          .emplace(epix::mesh::Mesh(wgpu::PrimitiveTopology::eTriangleList,
                                                    RenderAssetUsages::RENDER_WORLD))
                          .id()) {}
};
}  // namespace

// Bevy `Wireframe2dPlugin::build` embeds `wireframe2d.wgsl`, initializes
// `Wireframe2dConfig`, and registers the material asset and the entity list the
// specialization systems consume (`wireframe2d.rs:80-108`).
TEST(Wireframe2dPlugin, RegistersItsResourcesAssetsAndEmbeddedShader) {
    auto app = make_shader_asset_app();

    app.add_plugins(sprite_render::Wireframe2dPlugin{});
    flush_load_tasks(app);

    ASSERT_TRUE(app.world().get_resource<sprite_render::Wireframe2dConfig>().has_value());
    const auto& config = app.resource<sprite_render::Wireframe2dConfig>();
    // Bevy's `Default` derive: `global = false`, `default_color = Color::WHITE`.
    EXPECT_FALSE(config.global);
    EXPECT_EQ(config.default_color, ::epix::color::Color::linear_rgba(1.0f, 1.0f, 1.0f, 1.0f));

    EXPECT_TRUE(app.world().get_resource<sprite_render::WireframeEntitiesNeedingSpecialization>().has_value());
    EXPECT_TRUE(app.world().get_resource<Assets<sprite_render::Wireframe2dMaterial>>().has_value());

    auto& server = app.resource<AssetServer>();
    // The plugin registers the embedded shader; requesting the handle is what
    // makes the asset server load it (in a real app the render-startup pipeline
    // init does this, and that needs a device).
    const auto loaded = server.load<Shader>(AssetPath(std::string("embedded://sprite/wireframe2d.slang")));
    flush_load_tasks(app);

    auto handle = server.get_handle<Shader>(AssetPath(std::string("embedded://sprite/wireframe2d.slang")));
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(handle->id(), loaded.id());
    EXPECT_TRUE(server.is_loaded_with_dependencies(handle->id()));

    auto shader = app.resource<Assets<Shader>>().get(handle->id());
    ASSERT_TRUE(shader.has_value());
    // The shader asset records the full asset path it was loaded from, as the
    // other embedded-shader tests assert.
    constexpr std::string_view kPath = "embedded://sprite/wireframe2d.slang";
    EXPECT_EQ(shader->get().path, kPath);
}

// Bevy derives `Ord` over `Wireframe2dBatchSetKey`'s fields in declaration
// order: pipeline, material asset, draw function, vertex slab, index slab
// (`wireframe2d.rs:240-266`).
TEST(Wireframe2dBatchSetKey, OrdersByPipelineThenAssetThenDrawFunctionThenSlabs) {
    TestAssets assets;
    const sprite_render::Wireframe2dBatchSetKey base{
        .pipeline      = render::CachedPipelineId{1},
        .asset_id      = UntypedAssetId{assets.first_material},
        .draw_function = render::phase::DrawFunctionId{1},
        .vertex_slab   = render::mesh::SlabId{1},
        .index_slab    = std::nullopt,
    };
    const auto same = base;
    EXPECT_TRUE(base == same);
    EXPECT_FALSE(base < same);

    auto higher_pipeline     = base;
    higher_pipeline.pipeline = render::CachedPipelineId{2};
    EXPECT_TRUE(base < higher_pipeline);

    auto higher_asset     = base;
    higher_asset.asset_id = UntypedAssetId{assets.second_material};
    EXPECT_TRUE(base < higher_asset);

    auto higher_draw          = base;
    higher_draw.draw_function = render::phase::DrawFunctionId{2};
    EXPECT_TRUE(base < higher_draw);

    auto higher_vertex_slab        = base;
    higher_vertex_slab.vertex_slab = render::mesh::SlabId{2};
    EXPECT_TRUE(base < higher_vertex_slab);

    // `PhaseItemBatchSetKey::indexed` is `index_slab.is_some()`, and an indexed
    // key sorts after the same key without an index slab.
    EXPECT_FALSE(base.indexed());
    auto indexed       = base;
    indexed.index_slab = render::mesh::SlabId{1};
    EXPECT_TRUE(indexed.indexed());
    EXPECT_TRUE(base < indexed);

    // Equal keys hash equally.
    EXPECT_EQ(std::hash<sprite_render::Wireframe2dBatchSetKey>{}(base),
              std::hash<sprite_render::Wireframe2dBatchSetKey>{}(same));
}

// Bevy's bin key holds only the wireframe mesh asset (`wireframe2d.rs:268-275`).
TEST(Wireframe2dBinKey, KeysOnTheMeshAsset) {
    TestAssets assets;
    const sprite_render::Wireframe2dBinKey first{.asset_id = UntypedAssetId{assets.first_mesh}};
    const sprite_render::Wireframe2dBinKey same{.asset_id = UntypedAssetId{assets.first_mesh}};
    const sprite_render::Wireframe2dBinKey other{.asset_id = UntypedAssetId{assets.second_mesh}};

    EXPECT_TRUE(first == same);
    EXPECT_FALSE(first == other);
    EXPECT_TRUE(first < other);
    EXPECT_EQ(std::hash<sprite_render::Wireframe2dBinKey>{}(first),
              std::hash<sprite_render::Wireframe2dBinKey>{}(same));
}

// The phase item satisfies Bevy's cached-pipeline and binned traits, and both
// its draw function and its pipeline come from the *batch-set* key
// (`wireframe2d.rs:183-238`).
TEST(Wireframe2dPhaseItem, ReadsDrawFunctionAndPipelineFromTheBatchSetKey) {
    static_assert(render::phase::CachedRenderPipelinePhaseItem<sprite_render::Wireframe2dPhaseItem>);
    static_assert(render::phase::BinnedPhaseItem<sprite_render::Wireframe2dPhaseItem>);

    TestAssets assets;
    const epix::ecs::Entity render_entity = epix::ecs::Entity::from_index(7u);
    const epix::ecs::Entity main_entity   = epix::ecs::Entity::from_index(3u);

    const sprite_render::Wireframe2dBatchSetKey batch_set_key{
        .pipeline      = render::CachedPipelineId{11},
        .asset_id      = UntypedAssetId{assets.first_material},
        .draw_function = render::phase::DrawFunctionId{22},
        .vertex_slab   = render::mesh::SlabId{3},
        .index_slab    = render::mesh::SlabId{4},
    };
    const sprite_render::Wireframe2dBinKey bin_key{.asset_id = UntypedAssetId{assets.first_mesh}};

    auto item = sprite_render::Wireframe2dPhaseItem::create(
        batch_set_key, bin_key, std::pair{render_entity, render::sync_world::MainEntity{main_entity}},
        std::pair<std::uint32_t, std::uint32_t>{0u, 1u}, render::phase::PhaseItemExtraIndex{});

    EXPECT_EQ(item.entity(), render_entity);
    EXPECT_EQ(item.main_entity(), render::sync_world::MainEntity{main_entity});
    EXPECT_EQ(item.draw_function().get(), render::phase::DrawFunctionId{22}.get());
    EXPECT_EQ(item.cached_pipeline().get(), render::CachedPipelineId{11}.get());
    EXPECT_EQ(item.batch_range().first, 0u);
    EXPECT_EQ(item.batch_range().second, 1u);
    EXPECT_EQ(item.bin_key_value.asset_id, bin_key.asset_id);

    item.batch_range() = {4u, 9u};
    EXPECT_EQ(item.batch_range().second, 9u);
}

// `RenderWireframeMaterial` preparation copies the (already linear) colour, and
// the asset keeps the default usage so `Mesh2dWireframe` holds it in both worlds
// (`wireframe2d.rs:470-484`).
TEST(RenderWireframeMaterial, PreparationCopiesTheColour) {
    auto prepared = render::RenderAsset<sprite_render::RenderWireframeMaterial>::prepare_asset(
        sprite_render::Wireframe2dMaterial{.color = ::epix::color::Color::linear_rgba(0.25f, 0.5f, 0.75f, 1.0f)},
        AssetId<sprite_render::Wireframe2dMaterial>::invalid(), std::tuple<>{}, nullptr);
    ASSERT_TRUE(prepared.has_value());
    EXPECT_EQ(prepared->color, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));

    const auto usage = render::RenderAsset<sprite_render::RenderWireframeMaterial>::asset_usage(
        sprite_render::Wireframe2dMaterial{});
    EXPECT_NE(static_cast<std::uint8_t>(usage & RenderAssetUsages::MAIN_WORLD), 0u);
    EXPECT_NE(static_cast<std::uint8_t>(usage & RenderAssetUsages::RENDER_WORLD), 0u);
}

// `Mesh2dWireframe` is an asset reference, which is what `AssetChanged<..>`
// needs to re-specialize a wireframe (`wireframe2d.rs:458-468`).
TEST(Mesh2dWireframe, ExposesItsMaterialAssetId) {
    static_assert(AsAssetIdImpl<sprite_render::Mesh2dWireframe>);

    Assets<sprite_render::Wireframe2dMaterial> materials;
    const auto handle =
        materials.emplace(sprite_render::Wireframe2dMaterial{.color = ::epix::color::Color::linear_rgba(0.1f, 0.2f, 0.3f, 1.0f)});
    const sprite_render::Mesh2dWireframe wireframe{.handle = handle};

    EXPECT_EQ(AsAssetId<sprite_render::Mesh2dWireframe>::as_asset_id(wireframe), handle.id());
}

namespace {
/** @brief The wireframe material events one frame published. */
struct WireframeMaterialEvents {
    std::size_t added    = 0;
    std::size_t modified = 0;
};
}  // namespace

/** @brief Bevy registers `global_color_changed` behind
 *  `run_if(resource_changed::<Wireframe2dConfig>)` (`wireframe2d.rs:93-102`).
 *  Without it the system rewrites the global material through `get_mut` every
 *  frame, which re-marks it modified and re-prepares `RenderWireframeMaterial`
 *  for nothing. */
TEST(Wireframe2dPlugin, GlobalColourChangedOnlyRunsWhenTheConfigChanged) {
    auto app = make_shader_asset_app();
    app.add_plugins(sprite_render::Wireframe2dPlugin{});
    app.world_mut().insert_resource(WireframeMaterialEvents{});
    app.add_systems(Last,
                    epix::ecs::into([](epix::ecs::EventReader<AssetEvent<sprite_render::Wireframe2dMaterial>> events,
                                       epix::ecs::ResMut<WireframeMaterialEvents> counts) {
                        for (const auto& event : events.read()) {
                            if (event.is_added()) ++counts->added;
                            if (event.is_modified()) ++counts->modified;
                        }
                    })
                        .after(AssetSystems::WriteEvents)
                        .set_name("count wireframe material events"));
    flush_load_tasks(app);

    app.update();  // Startup creates the global material from the config.
    const auto baseline = app.resource<WireframeMaterialEvents>().modified;

    app.update();  // The config did not change, so the writer must stay off.
    EXPECT_EQ(app.resource<WireframeMaterialEvents>().modified, baseline)
        << "global_color_changed only runs when Wireframe2dConfig changed";

    app.world_mut().resource_mut<sprite_render::Wireframe2dConfig>().default_color =
        ::epix::color::Color::linear_rgba(0.0f, 1.0f, 0.0f, 1.0f);
    app.update();
    EXPECT_GT(app.resource<WireframeMaterialEvents>().modified, baseline)
        << "a changed config still reaches the global material";

    const auto& handle = app.resource<sprite_render::GlobalWireframeMaterial>().handle;
    const auto& global = app.resource<Assets<sprite_render::Wireframe2dMaterial>>().get(handle);
    ASSERT_TRUE(global.has_value());
    EXPECT_EQ(global->get().color, ::epix::color::Color::linear_rgba(0.0f, 1.0f, 0.0f, 1.0f));
}
