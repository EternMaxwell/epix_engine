// FullscreenMaterial (C3) visual example: a full-screen post-processing
// material samples the current frame and multiplies it by a per-camera tint.
// It exercises the generic `FullscreenMaterial` facility that installs
// component extraction + a dynamic uniform, queues paired HDR/non-HDR
// full-screen pipelines, and registers a typed view node between tonemapping
// and the end of post-processing in the Core2D graph.

#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>
using namespace epix;

// Uniform struct: a single RGBA tint applied to the sampled frame.
struct ScreenTintMaterial {
    glm::vec4 tint{0.9f, 0.15f, 0.15f, 1.0f};

    static std::string_view fragment_shader();
    static std::string_view fragment_shader_source();
    // Bevy node_edges: run after tonemapping, before the end of
    // post-processing. The material's own label sits between the two.
    static std::vector<render::graph::NodeLabel> node_edges();
    static std::optional<render::graph::GraphLabel> sub_graph();
};

// Extract the material component from the main world onto the synced render
// entity so the view node can read it through DynamicUniformIndex.
template <>
struct render::ExtractComponent<ScreenTintMaterial> {
    using QueryData   = const ScreenTintMaterial&;
    using QueryFilter = ::epix::ecs::Filter<>;
    using Out         = ScreenTintMaterial;

    static std::optional<Out> extract_component(QueryData value) { return value; }
};

namespace epix::render::render_resource {
template <>
struct ShaderTypeInfo<ScreenTintMaterial> : RawShaderType<ScreenTintMaterial> {};
}  // namespace epix::render::render_resource

std::string_view ScreenTintMaterial::fragment_shader() {
    return "core_pipeline/fullscreen_screen_tint.slang";
}

std::string_view ScreenTintMaterial::fragment_shader_source() {
    return R"slang(
[[vk::binding(0, 0)]] Texture2D<float4> screen_texture;
[[vk::binding(1, 0)]] SamplerState screen_sampler;
struct Settings { float4 tint; };
[[vk::binding(2, 0)]] ConstantBuffer<Settings> settings;
struct VIn {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
};
[shader("fragment")]
float4 fs_main(VIn input) : SV_Target {
    float4 color = screen_texture.Sample(screen_sampler, input.uv);
    return float4(color.rgb * settings.tint.rgb, color.a);
}
)slang";
}

std::vector<render::graph::NodeLabel> ScreenTintMaterial::node_edges() {
    return {render::graph::NodeLabel{core_graph::core_2d::Core2dNodes::Tonemapping},
            core_graph::fullscreen_material_node_label<ScreenTintMaterial>(),
            render::graph::NodeLabel{core_graph::core_2d::Core2dNodes::EndMainPassPostProcessing}};
}

std::optional<render::graph::GraphLabel> ScreenTintMaterial::sub_graph() {
    return render::graph::GraphLabel{core_graph::core_2d::Core2d};
}

struct FullscreenMaterialVisualTestPlugin {
    void ready(app::App& app) {
        // A Camera2d drives the Core2D graph; attaching the material component
        // makes the FullscreenMaterialPlugin register and run the node.
        app.world_mut().spawn(camera::Camera2d{}, transform::Transform{}, ScreenTintMaterial{});
    }
};

int main() {
    app::App app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Fullscreen Material";
    primary_window.size  = {1280, 720};

    app.add_plugins(app::TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{
            .primary_window = primary_window,
            .exit_condition = window::ExitCondition::OnPrimaryClosed,
        })
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(core_graph::FullscreenMaterialPlugin<ScreenTintMaterial>{})
        .add_plugins(FullscreenMaterialVisualTestPlugin{});

    app.run();
}
