#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/lightmap.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <exception>
#include <utility>

using namespace epix::app;
using namespace epix::assets;
using namespace epix::render;

namespace pbr = epix::pbr;

namespace {

// Attaching RenderPlugin loads an embedded shader, which spawns an async load
// task that terminates the process when the IO task pool has not been
// initialized.
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} g_io_task_pool_init;

void add_render_prerequisites(App& app) {
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::mesh::MeshPlugin{})
        .add_plugins(epix::shader::ShaderPlugin{})
        .add_plugins(FrameCountPlugin{})
        .add_plugins(view::ViewPlugin{})
        .add_plugins(GlobalsPlugin{});
}

}  // namespace

/** @brief The lightmap resource is created empty and then filled by extraction
 *  (Bevy `init_render_lightmaps` and `extract_lightmaps`,
 *  `lightmap/mod.rs:203-350`).
 *
 *  Both halves are exercised here because they are two ends of one contract: the
 *  initialiser decides only whether binding arrays are usable and leaves the slabs
 *  to `allocate`, and extraction is what calls `allocate` - for changed, *visible*
 *  lightmaps only - and hands the slot back when the component goes. */
TEST(LightmapExtract, InitialisesEmptyAndAllocatesForVisibleLightmaps) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(app);
    try {
        app.add_plugins(RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    // `MeshAllocatorPlugin` creates `MeshAllocator` in its `finish`/ready hook,
    // which a fixture that never calls `App::run` has to invoke directly
    // (`bevy_render/src/mesh/allocator.rs:1067-1073`); the render schedule it is
    // registered in runs below.
    epix::render::mesh::MeshAllocatorPlugin{}.ready(app);

    auto render_app = app.get_sub_app_mut(Render);
    ASSERT_TRUE(render_app.has_value());

    // A lightmap needs an uploaded image to be filled into a slab, so the scene has
    // one, and the lightmap carries its asset id.
    // The port's `Image::new` are constructors (C++ has no `new`-named factory), and
    // the four-argument one is Bevy's `new_uninit`: the payload is empty, which is
    // enough - the lightmap only needs the image to exist and be uploaded.
    epix::image::Image image{wgpu::Extent3D{4u, 4u, 1u}, wgpu::TextureDimension::e2D,
                             wgpu::TextureFormat::eRGBA8Unorm, RenderAssetUsages::RENDER_WORLD};
    const auto image_handle = app.world_mut().resource_mut<Assets<epix::image::Image>>().add(std::move(image));
    (void)image_handle;

    epix::camera::ViewVisibility visible;
    visible.set_visible();
    const epix::ecs::Entity lit = app.world_mut().spawn(pbr::Lightmap{}, visible).id();
    const epix::ecs::Entity invisible =
        app.world_mut().spawn(pbr::Lightmap{}, epix::camera::ViewVisibility::HIDDEN).id();

    // The systems are registered by the plugin rather than by hand, so this also covers
    // its scheduling: the initialiser at render startup, the extraction on the extract
    // schedule after `MeshExtractionSystems` (Bevy `LightmapPlugin::build`).
    app.add_plugins(pbr::LightmapPlugin{});
    render_app->get().run_schedule(RenderStartup);

    // What the initialiser leaves behind: an empty resource, not a pre-built slab.
    const auto& initialised = render_app->get().world().resource<pbr::RenderLightmaps>();
    EXPECT_TRUE(initialised.slabs.empty()) << "slabs are created on demand, by allocate";
    EXPECT_TRUE(initialised.render_lightmaps.empty());

    auto render_sub = app.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    render_sub->extract(app);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    const auto& lightmaps = render_sub->world().resource<pbr::RenderLightmaps>();
    const auto lit_record =
        lightmaps.render_lightmaps.find(sync_world::MainEntity{lit});
    ASSERT_NE(lit_record, lightmaps.render_lightmaps.end()) << "a changed, visible lightmap gets a record";
    EXPECT_FALSE(lightmaps.slabs.empty()) << "which means a slab was created for it";
    EXPECT_EQ(lit_record->second.slab_index.index, 0u);
    EXPECT_FALSE(lightmaps.pending_lightmaps.empty())
        << "and its slot waits for the image, which is what the pending sweep is for";
    EXPECT_FALSE(lightmaps.render_lightmaps.contains(sync_world::MainEntity{invisible}))
        << "an invisible lightmap is skipped";
}
