#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/pbr/mesh_bindings.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <exception>

using namespace epix::app;

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

/** @brief A buffer that satisfies either form of the model or joint binding: both
 *  usages and a size at least as large as the layout's minimum. */
wgpu::Buffer make_binding_buffer(const wgpu::Device& device, const char* label, std::uint64_t size) {
    return device.createBuffer(wgpu::BufferDescriptor()
                                   .setLabel(label)
                                   .setSize(size)
                                   .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eStorage |
                                             wgpu::BufferUsage::eCopyDst));
}

}  // namespace

/** @brief Every mesh bind group the layouts describe can be created from real
 *  resources (Bevy `MeshLayouts`' bind group methods,
 *  `bevy_pbr/src/render/mesh_bindings.rs:370-562`).
 *
 *  Creation is the check: wgpu validates each entry's binding number, resource
 *  type and minimum size against the layout, so a wrong entry fails the group
 *  rather than passing silently, and the handle is null when it does. Skips
 *  without a device, like the other GPU tests. */
TEST(MeshBindGroups, EveryVariantIsCreatedFromItsLayout) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::view::ViewPlugin{})
        .add_plugins(render::GlobalsPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world = render_app->get().world_mut();

    const auto& device          = world.resource<wgpu::Device>();
    const auto& pipeline_server = world.resource<render::PipelineServer>();

    const wgpu::Limits limits{};
    const auto layouts = pbr::MeshLayouts::make(limits, /*binding_arrays_are_usable=*/false);
    const bool uniform_skins = pbr::skins_use_uniform_buffers(limits);

    const auto model = make_binding_buffer(device, "model", sizeof(pbr::MeshUniform) * 2);
    const auto joints = make_binding_buffer(device, "joints", pbr::JOINT_BUFFER_SIZE);
    const auto weights = make_binding_buffer(device, "weights", pbr::MORPH_BUFFER_SIZE);
    const auto targets_texture = device.createTexture(wgpu::TextureDescriptor()
                                                          .setLabel("morph targets")
                                                          // The morph targets are a 3D texture: the shader reads
                                                          // a vertex's components out of one layer per weight.
                                                          .setDimension(wgpu::TextureDimension::e3D)
                                                          .setSize(wgpu::Extent3D{1, 1, 1})
                                                          .setFormat(wgpu::TextureFormat::eRGBA8Unorm)
                                                          .setUsage(wgpu::TextureUsage::eTextureBinding |
                                                                    wgpu::TextureUsage::eCopyDst));
    const wgpu::TextureViewDescriptor targets_descriptor{};
    const auto targets = targets_texture.createView(targets_descriptor);

    EXPECT_TRUE(layouts.create_model_only(device, pipeline_server, model));
    EXPECT_TRUE(layouts.create_skinned(device, pipeline_server, uniform_skins, model, joints));
    EXPECT_TRUE(layouts.create_skinned_motion(device, pipeline_server, uniform_skins, model, joints, joints));
    EXPECT_TRUE(layouts.create_morphed(device, pipeline_server, model, weights, targets));
    EXPECT_TRUE(layouts.create_morphed_motion(device, pipeline_server, model, weights, targets, weights));
    EXPECT_TRUE(layouts.create_morphed_skinned(device, pipeline_server, uniform_skins, model, joints, weights, targets));
    EXPECT_TRUE(layouts.create_morphed_skinned_motion(device, pipeline_server, uniform_skins, model, joints, weights,
                                                      targets, joints, weights));
}
