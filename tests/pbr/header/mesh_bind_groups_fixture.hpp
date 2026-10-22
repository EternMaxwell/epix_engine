#pragma once

// Share setup without mixing native process-wide skin-buffer choices.
#include <gtest/gtest.h>

#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/pbr/mesh_bindings.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <exception>
#include <memory>
#include <string>

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
    return device.createBuffer(wgpu::BufferDescriptor().setLabel(label).setSize(size).setUsage(
        wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
}

}  // namespace

/** @brief Every mesh bind group the layouts describe can be created from real
 *  resources (Bevy `MeshLayouts`' bind group methods,
 *  `bevy_pbr/src/render/mesh_bindings.rs:370-562`).
 *
 *  Creation is the check: wgpu validates each entry's binding number, resource
 *  type and minimum size against the layout, so a wrong entry fails the group
 *  rather than passing silently. Check a validation scope as well: a non-null
 *  native error handle is not proof that creation succeeded. */
static void verify_mesh_bind_groups(bool uniform_model) {
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
        render::WgpuSettings settings;
        if (uniform_model) {
            auto constrained                            = settings.limits;
            constrained.maxStorageBuffersPerShaderStage = 0;
            settings.constrained_limits                 = constrained;
        }
        app.add_plugins(render::RenderPlugin{.render_creation = render::RenderCreation::automatic(std::move(settings)),
                                             .synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world = render_app->get().world_mut();

    const auto& device          = world.resource<wgpu::Device>();
    const auto& pipeline_server = world.resource<render::PipelineServer>();

    // The layouts are built from the device's real limits: the binding-array
    // entries depend on them, so a zeroed Limits would describe single-item
    // bindings while the bindless group binds an array.
    wgpu::Limits limits{};
    ASSERT_EQ(device.getLimits(&limits), wgpu::Status::eSuccess);
    if (uniform_model) ASSERT_EQ(limits.maxStorageBuffersPerShaderStage, 0u);
    const auto layouts       = pbr::MeshLayouts::make(limits, /*binding_arrays_are_usable=*/false);
    const bool uniform_skins = pbr::skins_use_uniform_buffers(limits);

    device.pushErrorScope(wgpu::ErrorFilter::eValidation);
    render::render_resource::GpuArrayBuffer<pbr::MeshUniform> instances{limits};
    if (uniform_model) {
        const auto batch = instances.batch_size(limits);
        ASSERT_TRUE(batch);
        for (unsigned i = 0; i <= *batch; ++i) instances.push(pbr::MeshUniform{});
        instances.write_buffer(device, world.resource<wgpu::Queue>());
        ASSERT_TRUE(instances.binding());
        EXPECT_GT(instances.binding()->buffer.getSize(), limits.maxUniformBufferBindingSize);
    }
    const auto model   = uniform_model ? *instances.binding()
                                       : render::render_resource::BufferBinding{
                                             make_binding_buffer(device, "model", sizeof(pbr::MeshUniform) * 2)};
    const auto joints  = make_binding_buffer(device, "joints", pbr::JOINT_BUFFER_SIZE);
    const auto weights = make_binding_buffer(device, "weights", pbr::MORPH_BUFFER_SIZE);
    const auto targets_texture =
        device.createTexture(wgpu::TextureDescriptor()
                                 .setLabel("morph targets")
                                 // The morph targets are a 3D texture: the shader reads
                                 // a vertex's components out of one layer per weight.
                                 .setDimension(wgpu::TextureDimension::e3D)
                                 .setSize(wgpu::Extent3D{1, 1, 1})
                                 .setFormat(wgpu::TextureFormat::eRGBA8Unorm)
                                 .setUsage(wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopyDst));
    const wgpu::TextureViewDescriptor targets_descriptor{};
    const auto targets = targets_texture.createView(targets_descriptor);

    EXPECT_TRUE(layouts.create_model_only(device, pipeline_server, model));
    EXPECT_TRUE(layouts.create_skinned(device, pipeline_server, uniform_skins, model, joints));
    EXPECT_TRUE(layouts.create_skinned_motion(device, pipeline_server, uniform_skins, model, joints, joints));
    EXPECT_TRUE(layouts.create_morphed(device, pipeline_server, model, weights, targets));
    EXPECT_TRUE(layouts.create_morphed_motion(device, pipeline_server, model, weights, targets, weights));
    EXPECT_TRUE(
        layouts.create_morphed_skinned(device, pipeline_server, uniform_skins, model, joints, weights, targets));
    EXPECT_TRUE(layouts.create_morphed_skinned_motion(device, pipeline_server, uniform_skins, model, joints, weights,
                                                      targets, joints, weights));

    // The lightmapped layout, both branches of Bevy's method: the slab's first
    // slot as an ordinary view and sampler, and the slab's whole binding array.
    // The slab takes the render world's fallback images, which RenderPlugin has
    // already uploaded, so its slots hold real views and samplers.
    const auto& fallback_images = world.resource<render::texture::FallbackImage>();
    const auto lightmap_slab    = pbr::LightmapSlab::make(fallback_images, /*bindless_supported=*/true);

    // What the two layouts declare, before any interning or bind group is
    // involved: the array size is the difference between them.
    ASSERT_EQ(layouts.lightmapped.entries[1].bindingArraySize, 0u);
    const auto bindless_layouts = pbr::MeshLayouts::make(limits, /*binding_arrays_are_usable=*/true);
    ASSERT_EQ(bindless_layouts.lightmapped.entries[1].bindingArraySize, 4u);

    // Only the single-item group is created here. The bindless layout needs the
    // device's binding-array limit, which wgpu-native keeps in its native limits
    // struct: this adapter reports the texture-binding-array feature but a limit
    // of zero, and creating the layout then fails with "limit is 0, count was 8".
    // The array entries themselves are covered by Lightmap.* and by the layout
    // assertions above; the group creation is exercised on a device that can
    // express arrays.
    EXPECT_TRUE(layouts.create_lightmapped(device, pipeline_server, model, lightmap_slab,
                                           /*bindless_lightmaps=*/false));
    struct Validation {
        bool done  = false;
        bool valid = false;
        std::string message;
    };
    const auto validation = std::make_shared<Validation>();
    device.popErrorScope(
        wgpu::PopErrorScopeCallbackInfo()
            .setMode(wgpu::CallbackMode::eAllowProcessEvents)
            .setCallback(wgpu::PopErrorScopeCallback([validation](wgpu::PopErrorScopeStatus status,
                                                                  wgpu::ErrorType type, wgpu::StringView message) {
                validation->done  = true;
                validation->valid = status == wgpu::PopErrorScopeStatus::eSuccess && type == wgpu::ErrorType::eNoError;
                if (!validation->valid) validation->message = std::string(std::string_view(message));
            })));
    for (unsigned attempt = 0; attempt < 1000 && !validation->done; ++attempt) device.poll(wgpu::Bool(true));
    EXPECT_TRUE(validation->done);
    EXPECT_TRUE(validation->valid) << validation->message;
}
