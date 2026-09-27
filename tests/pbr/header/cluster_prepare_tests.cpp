#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/pbr/cluster_prepare.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

using namespace epix::app;
using namespace epix::assets;

namespace pbr    = epix::pbr;
namespace render = epix::render;

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
        .add_plugins(epix::shader::ShaderPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::view::ViewPlugin{})
        .add_plugins(render::GlobalsPlugin{});
}

}  // namespace

/** @brief The per-view cluster bindings are built from the view's extracted
 *  records, uploaded, and inserted on the view (Bevy `prepare_clusters`,
 *  `bevy_pbr/src/cluster.rs:289-336`).
 *
 *  The walk itself is `prepare_view_cluster_bindings`, covered without a device;
 *  what this checks is the system: that it reads the buffer binding type off
 *  `MeshPipeline` rather than the global cluster settings, and that it leaves the
 *  view holding the bindings. */
TEST(ClusterPrepare, BuildsAndInsertsTheViewBindings) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(app);
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world = render_app->get().world_mut();

    // The pipeline's binding type decides whether the index lists may grow past
    // MAX_INDICES, which is why the system reads it from here. The shader handle is
    // weak: this test never builds a pipeline, it only needs the resource to exist.
    pbr::MeshPipeline mesh_pipeline{
        .view_layouts = {},
        .clustered_forward_buffer_binding_type = wgpu::BufferBindingType::eStorage,
        .mesh_layouts = pbr::MeshLayouts::make(wgpu::Limits{}, /*binding_arrays_are_usable=*/false),
        .shader = epix::assets::Handle<epix::shader::Shader>{epix::assets::AssetId<epix::shader::Shader>::invalid()},
    };
    world.insert_resource(std::move(mesh_pipeline));

    // One clusterable object is known to the global index map and one is not: the
    // walk must count the first and skip the second.
    const epix::ecs::Entity known{41};
    const epix::ecs::Entity unknown{42};
    pbr::GlobalClusterableObjectMeta meta{wgpu::BufferBindingType::eStorage};
    meta.entity_to_index.emplace(known, 5u);
    world.insert_resource(std::move(meta));

    pbr::ExtractedClusterableObjects extracted;
    extracted.data.push_back(pbr::ExtractedClusterableObjectElement{
        .kind = pbr::ExtractedClusterableObjectElement::Kind::ClusterHeader, .counts = pbr::ClusterableObjectCounts{}});
    extracted.data.push_back(pbr::ExtractedClusterableObjectElement{
        .kind = pbr::ExtractedClusterableObjectElement::Kind::ClusterableObjectEntity, .entity = known});
    extracted.data.push_back(pbr::ExtractedClusterableObjectElement{
        .kind = pbr::ExtractedClusterableObjectElement::Kind::ClusterableObjectEntity, .entity = unknown});

    const epix::ecs::Entity view = world.spawn(std::move(extracted)).id();

    render_app->get().add_systems(render::RenderStartup,
                                  epix::ecs::into(epix::pbr::prepare_clusters).set_name("prepare clusters"));
    render_app->get().run_schedule(render::RenderStartup);

    // The bindings are inserted on the view; the walk skipped the unknown object.
    const auto bindings = world.entity(view).get<pbr::ViewClusterBindings>();
    ASSERT_TRUE(bindings.has_value());
    EXPECT_EQ(bindings.value().get().n_indices, 1u) << "the unknown clusterable object is skipped, not counted";
}
