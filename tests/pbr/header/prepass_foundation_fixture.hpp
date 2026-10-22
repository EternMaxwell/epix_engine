#pragma once

// Separate device-mode executables preserve Bevy's first-device skin policy.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/gpu_lights.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/pbr/prepass.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <span>
#include <thread>
#include <vector>

namespace {
namespace ecs    = epix::ecs;
namespace pbr    = epix::pbr;
namespace render = epix::render;
namespace assets = epix::assets;
namespace shader = epix::shader;
namespace mesh   = epix::mesh;

struct IoTaskPoolInit {
    IoTaskPoolInit() { epix::task::IoTaskPool::get_or_init(epix::task::TaskPoolBuilder{}.num_threads(4).build()); }
} io_task_pool_init;

class PrepassFoundation : public testing::Test {
   protected:
    epix::app::App app       = epix::app::App::create();
    ecs::World* render_world = nullptr;
    virtual bool uniform_fallback() const { return false; }
    void compile_branch_matrix(bool forward = false);
    void check_visibility_binding_sizes();
    void draw_and_check_outputs(bool visibility_probe = false, bool motion_vectors = true);
    void check_cpu_model_binding();

    template <class F>
    void run(F function) {
        auto system = ecs::make_system_unique(function);
        system->initialize(*render_world);
        ASSERT_TRUE(system->run({}, *render_world).has_value());
        system->apply_deferred(*render_world);
    }

    void SetUp() override {
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{}, epix::camera::CameraPlugin{}, assets::AssetPlugin{},
                        epix::image::ImagePlugin{}, epix::mesh::MeshPlugin{}, epix::transform::TransformPlugin{},
                        shader::ShaderPlugin{}, render::FrameCountPlugin{});
        render::WgpuSettings settings;
        if (uniform_fallback()) {
            auto constrained                            = settings.limits;
            constrained.maxStorageBuffersPerShaderStage = 0;
            settings.constrained_limits                 = constrained;
        }
        ASSERT_NO_THROW(app.add_plugins(
            render::RenderPlugin{.render_creation = render::RenderCreation::automatic(std::move(settings)),
                                 .synchronous_pipeline_compilation = true}));
        app.add_plugins(epix::core_graph::CoreGraphPlugin{},
                        pbr::PbrPlugin{.supports_storage_buffers = !uniform_fallback()});
        app.resource_scope([&](epix::app::internal::Plugins& plugins) { plugins.ready_all(app); });
        auto& render_app = app.get_sub_app_mut(render::Render)->get();
        render_world     = &render_app.world_mut();
        ASSERT_TRUE(render_app.run_schedule(render::RenderStartup));
    }
};

template <class Pipeline>
void expect_owned_pipeline(const Pipeline& pipeline, ecs::World& world) {
    // Keep the baseline executable runnable before these missing members exist.
    // The final branch verifies real state, not just declaration presence.
    if constexpr (requires {
                      pipeline.default_prepass_shader.id();
                      pipeline.material_pipeline.mesh_pipeline.shader.id();
                  }) {
        EXPECT_NE(pipeline.default_prepass_shader.id(), assets::AssetId<shader::Shader>::invalid());
        const auto source_id = world.resource<pbr::MaterialPipeline>().mesh_pipeline.shader.id();
        EXPECT_EQ(pipeline.material_pipeline.mesh_pipeline.shader.id(), source_id);
        world.resource_mut<pbr::MaterialPipeline>().mesh_pipeline.shader =
            assets::Handle<shader::Shader>{assets::AssetId<shader::Shader>::invalid()};
        EXPECT_EQ(pipeline.material_pipeline.mesh_pipeline.shader.id(), source_id)
            << "prepass must own the native cloned material pipeline, not alias the mutable resource";
    } else {
        FAIL() << "PrepassPipeline is missing its default shader and owned MaterialPipeline snapshot";
    }
}

