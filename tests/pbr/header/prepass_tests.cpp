#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/core_graph/core3d.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/mesh_instance.hpp>
#include <epix/pbr/prepass.hpp>
#include <epix/pbr/prepass_shaders.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <exception>
#include <optional>
#include <vector>

using namespace epix::app;
using namespace epix::assets;
namespace ecs    = epix::ecs;
namespace pbr    = epix::pbr;
namespace render = epix::render;

namespace {

/** @brief `RenderPlugin` loads its shader libraries through the asset server, which needs an
 *  I/O task pool before the plugin attaches. */
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} g_io_task_pool_init;

void add_prerequisites(App& app) {
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::mesh::MeshPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
}

/** @brief The mesh pipeline the prepass pipeline takes its layouts and its device facts
 *  from.
 *
 *  Two of the copied facts are set to values the device would not report on its own
 *  (`binding_arrays_are_usable` and `skins_use_uniform_buffers`), so a test that reads them
 *  back off the prepass pipeline proves the copy rather than agreeing with a default. */
pbr::MeshPipeline make_mesh_pipeline() {
    return pbr::MeshPipeline{
        .view_layouts                          = {},
        .clustered_forward_buffer_binding_type = wgpu::BufferBindingType::eStorage,
        .mesh_layouts                          = pbr::MeshLayouts::make(wgpu::Limits{}, /*binding_arrays_are_usable=*/false),
        .shader       = Handle<epix::shader::Shader>{AssetId<epix::shader::Shader>::invalid()},
        .binding_arrays_are_usable = true,
        .skins_use_uniform_buffers = true,
    };
}

/** @brief The six commands Bevy's `DrawPrepass` names, each of which has to satisfy
 *  `RenderCommandImpl` for the phase the sequence is registered for.
 *
 *  A sequence whose commands do not is ill-formed at the *registration*, not at its own
 *  definition, which is how a whole draw path can look ported while never having been
 *  instantiated (PBR-67). */
template <typename P>
void assert_draw_prepass_is_well_formed() {
    static_assert(epix::render::phase::is_render_command_sequence<pbr::DrawPrepass<P>>::value,
                  "DrawPrepass is not a RenderCommandSequence");
    static_assert(epix::render::phase::RenderCommandImpl<render::phase::SetItemPipeline, P>);
    static_assert(epix::render::phase::RenderCommandImpl<pbr::SetPrepassViewBindGroup<0>, P>);
    static_assert(epix::render::phase::RenderCommandImpl<pbr::SetPrepassViewEmptyBindGroup<1>, P>);
    static_assert(epix::render::phase::RenderCommandImpl<pbr::SetMeshBindGroup<2>, P>);
    static_assert(epix::render::phase::RenderCommandImpl<pbr::SetMaterialBindGroup<pbr::MATERIAL_BIND_GROUP_INDEX>, P>);
    static_assert(epix::render::phase::RenderCommandImpl<pbr::DrawMesh, P>);
}

std::vector<std::uint32_t> bindings_of(const render::BindGroupLayoutDescriptor& layout) {
    std::vector<std::uint32_t> bindings;
    for (const auto& entry : layout.entries) bindings.push_back(entry.binding);
    return bindings;
}

}  // namespace

/** @brief Every phase `DrawPrepass` is registered for is drawn by a sequence whose six
 *  commands are valid render commands (Bevy `prepass/mod.rs:158-161`). */
TEST(PrepassPipeline, EveryDrawPrepassCommandSatisfiesTheRenderCommandConcept) {
    assert_draw_prepass_is_well_formed<epix::core_graph::core_3d::Opaque3dPrepass>();
    assert_draw_prepass_is_well_formed<epix::core_graph::core_3d::AlphaMask3dPrepass>();
    assert_draw_prepass_is_well_formed<epix::core_graph::core_3d::Opaque3dDeferred>();
    assert_draw_prepass_is_well_formed<epix::core_graph::core_3d::AlphaMask3dDeferred>();
}

/** @brief `init_prepass_pipeline` builds Bevy's two view layouts and copies the device facts
 *  it would otherwise re-read (`prepass/mod.rs:258-331`).
 *
 *  The bindings are the ones the prepass shader's view bind group declares: the view uniform,
 *  the globals, the previous view uniforms only in the motion-vector layout, and the
 *  visibility ranges at binding 14 - and that last one is vertex-only, which is what lets a
 *  fragment stage ignore it. */
