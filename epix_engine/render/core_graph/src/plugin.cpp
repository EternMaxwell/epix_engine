
#include <spdlog/spdlog.h>

#include <cstddef>
#include <epix/core_graph.hpp>
#include <optional>
#include <span>
#include <string_view>

using namespace epix::ecs;
using namespace epix::app;

namespace epix::core_graph {
namespace {
constexpr std::string_view kFullscreenVertexPath  = "core_pipeline/fullscreen_vertex.slang";
constexpr std::string_view kFullscreenVertexSlang = R"slang(
struct VOut {
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv;
};
// Bevy fullscreen_vertex_shader/fullscreen.wgsl. The three generated vertices
// have UVs (0,0), (0,2), and (2,0), covering the whole target.
[shader("vertex")]
VOut fullscreen_vertex_shader(uint vertex_index : SV_VertexID) {
    float2 uv = float2(float(vertex_index >> 1), float(vertex_index & 1)) * 2.0f;
    VOut o;
    o.uv  = uv;
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}
}  // namespace

void CoreGraphPlugin::attach(App& app) {
    spdlog::debug("[core_graph] Attaching CoreGraphPlugin.");
    // Bevy CorePipelinePlugin embeds this shared shader before installing the
    // core-pipeline plugins, then initializes FullscreenShader in the render
    // world. It is a CoreGraphPlugin-owned facility, not a Core2D plugin.
    std::optional<FullscreenShader> fullscreen_shader;
    if (auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
        auto server   = app.world_mut().get_resource<assets::AssetServer>()) {
        registry->get().insert_asset_static(kFullscreenVertexPath, shader_bytes(kFullscreenVertexSlang));
        fullscreen_shader = FullscreenShader{
            server->get().load<shader::Shader>("embedded://core_pipeline/fullscreen_vertex.slang")};
    } else {
        spdlog::warn("[core_graph] EmbeddedAssetRegistry or AssetServer not available; fullscreen passes disabled.");
    }

    app.add_plugins(core_graph::core_2d::Core2dPlugin{});
    if (auto render_app = app.get_sub_app_mut(render::Render); render_app && fullscreen_shader &&
        !render_app->get().world().get_resource<FullscreenShader>()) {
        render_app->get().world_mut().insert_resource(std::move(*fullscreen_shader));
    }
}
}  // namespace epix::core_graph