void PrepassFoundation::check_visibility_binding_sizes() {
    wgpu::Limits limits{};
    render_world->resource<wgpu::Device>().getLimits(&limits);
    const auto expected_type =
        uniform_fallback() ? wgpu::BufferBindingType::eUniform : wgpu::BufferBindingType::eReadOnlyStorage;
    const auto expected_size =
        sizeof(glm::vec4) * (uniform_fallback() ? render::view::VISIBILITY_RANGE_UNIFORM_BUFFER_SIZE : 1u);
    if (uniform_fallback()) EXPECT_LE(expected_size, limits.maxUniformBufferBindingSize);
    const auto& forward = render_world->resource<pbr::MeshPipeline>();
    const auto& prepass = render_world->resource<pbr::PrepassPipeline>();
    for (const auto* layout : {&forward.view_layouts.layouts[0].main_layout, &prepass.view_layout_motion_vectors,
                               &prepass.view_layout_no_motion_vectors}) {
        SCOPED_TRACE(layout->label);
        const auto entry = std::ranges::find(layout->entries, 14u, &wgpu::BindGroupLayoutEntry::binding);
        ASSERT_NE(entry, layout->entries.end());
        EXPECT_EQ(entry->buffer.type, expected_type);
        EXPECT_EQ(entry->buffer.minBindingSize, expected_size);
        EXPECT_FALSE(entry->buffer.hasDynamicOffset);
        EXPECT_EQ(entry->visibility, wgpu::ShaderStage::eVertex);
    }
}