TEST(PrepassPipeline, TheViewLayoutsAreThePrepassShadersAndTheFactsAreCopied) {
    App application = App::create();
    add_prerequisites(application);
    try {
        application.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    auto render_app = application.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& render = render_app->get();

    const auto device = render.world().get_resource<wgpu::Device>();
    ASSERT_TRUE(device.has_value());

    const auto mesh_pipeline = make_mesh_pipeline();
    render.world_mut().insert_resource(mesh_pipeline);
    render.world_mut().insert_resource(pbr::MaterialPipeline{mesh_pipeline});
    pbr::register_mesh_shaders(application.world_mut());
    pbr::register_prepass_shaders(application.world_mut());

    render.add_systems(render::RenderStartup,
                       ecs::into(pbr::init_prepass_pipeline).set_name("init prepass pipeline"));
    render.run_schedule(render::RenderStartup);

    const auto pipeline = render.world().get_resource<pbr::PrepassPipeline>();
    ASSERT_TRUE(pipeline.has_value()) << "init_prepass_pipeline inserts the pipeline resource";

    EXPECT_EQ(bindings_of(pipeline->get().view_layout_motion_vectors), (std::vector<std::uint32_t>{0, 1, 2, 14}))
        << "the motion-vector view layout's bindings";
    EXPECT_EQ(bindings_of(pipeline->get().view_layout_no_motion_vectors), (std::vector<std::uint32_t>{0, 1, 14}))
        << "the layout without motion vectors leaves the previous view uniforms out";
    EXPECT_TRUE(pipeline->get().empty_layout.entries.empty())
        << "the empty layout slot 1 binds has no entries";

    const auto& motion_entries = pipeline->get().view_layout_motion_vectors.entries;
    EXPECT_EQ(motion_entries[0].visibility, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
        << "the view uniform is visible to both stages";
    EXPECT_EQ(motion_entries[3].visibility, wgpu::ShaderStage::eVertex)
        << "the visibility ranges are vertex-only";

    EXPECT_EQ(pipeline->get().skins_use_uniform_buffers, mesh_pipeline.skins_use_uniform_buffers);
    EXPECT_EQ(pipeline->get().binding_arrays_are_usable, mesh_pipeline.binding_arrays_are_usable);
    EXPECT_TRUE(pipeline->get().binding_arrays_are_usable)
        << "the flag comes from the mesh pipeline, which this test set to a value the device "
        "would not report";
    EXPECT_EQ(pipeline->get().depth_clip_control_supported,
              static_cast<bool>(device->get().hasFeature(wgpu::FeatureName::eDepthClipControl)))
        << "and this one the device does answer";
}

/** @brief The two view bind groups appear only once the buffers they bind do, which is what
 *  Bevy's guard waits for (`prepass/mod.rs:710-760`), and the empty group slot 1 binds exists
 *  from the start (`:692-708`). */
TEST(PrepassPipeline, TheViewBindGroupsWaitForTheirBuffers) {
    App application = App::create();
    add_prerequisites(application);
    try {
        application.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    auto render_app = application.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& render = render_app->get();

    const auto device = render.world().get_resource<wgpu::Device>();
    const auto queue  = render.world().get_resource<wgpu::Queue>();
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(queue.has_value());

    render.world_mut().insert_resource(make_mesh_pipeline());
    render.world_mut().insert_resource(pbr::MaterialPipeline{make_mesh_pipeline()});
    pbr::register_mesh_shaders(application.world_mut());
    pbr::register_prepass_shaders(application.world_mut());
    // The four resources the prepare system reads. In a real app the view, globals and
    // previous-view plugins create them; the prepass reads them either way.
    render.world_mut().insert_resource(render::view::ViewUniforms{});
    render.world_mut().insert_resource(render::GlobalsBuffer{});
    render.world_mut().insert_resource(epix::core_graph::PreviousViewUniforms{});
    render.world_mut().insert_resource(render::view::RenderVisibilityRanges{});

    render.add_systems(render::RenderStartup,
                       ecs::into(pbr::init_prepass_pipeline).set_name("init prepass pipeline"));
    render.add_systems(render::RenderStartup, ecs::into(pbr::init_prepass_view_bind_group)
                                                  .after(pbr::init_prepass_pipeline)
                                                  .set_name("init prepass view bind group"));
    render.add_systems(render::RenderStartup, ecs::into(pbr::prepare_prepass_view_bind_group)
                                                  .after(pbr::init_prepass_view_bind_group)
                                                  .set_name("prepare prepass view bind group"));

    render.run_schedule(render::RenderStartup);
    {
        const auto bind_group = render.world().get_resource<pbr::PrepassViewBindGroup>();
        ASSERT_TRUE(bind_group.has_value()) << "init_prepass_view_bind_group inserts the resource";
        EXPECT_TRUE(static_cast<bool>(bind_group->get().empty_bind_group))
            << "the empty group needs only the layout, so it exists before any buffer does";
        EXPECT_FALSE(bind_group->get().no_motion_vectors.has_value())
            << "no bind group before the buffers it binds exist";
        EXPECT_FALSE(bind_group->get().motion_vectors.has_value());
    }

    // Now the buffers, the way their own prepare systems make them: push the values and
    // upload. The visibility-ranges buffer is a plain storage-buffer vector.
    {
        auto& world = render.world_mut();
        world.resource_mut<render::view::ViewUniforms>().uniforms.push(render::view::ViewUniform{});
        world.resource_mut<render::view::ViewUniforms>().uniforms.write_buffer(device->get(), queue->get());
        world.resource_mut<render::GlobalsBuffer>().buffer.get_mut() = render::GlobalsUniform{};
        world.resource_mut<render::GlobalsBuffer>().buffer.write_buffer(device->get(), queue->get());
        world.resource_mut<epix::core_graph::PreviousViewUniforms>().uniforms.push(
            epix::core_graph::PreviousViewData{});
        world.resource_mut<epix::core_graph::PreviousViewUniforms>().uniforms.write_buffer(device->get(),
                                                                                          queue->get());
        auto& ranges = world.resource_mut<render::view::RenderVisibilityRanges>();
        ranges.buffer.push(glm::vec4{0.0f});
        ranges.buffer.write_buffer(device->get(), queue->get());
    }

    render.run_schedule(render::RenderStartup);
    {
        const auto bind_group = render.world().get_resource<pbr::PrepassViewBindGroup>();
        ASSERT_TRUE(bind_group.has_value());
        EXPECT_TRUE(bind_group->get().no_motion_vectors.has_value())
            << "a view without motion vectors binds the layout without previous view uniforms";
        EXPECT_TRUE(bind_group->get().motion_vectors.has_value())
            << "and with them, once the previous view uniforms have a buffer";
    }
}
