#include "prepass_foundation_fixture.hpp"

namespace {
TEST_F(PrepassFoundation, SharedPluginPublishesRequiredPrepassShaders) {
    const auto& server = app.resource<assets::AssetServer>();
    for (const auto* path :
         {"embedded://pbr/prepass.slang", "embedded://pbr/prepass_io.slang", "embedded://pbr/prepass_utils.slang"}) {
        EXPECT_TRUE(server.get_handle<shader::Shader>(assets::AssetPath{path}).has_value()) << path;
    }
    // Existing library is a positive control, not proof of the missing root/IO.
    EXPECT_TRUE(
        server.get_handle<shader::Shader>(assets::AssetPath{"embedded://pbr/prepass_bindings.slang"}).has_value());
}

TEST_F(PrepassFoundation, PipelineOwnsDefaultShaderAndMaterialSnapshot) {
    ASSERT_TRUE(render_world->get_resource<pbr::PrepassPipeline>().has_value());
    expect_owned_pipeline(render_world->resource<pbr::PrepassPipeline>(), *render_world);
}

TEST_F(PrepassFoundation, LibraryAssetsOwnTheirLoadingSettingsWithoutMutatingDescriptors) {
    const auto& handles = app.resource<pbr::MeshShaderHandles>();
    const auto& shaders = app.resource<assets::Assets<shader::Shader>>();
    for (unsigned attempt = 0;
         attempt < 400 && (!shaders.get(handles.mesh_view_types.id()) || !shaders.get(handles.mesh_bindings.id()));
         ++attempt) {
        app.run_schedule(epix::app::PreUpdate);
        app.run_schedule(epix::app::PostUpdate);
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    }
    const auto types = shaders.get(handles.mesh_view_types.id());
    ASSERT_TRUE(types);
    const std::vector expected{
        shader::ShaderDefVal::from_uint("MAX_DIRECTIONAL_LIGHTS",
                                        static_cast<std::uint32_t>(pbr::MAX_DIRECTIONAL_LIGHTS)),
        shader::ShaderDefVal::from_uint("MAX_CASCADES_PER_LIGHT",
                                        static_cast<std::uint32_t>(pbr::MAX_CASCADES_PER_LIGHT)),
    };
    EXPECT_EQ(types->get().shader_defs, expected);
    const auto bindings = shaders.get(handles.mesh_bindings.id());
    ASSERT_TRUE(bindings);
    wgpu::Limits limits{};
    render_world->resource<wgpu::Device>().getLimits(&limits);
    const auto batch = render::render_resource::GpuArrayBuffer<pbr::MeshUniform>::batch_size(limits);
    std::vector<shader::ShaderDefVal> expected_bindings;
    if (batch) expected_bindings.push_back(shader::ShaderDefVal::from_uint("PER_OBJECT_BUFFER_BATCH_SIZE", *batch));
    EXPECT_EQ(bindings->get().shader_defs, expected_bindings);
}

TEST_F(PrepassFoundation, DeviceReadOnlyStorageContractReachesForwardAndPrepassLayouts) {
    wgpu::Limits limits{};
    render_world->resource<wgpu::Device>().getLimits(&limits);
    const auto& forward     = render_world->resource<pbr::MeshPipeline>();
    const auto cluster_type = limits.maxStorageBuffersPerShaderStage >= pbr::CLUSTERED_FORWARD_STORAGE_BUFFER_COUNT
                                  ? wgpu::BufferBindingType::eReadOnlyStorage
                                  : wgpu::BufferBindingType::eUniform;
    const auto visibility_type =
        limits.maxStorageBuffersPerShaderStage >= render::view::VISIBILITY_RANGES_STORAGE_BUFFER_COUNT
            ? wgpu::BufferBindingType::eReadOnlyStorage
            : wgpu::BufferBindingType::eUniform;
    EXPECT_EQ(forward.clustered_forward_buffer_binding_type, cluster_type);
    const auto expect_binding = [](const render::BindGroupLayoutDescriptor& layout, unsigned binding,
                                   wgpu::BufferBindingType expected) {
        const auto entry = std::ranges::find(layout.entries, binding, &wgpu::BindGroupLayoutEntry::binding);
        ASSERT_NE(entry, layout.entries.end());
        EXPECT_EQ(entry->buffer.type, expected) << "binding " << binding;
    };
    const auto& forward_layout = forward.view_layouts.layouts[0].main_layout;
    for (const auto binding : {8u, 9u, 10u}) expect_binding(forward_layout, binding, cluster_type);
    expect_binding(forward_layout, 14u, visibility_type);
    const auto& prepass = render_world->resource<pbr::PrepassPipeline>();
    expect_binding(prepass.view_layout_motion_vectors, 14u, visibility_type);
    expect_binding(prepass.view_layout_no_motion_vectors, 14u, visibility_type);
    check_visibility_binding_sizes();
}

TEST_F(PrepassFoundation, ShaderOverridesSelectPrepassOrDeferredWithoutUsingForwardHooks) {
    auto& shaders = app.resource_mut<assets::Assets<shader::Shader>>();
    std::vector<assets::Handle<shader::Shader>> handles;
    handles.reserve(6);
    for (unsigned i = 0; i < 6; ++i)
        handles.push_back(shaders.emplace(shader::Shader::from_slang(
            "[shader(\"vertex\")] float4 vertex() : SV_Position { return float4(0,0,0.5,1); }\n"
            "[shader(\"fragment\")] float4 fragment() : SV_Target0 { return float4(1); }\n",
            assets::AssetPath{"prepass/hooks" + std::to_string(i) + ".slang"})));
    pbr::MaterialProperties properties;
    properties.material_layout =
        render::BindGroupLayoutDescriptor::create("prepass hook material", std::vector<wgpu::BindGroupLayoutEntry>{});
    properties.shaders = {
        {ecs::intern(render::phase::ShaderLabel{pbr::MaterialVertexShader{}}), handles[0]},
        {ecs::intern(render::phase::ShaderLabel{pbr::MaterialFragmentShader{}}), handles[1]},
        {ecs::intern(render::phase::ShaderLabel{pbr::PrepassVertexShader{}}), handles[2]},
        {ecs::intern(render::phase::ShaderLabel{pbr::PrepassFragmentShader{}}), handles[3]},
        {ecs::intern(render::phase::ShaderLabel{pbr::DeferredVertexShader{}}), handles[4]},
        {ecs::intern(render::phase::ShaderLabel{pbr::DeferredFragmentShader{}}), handles[5]},
    };
    mesh::MeshVertexBufferLayouts layouts;
    const auto layout    = mesh::Cuboid(1, 1, 1).mesh().build().get_mesh_vertex_buffer_layout(layouts);
    const auto& pipeline = render_world->resource<pbr::PrepassPipeline>();
    const auto prepass   = pipeline.specialize(pbr::MeshPipelineKey::NORMAL_PREPASS, {}, layout, properties);
    ASSERT_TRUE(prepass);
    EXPECT_EQ(prepass->vertex.shader, handles[2]);
    ASSERT_TRUE(prepass->fragment);
    EXPECT_EQ(prepass->fragment->shader, handles[3]);
    const auto deferred = pipeline.specialize(pbr::MeshPipelineKey::DEFERRED_PREPASS, {}, layout, properties);
    ASSERT_TRUE(deferred);
    EXPECT_EQ(deferred->vertex.shader, handles[4]);
    ASSERT_TRUE(deferred->fragment);
    EXPECT_EQ(deferred->fragment->shader, handles[5]);
    const auto depth = pipeline.specialize(pbr::MeshPipelineKey::DEPTH_PREPASS, {}, layout, properties);
    ASSERT_TRUE(depth);
    EXPECT_EQ(depth->vertex.shader, handles[2]);
    EXPECT_FALSE(depth->fragment) << "an override alone does not add a depth-only fragment stage";
    const auto mask = pipeline.specialize(pbr::MeshPipelineKey::DEPTH_PREPASS | pbr::MeshPipelineKey::MAY_DISCARD, {},
                                          layout, properties);
    ASSERT_TRUE(mask);
    ASSERT_TRUE(mask->fragment);
    EXPECT_EQ(mask->fragment->shader, handles[3]);
    EXPECT_TRUE(mask->fragment->targets.empty());
}

TEST_F(PrepassFoundation, DeferredOverridesDoNotReplaceMissingPrepassHooks) {
    const auto& pipeline = render_world->resource<pbr::PrepassPipeline>();
    pbr::MaterialProperties properties;
    properties.material_layout = render::BindGroupLayoutDescriptor::create("prepass default hook material",
                                                                           std::vector<wgpu::BindGroupLayoutEntry>{});
    const auto other           = app.resource_mut<assets::Assets<shader::Shader>>().emplace(shader::Shader::from_slang(
        "[shader(\"vertex\")] float4 vertex() : SV_Position { return float4(0,0,0.5,1); }", "prepass/deferred.slang"));
    properties.shaders         = {
        {ecs::intern(render::phase::ShaderLabel{pbr::DeferredVertexShader{}}), other},
        {ecs::intern(render::phase::ShaderLabel{pbr::DeferredFragmentShader{}}), other},
    };
    mesh::MeshVertexBufferLayouts layouts;
    const auto layout = mesh::Cuboid(1, 1, 1).mesh().build().get_mesh_vertex_buffer_layout(layouts);
    const auto normal = pipeline.specialize(pbr::MeshPipelineKey::NORMAL_PREPASS, {}, layout, properties);
    ASSERT_TRUE(normal);
    EXPECT_EQ(normal->vertex.shader, pipeline.default_prepass_shader);
    ASSERT_TRUE(normal->fragment);
    EXPECT_EQ(normal->fragment->shader, pipeline.default_prepass_shader);
    const auto depth_mask = pipeline.specialize(pbr::MeshPipelineKey::DEPTH_PREPASS | pbr::MeshPipelineKey::MAY_DISCARD,
                                                {}, layout, properties);
    ASSERT_TRUE(depth_mask);
    EXPECT_FALSE(depth_mask->fragment);
}

TEST_F(PrepassFoundation, UnclippedDepthEmulationAddsFragmentOnlyWithoutHardwareSupport) {
    auto pipeline = render_world->resource<pbr::PrepassPipeline>();
    pbr::MaterialProperties properties;
    properties.material_layout = render::BindGroupLayoutDescriptor::create("prepass depth emulation material",
                                                                           std::vector<wgpu::BindGroupLayoutEntry>{});
    mesh::MeshVertexBufferLayouts layouts;
    const auto layout                     = mesh::Cuboid(1, 1, 1).mesh().build().get_mesh_vertex_buffer_layout(layouts);
    pipeline.depth_clip_control_supported = false;
    const auto emulated = pipeline.specialize(pbr::MeshPipelineKey::UNCLIPPED_DEPTH_ORTHO, {}, layout, properties);
    ASSERT_TRUE(emulated);
    EXPECT_FALSE(emulated->primitive.unclippedDepth);
    ASSERT_TRUE(emulated->fragment);
    EXPECT_TRUE(emulated->fragment->targets.empty());
    EXPECT_TRUE(std::ranges::any_of(emulated->vertex.shader_defs,
                                    [](const auto& def) { return def.name == "UNCLIPPED_DEPTH_ORTHO_EMULATION"; }));
    pipeline.depth_clip_control_supported = true;
    const auto hardware = pipeline.specialize(pbr::MeshPipelineKey::UNCLIPPED_DEPTH_ORTHO, {}, layout, properties);
    ASSERT_TRUE(hardware);
    EXPECT_TRUE(hardware->primitive.unclippedDepth);
    EXPECT_FALSE(hardware->fragment);
}

TEST_F(PrepassFoundation, ActualDeviceCompilesDefaultPrepassBranchMatrix) { compile_branch_matrix(); }

TEST_F(PrepassFoundation, ActualDeviceCompilesForwardVisibilityDitherAndControl) { compile_branch_matrix(true); }

TEST_F(PrepassFoundation, DefaultShaderDrawWritesNativeDepthNormalMotionAndDeferredValues) { draw_and_check_outputs(); }

TEST_F(PrepassFoundation, PreparedViewBindsTheEntireVisibilityRangeTable) { draw_and_check_outputs(true); }

TEST_F(PrepassFoundation, PreparedViewWithoutMotionBindsTheEntireVisibilityRangeTable) {
    draw_and_check_outputs(true, false);
}

TEST_F(PrepassFoundation, CpuModelBindingRetainsTheNativeBufferAndLayoutContract) { check_cpu_model_binding(); }
}  // namespace