void PrepassFoundation::compile_branch_matrix(bool forward) {
    auto source      = mesh::Cuboid(1.0f, 1.0f, 1.0f).mesh().build();
    const auto count = source.count_vertices();
    source.insert_attribute(mesh::Mesh::ATTRIBUTE_UV_1,
                            mesh::VertexAttributeValues::Float32x2{
                                std::vector<mesh::VertexAttributeValues::Float32x2Value>(count, {0.0f, 0.0f})});
    source.insert_attribute(mesh::Mesh::ATTRIBUTE_TANGENT, mesh::VertexAttributeValues::Float32x4{
                                                               std::vector<mesh::VertexAttributeValues::Float32x4Value>(
                                                                   count, {1.0f, 0.0f, 0.0f, 1.0f})});
    source.insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR, mesh::VertexAttributeValues::Float32x4{
                                                             std::vector<mesh::VertexAttributeValues::Float32x4Value>(
                                                                 count, {1.0f, 1.0f, 1.0f, 1.0f})});
    mesh::MeshVertexBufferLayouts layouts;
    const auto plain = source.get_mesh_vertex_buffer_layout(layouts);
    source.insert_attribute(mesh::Mesh::ATTRIBUTE_JOINT_INDEX,
                            mesh::VertexAttributeValues::Uint16x4{
                                std::vector<mesh::VertexAttributeValues::Uint16x4Value>(count, {0, 0, 0, 0})});
    source.insert_attribute(
        mesh::Mesh::ATTRIBUTE_JOINT_WEIGHT,
        mesh::VertexAttributeValues::Float32x4{
            std::vector<mesh::VertexAttributeValues::Float32x4Value>(count, {1.0f, 0.0f, 0.0f, 0.0f})});
    const auto skinned       = source.get_mesh_vertex_buffer_layout(layouts);
    using Key                = pbr::MeshPipelineKey;
    const auto normal_motion = Key::NORMAL_PREPASS | Key::MOTION_VECTOR_PREPASS;
    struct Variant {
        const char* name;
        Key flags;
        bool skinned       = false;
        bool emulate_depth = false;
    };
    std::vector<Variant> variants{
        {"depth", Key::DEPTH_PREPASS},
        {"normal", Key::NORMAL_PREPASS},
        {"motion", Key::MOTION_VECTOR_PREPASS},
        {"deferred", Key::DEFERRED_PREPASS},
        {"normal_motion", normal_motion},
        {"all_outputs", normal_motion | Key::DEFERRED_PREPASS},
        {"emulated_unclipped_depth", Key::UNCLIPPED_DEPTH_ORTHO, false, true},
        {"skin", normal_motion, true},
        {"previous_skin", normal_motion | Key::HAS_PREVIOUS_SKIN, true},
        {"morph", normal_motion | Key::MORPH_TARGETS},
        {"previous_morph", normal_motion | Key::MORPH_TARGETS | Key::HAS_PREVIOUS_MORPH},
        {"skin_morph", normal_motion | Key::MORPH_TARGETS | Key::HAS_PREVIOUS_MORPH | Key::HAS_PREVIOUS_SKIN, true},
        {"visibility_dither", normal_motion | Key::VISIBILITY_RANGE_DITHER},
        {"lightmap_blend", normal_motion | Key::LIGHTMAPPED | Key::LIGHTMAP_BICUBIC_SAMPLING | Key::BLEND_ALPHA},
        {"msaa4", normal_motion | Key::from_msaa_samples(4)},
    };
    if (forward) variants = {{"forward_control", Key{}}, {"forward_visibility_dither", Key::VISIBILITY_RANGE_DITHER}};
    pbr::MaterialProperties properties;
    properties.material_layout =
        render::BindGroupLayoutDescriptor::create("prepass test material", std::vector<wgpu::BindGroupLayoutEntry>{});
    auto& server = app.resource<render::PipelineServer>();
    for (const auto& variant : variants) {
        SCOPED_TRACE(variant.name);
        auto pipeline = render_world->resource<pbr::PrepassPipeline>();
        if (variant.emulate_depth) pipeline.depth_clip_control_supported = false;
        const auto key     = variant.flags | Key::from_primitive_topology(wgpu::PrimitiveTopology::eTriangleList);
        const auto& layout = variant.skinned ? skinned : plain;
        auto descriptor    = forward ? render_world->resource<pbr::MeshPipeline>().specialize(key, layout)
                                     : pipeline.specialize(key, {}, layout, properties);
        ASSERT_TRUE(descriptor) << "native descriptor specialization failed";
        ASSERT_EQ(descriptor->layouts.size(), forward ? 3u : 4u);
        const auto& device = render_world->resource<wgpu::Device>();
        device.pushErrorScope(wgpu::ErrorFilter::eValidation);
        const auto id = server.queue_render_pipeline(std::move(*descriptor));
        for (unsigned attempt = 0; attempt < 400 && !server.get_render_pipeline(id); ++attempt) {
            app.run_schedule(epix::app::PreUpdate);
            app.run_schedule(epix::app::PostUpdate);
            app.run_schedule(epix::app::Last);
            std::this_thread::yield();
        }
        struct Validation {
            bool completed = false;
            wgpu::PopErrorScopeStatus status{};
            wgpu::ErrorType type{};
            std::string message;
        };
        const auto validation = std::make_shared<Validation>();
        device.popErrorScope(
            wgpu::PopErrorScopeCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::PopErrorScopeCallback(
                    [validation](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView message) {
                        validation->status = status;
                        validation->type   = type;
                        if (type != wgpu::ErrorType::eNoError)
                            validation->message = std::string(std::string_view(message));
                        validation->completed = true;
                    })));
        for (unsigned attempt = 0; attempt < 1000 && !validation->completed; ++attempt) device.poll(wgpu::Bool(true));
        EXPECT_TRUE(validation->completed);
        EXPECT_EQ(validation->status, wgpu::PopErrorScopeStatus::eSuccess);
        EXPECT_EQ(validation->type, wgpu::ErrorType::eNoError) << validation->message;
        EXPECT_TRUE(server.get_render_pipeline(id)) << "actual device rejected the shader/layout branch";
    }
}

