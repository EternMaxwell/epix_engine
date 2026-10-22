#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>

#include <string>
#include <thread>

namespace {
namespace render = epix::render;
namespace assets = epix::assets;
namespace shader = epix::shader;

struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPoolBuilder{}.num_threads(4).build());
    }
} io_task_pool_init;

class GlobalShaderDefinitions : public testing::Test {
   protected:
    epix::app::App app = epix::app::App::create();

    void SetUp() override {
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{}, epix::camera::CameraPlugin{}, assets::AssetPlugin{},
                        epix::image::ImagePlugin{}, epix::mesh::MeshPlugin{}, shader::ShaderPlugin{},
                        render::FrameCountPlugin{});
        ASSERT_NO_THROW(app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true}));
    }

    void compile(bool probe_vertex, bool probe_fragment, bool asset_defaults = false, bool conflicting_limit = false) {
        const auto& world = app.get_sub_app_mut(render::Render)->get().world();
        wgpu::Limits limits{};
        world.resource<wgpu::Device>().getLimits(&limits);
        const auto global_probe = std::string{
            "#ifndef AVAILABLE_STORAGE_BUFFER_BINDINGS\n#error missing_device_global\n#endif\n"}
            + "#if AVAILABLE_STORAGE_BUFFER_BINDINGS != " + std::to_string(limits.maxStorageBuffersPerShaderStage)
            + "\n#error wrong_device_global\n#endif\n";
        const auto local_probe = std::string{
            "#ifndef DESCRIPTOR_CONTROL\n#error missing_descriptor_define\n#endif\n"}
            + "#if DESCRIPTOR_CONTROL != " + std::to_string(asset_defaults ? 23 : 17)
            + "\n#error wrong_descriptor_define\n#endif\n";
        const auto vertex_source = local_probe + (probe_vertex ? global_probe : "") + R"slang(
[shader("vertex")]
float4 vertex(uint index : SV_VulkanVertexID) : SV_Position {
    return float4(float(index & 1u), float((index >> 1u) & 1u), 0.5, 1.0);
}
)slang";
        const auto fragment_source = local_probe + (probe_fragment ? global_probe : "") + R"slang(
[shader("fragment")]
float4 fragment() : SV_Target0 { return float4(1.0, 0.0, 0.0, 1.0); }
)slang";
        auto& shaders = app.resource_mut<assets::Assets<shader::Shader>>();
        auto vertex_asset = shader::Shader::from_slang(vertex_source, assets::AssetPath{"global-defs/vertex.slang"});
        auto fragment_asset = shader::Shader::from_slang(fragment_source, assets::AssetPath{"global-defs/fragment.slang"});
        if (asset_defaults) {
            vertex_asset.shader_defs.push_back(shader::ShaderDefVal::from_uint("DESCRIPTOR_CONTROL", 23u));
            fragment_asset.shader_defs.push_back(shader::ShaderDefVal::from_uint("DESCRIPTOR_CONTROL", 23u));
        }
        const auto vertex = shaders.emplace(std::move(vertex_asset));
        const auto fragment = shaders.emplace(std::move(fragment_asset));
        render::RenderPipelineDescriptor descriptor{
            .label = "device global shader definitions probe",
            .vertex = {.shader = vertex,
                       .shader_defs = {shader::ShaderDefVal::from_uint("DESCRIPTOR_CONTROL", 17u)},
                       .entry_point = std::string("vertex")},
            .primitive = wgpu::PrimitiveState().setTopology(wgpu::PrimitiveTopology::eTriangleList)
                .setFrontFace(wgpu::FrontFace::eCCW).setCullMode(wgpu::CullMode::eNone),
            .multisample = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
            .fragment = render::FragmentState{
                .shader = fragment,
                .shader_defs = {shader::ShaderDefVal::from_uint("DESCRIPTOR_CONTROL", 17u)},
                .entry_point = std::string("fragment"),
                .targets = {wgpu::ColorTargetState().setFormat(wgpu::TextureFormat::eRGBA8Unorm)
                                                     .setWriteMask(wgpu::ColorWriteMask::eAll)}},
        };
        if (conflicting_limit) {
            const auto wrong_limit = limits.maxStorageBuffersPerShaderStage == 0 ? 1u : 0u;
            const auto wrong = shader::ShaderDefVal::from_uint("AVAILABLE_STORAGE_BUFFER_BINDINGS", wrong_limit);
            descriptor.vertex.shader_defs.push_back(wrong);
            descriptor.fragment->shader_defs.push_back(wrong);
        }
        auto& server = app.resource<render::PipelineServer>();
        const auto id = server.queue_render_pipeline(std::move(descriptor));
        for (unsigned attempt = 0; attempt < 400 && !server.get_render_pipeline(id); ++attempt) {
            app.run_schedule(epix::app::PreUpdate);
            app.run_schedule(epix::app::PostUpdate);
            app.run_schedule(epix::app::Last);
            std::this_thread::yield();
        }
        EXPECT_TRUE(server.get_render_pipeline(id).has_value())
            << "device global storage binding count must reach the selected stage and equal the actual device limit";
        const std::vector<shader::ShaderDefVal> expected_asset_defs = asset_defaults
            ? std::vector{shader::ShaderDefVal::from_uint("DESCRIPTOR_CONTROL", 23u)}
            : std::vector<shader::ShaderDefVal>{};
        ASSERT_TRUE(shaders.get(vertex.id()));
        ASSERT_TRUE(shaders.get(fragment.id()));
        EXPECT_EQ(shaders.get(vertex.id())->get().shader_defs, expected_asset_defs);
        EXPECT_EQ(shaders.get(fragment.id())->get().shader_defs, expected_asset_defs)
            << "device defaults belong to pipeline-server shader copies, not source assets";
    }
};

TEST_F(GlobalShaderDefinitions, DescriptorLocalDefinitionsRemainAvailable) { compile(false, false); }
TEST_F(GlobalShaderDefinitions, VertexStageReceivesActualDeviceStorageLimit) { compile(true, false); }
TEST_F(GlobalShaderDefinitions, FragmentStageReceivesActualDeviceStorageLimit) { compile(false, true); }
TEST_F(GlobalShaderDefinitions, ShaderDefaultsOverrideDescriptorValues) { compile(false, false, true); }
TEST_F(GlobalShaderDefinitions, DescriptorCannotOverrideDeviceGlobal) { compile(true, true, false, true); }
}  // namespace
