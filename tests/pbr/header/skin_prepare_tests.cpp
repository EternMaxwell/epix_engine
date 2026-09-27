#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/pbr/skin.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <utility>

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

/** @brief This frame's joint matrices reach the GPU, and the buffers grow to hold
 *  them (Bevy `prepare_skins`, `render/skin.rs:199-268`).
 *
 *  What is observable from outside is the growth: the buffers start at exactly one
 *  `MAX_JOINTS`-sized binding and must end large enough for the staging data plus
 *  that same margin, since a full uniform buffer's worth has to stay bindable
 *  where skins use uniform buffers. */
TEST(SkinPrepare, UploadsTheJointMatrices) {
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

    const auto& device = world.resource<wgpu::Device>();
    wgpu::Limits limits{};
    ASSERT_EQ(device.getLimits(&limits), wgpu::Status::eSuccess);

    pbr::SkinUniforms uniforms = pbr::SkinUniforms::make(device, limits);
    const std::uint64_t initial_size = uniforms.current_buffer.getSize();
    // Eight joints is nothing, but it is two more than the margin can absorb:
    // `(8 + MAX_JOINTS)` `mat4`s do not fit in `MAX_JOINTS` of them.
    uniforms.current_staging_buffer.assign(8, glm::mat4{1.0f});
    world.insert_resource(std::move(uniforms));

    render_app->get().add_systems(render::RenderStartup,
                                  epix::ecs::into(epix::pbr::prepare_skins).set_name("prepare skins"));
    render_app->get().run_schedule(render::RenderStartup);

    const std::uint64_t uploaded_size = world.resource<pbr::SkinUniforms>().current_buffer.getSize();
    EXPECT_GE(uploaded_size, static_cast<std::uint64_t>(8u + pbr::MAX_JOINTS) * sizeof(glm::mat4))
        << "the buffer grows to hold the staging data plus the MAX_JOINTS margin";
    EXPECT_GT(uploaded_size, initial_size) << "which is larger than what it started at";
}