void PrepassFoundation::draw_and_check_outputs(bool visibility_probe, bool motion_vectors) {
    const auto& device = render_world->resource<wgpu::Device>();
    const auto& queue  = render_world->resource<wgpu::Queue>();
    auto& server       = app.resource<render::PipelineServer>();
    mesh::Mesh triangle{wgpu::PrimitiveTopology::eTriangleList, assets::RenderAssetUsages::MAIN_WORLD};
    triangle.insert_attribute(
        mesh::Mesh::ATTRIBUTE_POSITION,
        mesh::VertexAttributeValues::Float32x3{{{-0.75f, -0.75f, 0.5f}, {0.75f, -0.75f, 0.5f}, {0.0f, 0.75f, 0.5f}}});
    triangle.insert_attribute(
        mesh::Mesh::ATTRIBUTE_NORMAL,
        mesh::VertexAttributeValues::Float32x3{{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}});
    mesh::MeshVertexBufferLayouts layouts;
    const auto vertex_layout = triangle.get_mesh_vertex_buffer_layout(layouts);
    pbr::MaterialProperties properties;
    properties.material_layout = render::BindGroupLayoutDescriptor::create("prepass readback material",
                                                                           std::vector<wgpu::BindGroupLayoutEntry>{});
    if (visibility_probe) {
        const auto fragment = app.resource_mut<assets::Assets<shader::Shader>>().emplace(shader::Shader::from_slang(
            "import epix.prepass_io;\n"
            "[shader(\"fragment\")] epix::prepass_io::FragmentOutput fragment(epix::prepass_io::VertexOutput input) {\n"
            "    epix::prepass_io::FragmentOutput output = {};\n"
            "    output.normal = float4(float(input.visibility_range_dither) / 16.0, 0.25, 0.75, 1.0);\n"
            "    return output;\n"
            "}\n",
            "prepass/visibility_range_probe.slang"));
        properties.shaders.emplace_back(ecs::intern(render::phase::ShaderLabel{pbr::DeferredFragmentShader{}}),
                                        fragment);
    }
    using Key             = pbr::MeshPipelineKey;
    const auto descriptor = render_world->resource<pbr::PrepassPipeline>().specialize(
        Key::DEPTH_PREPASS | Key::NORMAL_PREPASS | Key::DEFERRED_PREPASS |
            (motion_vectors ? Key::MOTION_VECTOR_PREPASS : Key{}) |
            (visibility_probe ? Key::VISIBILITY_RANGE_DITHER : Key{}) |
            Key::from_primitive_topology(wgpu::PrimitiveTopology::eTriangleList),
        {}, vertex_layout, properties);
    ASSERT_TRUE(descriptor);
    const auto validation_ok = [&] {
        struct Result {
            bool done  = false;
            bool valid = false;
            std::string message;
        };
        const auto result = std::make_shared<Result>();
        device.popErrorScope(
            wgpu::PopErrorScopeCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::PopErrorScopeCallback([result](wgpu::PopErrorScopeStatus status,
                                                                  wgpu::ErrorType type, wgpu::StringView message) {
                    result->done  = true;
                    result->valid = status == wgpu::PopErrorScopeStatus::eSuccess && type == wgpu::ErrorType::eNoError;
                    if (!result->valid) result->message = std::string(std::string_view(message));
                })));
        for (unsigned attempt = 0; attempt < 1000 && !result->done; ++attempt) device.poll(wgpu::Bool(true));
        EXPECT_TRUE(result->done);
        EXPECT_TRUE(result->valid) << result->message;
        return result->valid;
    };
    device.pushErrorScope(wgpu::ErrorFilter::eValidation);
    const auto id = server.queue_render_pipeline(*descriptor);
    for (unsigned attempt = 0; attempt < 400 && !server.get_render_pipeline(id); ++attempt) {
        app.run_schedule(epix::app::PreUpdate);
        app.run_schedule(epix::app::PostUpdate);
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    }
    ASSERT_TRUE(validation_ok());
    const auto pipeline = server.get_render_pipeline(id);
    ASSERT_TRUE(pipeline);
    device.pushErrorScope(wgpu::ErrorFilter::eValidation);
    render::view::ViewUniform view{};
    view.clip_from_world            = glm::mat4(1.0f);
    view.unjittered_clip_from_world = glm::mat4(1.0f);
    view.world_position             = glm::vec3(0.0f, 0.0f, 3.0f);
    auto& view_buffer               = render_world->resource_mut<render::view::ViewUniforms>().uniforms;
    view_buffer.clear();
    view_buffer.push(render::view::ViewUniform{});
    const auto view_offset = static_cast<std::uint32_t>(view_buffer.push(view));
    view_buffer.write_buffer(device, queue);
    auto& globals     = render_world->resource_mut<render::GlobalsBuffer>().buffer;
    globals.get_mut() = render::GlobalsUniform{};
    globals.write_buffer(device, queue);
    epix::core_graph::PreviousViewData previous{};
    previous.clip_from_world    = glm::mat4(1.0f);
    previous.clip_from_world[3] = glm::vec4(-0.25f, 0.5f, 0.0f, 1.0f);
    auto& previous_buffer       = render_world->resource_mut<epix::core_graph::PreviousViewUniforms>().uniforms;
    previous_buffer.clear();
    previous_buffer.push(epix::core_graph::PreviousViewData{});
    const auto previous_offset = static_cast<std::uint32_t>(previous_buffer.push(previous));
    previous_buffer.write_buffer(device, queue);
    pbr::MeshUniform instance{};
    instance.world_from_local             = pbr::to_transpose(glm::mat4(1.0f));
    instance.previous_world_from_local    = instance.world_from_local;
    const auto normal                     = pbr::inverse_transpose_3x3(glm::mat4(1.0f));
    instance.local_from_world_transpose_a = normal.first;
    instance.local_from_world_transpose_b = normal.second;
    instance.flags                        = 1u;
    wgpu::Limits limits{};
    device.getLimits(&limits);
    render::batching::BatchedInstanceBuffer<pbr::MeshUniform> instance_buffers{limits};
    auto& model                        = instance_buffers.buffer;
    const auto batch_size              = render::render_resource::GpuArrayBuffer<pbr::MeshUniform>::batch_size(limits);
    auto sentinel                      = instance;
    auto outside                       = glm::mat4(1.0f);
    outside[3].x                       = 10.0f;
    sentinel.world_from_local          = pbr::to_transpose(outside);
    sentinel.previous_world_from_local = sentinel.world_from_local;
    for (unsigned i = 0; i <= batch_size.value_or(1u); ++i) model.push(sentinel);
    const auto model_index = model.push(instance);
    model.write_buffer(device, queue);
    const auto model_buffer = instance_buffers.instance_data_binding();
    ASSERT_TRUE(model_buffer);
    EXPECT_EQ(model_buffer->offset, 0u);
    if (batch_size) {
        ASSERT_TRUE(model_index.dynamic_offset);
        EXPECT_GT(*model_index.dynamic_offset, 0u);
        EXPECT_EQ(model_index.index, 1u);
        EXPECT_EQ(model_buffer->size,
                  std::get<render::render_resource::BatchedUniformBuffer<pbr::MeshUniform>>(model.storage).size());
        EXPECT_GT(model_buffer->buffer.getSize(), limits.maxUniformBufferBindingSize);
    } else {
        EXPECT_FALSE(model_index.dynamic_offset);
        EXPECT_EQ(model_index.index, 2u);
        EXPECT_FALSE(model_buffer->size);
    }
    auto& visibility = render_world->resource_mut<render::view::RenderVisibilityRanges>();
    visibility.buffer.clear();
    visibility.buffer.push(glm::vec4(0.0f, 1.0f, 10.0f, 11.0f));
    visibility.buffer.push(glm::vec4(0.0f, 1.0f, 2.0f, 4.0f));
    visibility.buffer_dirty = true;
    run(render::view::detail::write_render_visibility_ranges);
    run(pbr::prepare_prepass_view_bind_group);
    const auto& prepared_groups = render_world->resource<pbr::PrepassViewBindGroup>();
    const auto view_group       = motion_vectors ? prepared_groups.motion_vectors : prepared_groups.no_motion_vectors;
    ASSERT_TRUE(view_group);
    const auto group = [&](unsigned index, std::vector<wgpu::BindGroupEntry> entries) {
        return device.createBindGroup(wgpu::BindGroupDescriptor()
                                          .setLayout(server.get_bind_group_layout(descriptor->layouts[index]))
                                          .setEntries(entries));
    };
    const auto empty = group(1, {});
    const auto mesh_group =
        render_world->resource<pbr::PrepassPipeline>().mesh_layouts.create_model_only(device, server, *model_buffer);
    const auto material = group(3, {});
    const auto vertices = triangle.create_packed_vertex_buffer_data();
    const auto vertex_buffer =
        device.createBuffer(wgpu::BufferDescriptor()
                                .setSize(vertices.size())
                                .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst));
    queue.writeBuffer(vertex_buffer, 0, vertices.data(), vertices.size());
    std::array<wgpu::Texture, 5> textures;
    std::array<wgpu::TextureView, 5> views;
    std::vector<wgpu::RenderPassColorAttachment> attachments;
    ASSERT_TRUE(descriptor->fragment);
    ASSERT_EQ(descriptor->fragment->targets.size(), 4u);
    for (unsigned i = 0; i < 5; ++i) {
        const auto format = i < 4 ? descriptor->fragment->targets[i].format : descriptor->depth_stencil->format;
        if (format == wgpu::TextureFormat::eUndefined) {
            attachments.emplace_back();
            continue;
        }
        textures[i] =
            device.createTexture(wgpu::TextureDescriptor()
                                     .setDimension(wgpu::TextureDimension::e2D)
                                     .setSize({8, 8, 1})
                                     .setFormat(format)
                                     .setMipLevelCount(1)
                                     .setSampleCount(1)
                                     .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc));
        views[i] = textures[i].createView();
        if (i < 4)
            attachments.push_back(wgpu::RenderPassColorAttachment()
                                      .setView(views[i])
                                      .setLoadOp(wgpu::LoadOp::eClear)
                                      .setStoreOp(wgpu::StoreOp::eStore)
                                      .setClearValue(wgpu::Color(0, 0, 0, 0)));
    }
    const auto encoder = device.createCommandEncoder();
    const auto pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor()
                                                  .setColorAttachments(attachments)
                                                  .setDepthStencilAttachment(wgpu::RenderPassDepthStencilAttachment()
                                                                                 .setView(views[4])
                                                                                 .setDepthLoadOp(wgpu::LoadOp::eClear)
                                                                                 .setDepthStoreOp(wgpu::StoreOp::eStore)
                                                                                 .setDepthClearValue(0.0f)));
    pass.setPipeline(pipeline->get().pipeline());
    if (motion_vectors)
        pass.setBindGroup(0, *view_group, std::array<std::uint32_t, 2>{view_offset, previous_offset});
    else
        pass.setBindGroup(0, *view_group, std::array<std::uint32_t, 1>{view_offset});
    // An untyped {} selects the wrapper's one-scalar-offset overload.
    const std::span<const std::uint32_t> no_offsets{};
    pass.setBindGroup(1, empty, no_offsets);
    if (model_index.dynamic_offset)
        pass.setBindGroup(2, mesh_group, std::array<std::uint32_t, 1>{*model_index.dynamic_offset});
    else
        pass.setBindGroup(2, mesh_group, no_offsets);
    pass.setBindGroup(3, material, no_offsets);
    pass.setVertexBuffer(0, vertex_buffer, 0, vertices.size());
    pass.draw(3, 1, 0, model_index.index);
    pass.end();
    constexpr unsigned region_size = 256 * 8;
    const auto readback = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setSize(region_size * 5)
                                                  .setUsage(wgpu::BufferUsage::eMapRead | wgpu::BufferUsage::eCopyDst));
    for (unsigned i = 0; i < 5; ++i) {
        if (!textures[i]) continue;
        encoder.copyTextureToBuffer(
            wgpu::TexelCopyTextureInfo()
                .setTexture(textures[i])
                .setAspect(i == 4 ? wgpu::TextureAspect::eDepthOnly : wgpu::TextureAspect::eAll),
            wgpu::TexelCopyBufferInfo().setBuffer(readback).setLayout(
                wgpu::TexelCopyBufferLayout().setOffset(region_size * i).setBytesPerRow(256).setRowsPerImage(8)),
            {8, 8, 1});
    }
    const auto commands = encoder.finish();
    ASSERT_TRUE(validation_ok());
    device.pushErrorScope(wgpu::ErrorFilter::eValidation);
    queue.submit(commands);
    ASSERT_TRUE(validation_ok());
    struct Pixels {
        std::array<std::uint8_t, region_size * 5> data{};
        bool done   = false;
        bool mapped = false;
    };
    const auto pixels = std::make_shared<Pixels>();
    readback.mapAsync(
        wgpu::MapMode::eRead, 0, pixels->data.size(),
        wgpu::BufferMapCallbackInfo()
            .setMode(wgpu::CallbackMode::eAllowProcessEvents)
            .setCallback(wgpu::BufferMapCallback([pixels, readback](wgpu::MapAsyncStatus status, wgpu::StringView) {
                pixels->done = true;
                if (status != wgpu::MapAsyncStatus::eSuccess) return;
                std::memcpy(pixels->data.data(), readback.getConstMappedRange(0, pixels->data.size()),
                            pixels->data.size());
                readback.unmap();
                pixels->mapped = true;
            })));
    for (unsigned attempt = 0; attempt < 1000 && !pixels->done; ++attempt) device.poll(wgpu::Bool(true));
    ASSERT_TRUE(pixels->mapped);
    const auto word = [&](unsigned region, unsigned x, unsigned y, unsigned bytes_per_pixel, unsigned word_index = 0) {
        std::uint32_t value;
        std::memcpy(&value, pixels->data.data() + region * region_size + y * 256 + x * bytes_per_pixel + word_index * 4,
                    4);
        return value;
    };
    const auto packed_normal = word(0, 4, 4, 4);
    EXPECT_NEAR(float(packed_normal & 1023u) / 1023.0f, 0.5f, 1.0f / 1023.0f);
    EXPECT_NEAR(float((packed_normal >> 10) & 1023u) / 1023.0f, visibility_probe ? 0.25f : 0.5f, 1.0f / 1023.0f);
    EXPECT_NEAR(float((packed_normal >> 20) & 1023u) / 1023.0f, visibility_probe ? 0.75f : 1.0f, 1.0f / 1023.0f);
    EXPECT_EQ(packed_normal >> 30, 3u);
    EXPECT_EQ(word(1, 4, 4, 4), visibility_probe || !motion_vectors ? 0u : 0x34003000u)
        << "native UV motion is (+0.125,+0.25), including Y inversion";
    EXPECT_EQ(word(2, 4, 4, 16, 0), 0u);
    EXPECT_EQ(word(2, 4, 4, 16, 1), visibility_probe ? 0u : 0x84000100u)
        << "native default deferred shader packs emissive magenta RGB9E5";
    EXPECT_EQ(word(2, 4, 4, 16, 2), 0u);
    EXPECT_EQ(word(2, 4, 4, 16, 3), 0u);
    EXPECT_EQ(pixels->data[3 * region_size + 4 * 256 + 4], visibility_probe ? 0u : 1u);
    EXPECT_EQ(word(4, 4, 4, 4), 0x3f000000u) << "reverse-depth target receives 0.5";
    for (const auto region : {0u, 1u, 2u, 4u}) EXPECT_EQ(word(region, 0, 0, region == 2 ? 16 : 4), 0u);
    EXPECT_EQ(pixels->data[3 * region_size], 0u);
}

void PrepassFoundation::check_cpu_model_binding() {
    const auto& device = render_world->resource<wgpu::Device>();
    const auto& queue  = render_world->resource<wgpu::Queue>();
    wgpu::Limits limits{};
    ASSERT_EQ(device.getLimits(&limits), wgpu::Status::eSuccess);
    render::batching::BatchedInstanceBuffer<pbr::MeshUniform> instances{limits};
    EXPECT_FALSE(instances.instance_data_binding());
    const auto batch = render::render_resource::GpuArrayBuffer<pbr::MeshUniform>::batch_size(limits);
    for (unsigned i = 0; i <= batch.value_or(1u); ++i) instances.buffer.push(pbr::MeshUniform{});
    instances.buffer.write_buffer(device, queue);
    const auto binding = instances.instance_data_binding();
    ASSERT_TRUE(binding) << "native CPU batching forwards both storage and uniform bindings after upload";
    EXPECT_TRUE(binding->buffer);
    EXPECT_EQ(binding->offset, 0u);
    if (batch) {
        ASSERT_TRUE(binding->size);
        EXPECT_EQ(*binding->size, *batch * sizeof(pbr::MeshUniform));
        EXPECT_GT(binding->buffer.getSize(), *binding->size);
    } else
        EXPECT_FALSE(binding->size);
    const auto entry = binding->entry(7u);
    EXPECT_EQ(entry.binding, 7u);
    EXPECT_EQ(entry.offset, binding->offset);
    EXPECT_EQ(entry.size, binding->size.value_or(std::numeric_limits<std::uint64_t>::max()));
    const auto layout = render::render_resource::GpuArrayBuffer<pbr::MeshUniform>::binding_layout(limits);
    EXPECT_EQ(layout.minBindingSize, batch ? 0u : sizeof(pbr::MeshUniform));
    EXPECT_EQ(static_cast<bool>(layout.hasDynamicOffset), batch.has_value());
}

}  // namespace
