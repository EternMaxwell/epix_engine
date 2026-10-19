// Left: default magenta shader. Middle: path-loaded green fragment shader.
// Right: handle-selected blue fragment shader and a path-loaded vertex shader
// that raises the cube. All three use normal material extraction/preparation.
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/material_plugin.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/render.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/shader.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <spdlog/spdlog.h>

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

template <int Mode>
struct DemoMaterial : pbr::Material<DemoMaterial<Mode>> {
    static inline shader::ShaderRef fragment{};
    static shader::ShaderRef fragment_shader() { return fragment; }
    static shader::ShaderRef vertex_shader() {
        if constexpr (Mode == 2)
            return shader::ShaderRef::from_str("embedded://examples/material_shaders/raised.slang");
        return {};
    }
};

template <int Mode>
struct epix::render::render_resource::AsBindGroup<DemoMaterial<Mode>>
    : epix::render::render_resource::AsBindGroupBase {
    using Data = std::monostate;
    using Param = std::tuple<>;
    static constexpr std::string_view label() { return "material shader references"; }
    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() { return {}; }
    static Data bind_group_data(const DemoMaterial<Mode>&) { return {}; }
    static std::expected<epix::render::render_resource::UnpreparedBindGroup,
                         epix::render::render_resource::AsBindGroupError>
    unprepared_bind_group(const wgpu::Device&, const wgpu::BindGroupLayout&, const DemoMaterial<Mode>&, Param&) {
        return epix::render::render_resource::UnpreparedBindGroup{};
    }
};

namespace {
constexpr std::string_view green_shader = R"slang(
[shader("fragment")]
float4 fragment() : SV_Target { return float4(0.0, 0.8, 0.2, 1.0); }
)slang";
constexpr std::string_view blue_shader = R"slang(
[shader("fragment")]
float4 fragment() : SV_Target { return float4(0.1, 0.3, 1.0, 1.0); }
)slang";
constexpr std::string_view raised_shader = R"slang(
import epix.mesh_functions;
import epix.view_transformations;

struct Vertex {
    [[vk::location(0)]] float3 position : POSITION;
    uint instance_index : SV_VulkanInstanceID;
};
[shader("vertex")]
float4 vertex(Vertex input) : SV_Position {
    float4 world = epix::mesh_functions::mesh_position_local_to_world(
        epix::mesh_functions::get_world_from_local(input.instance_index), float4(input.position, 1.0));
    world.y += 0.75;
    return epix::view_transformations::position_world_to_clip(world.xyz);
}
)slang";

void spawn_scene(Commands commands, ResMut<assets::Assets<mesh::Mesh>> meshes,
                 ResMut<assets::Assets<shader::Shader>> shaders,
                 ResMut<assets::Assets<DemoMaterial<0>>> defaults,
                 ResMut<assets::Assets<DemoMaterial<1>>> paths,
                 ResMut<assets::Assets<DemoMaterial<2>>> handles) {
    DemoMaterial<1>::fragment =
        shader::ShaderRef::from_str("embedded://examples/material_shaders/green.slang");
    DemoMaterial<2>::fragment = shader::ShaderRef::from_handle(shaders->emplace(shader::Shader::from_slang(
        std::string{blue_shader}, assets::AssetPath{"manual/material_shaders/blue.slang"})));
    const auto cube = meshes->emplace(mesh::Cuboid(1.0f, 1.0f, 1.0f).mesh().build());
    commands.spawn(camera::Camera3d{},
                   camera::Projection::orthographic(camera::OrthographicProjection{
                       .scaling_mode = camera::ScalingMode::FixedVertical{4.5f}}),
                   transform::Transform::from_xyz(0.0f, 0.0f, 6.0f));
    commands.spawn(mesh::Mesh3d{cube}, pbr::MeshMaterial3d<DemoMaterial<0>>{defaults->emplace(DemoMaterial<0>{})},
                   transform::Transform::from_xyz(-2.2f, 0.0f, 0.0f));
    commands.spawn(mesh::Mesh3d{cube}, pbr::MeshMaterial3d<DemoMaterial<1>>{paths->emplace(DemoMaterial<1>{})},
                   transform::Transform{});
    commands.spawn(mesh::Mesh3d{cube}, pbr::MeshMaterial3d<DemoMaterial<2>>{handles->emplace(DemoMaterial<2>{})},
                   transform::Transform::from_xyz(2.2f, 0.0f, 0.0f));
}

void probe_material_shaders(
    Res<render::erased_render_asset::ErasedRenderAssets<pbr::PreparedMaterial>> materials,
    Res<render::PipelineServer> pipelines, Local<unsigned> frames) {
    if (++*frames != 90) return;
    for (const auto& [id, material] : materials->iter())
        spdlog::info("[material-shaders] Prepared material overrides={}", material.properties->shaders.size());
    for (std::size_t index = 0; index < pipelines->pipeline_count(); ++index) {
        const render::CachedPipelineId id{index};
        const auto state = pipelines->get_pipeline_state(id);
        spdlog::info("[material-shaders] Pipeline {} state={} ready={}", index,
                     state ? state->get().index() : 99, pipelines->get_render_pipeline(id).has_value());
        if (state) {
            if (const auto* waiting = std::get_if<render::PipelineStateRecoverableShaderError>(&state->get()))
                spdlog::warn("[material-shaders] Pipeline {} waiting: {}", index, waiting->signature);
        }
    }
}
}  // namespace

int main() {
    App app = App::create();
    window::Window primary;
    primary.title = "Material shader references: default / path / handle + raised vertex";
    primary.size = {1280, 720};
    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{.primary_window = primary,
                                         .exit_condition = window::ExitCondition::OnPrimaryClosed})
        .add_plugins(input::InputPlugin{}, time::TimePlugin{}, sdl3::SDL3Plugin{}, sdl3::SDL3RenderPlugin{})
        .add_plugins(transform::TransformPlugin{}, camera::CameraPlugin{}, assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{}, mesh::MeshPlugin{}, render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{}, core_graph::CoreGraphPlugin{})
        .add_plugins(pbr::MaterialPlugin<DemoMaterial<0>>{}, pbr::MaterialPlugin<DemoMaterial<1>>{},
                     pbr::MaterialPlugin<DemoMaterial<2>>{}, pbr::PbrPlugin{.supports_storage_buffers = true});
    auto& embedded = app.resource_mut<assets::EmbeddedAssetRegistry>();
    embedded.insert_asset_static("examples/material_shaders/green.slang", std::as_bytes(std::span{green_shader}));
    embedded.insert_asset_static("examples/material_shaders/raised.slang", std::as_bytes(std::span{raised_shader}));
    app.add_systems(Startup, into(spawn_scene));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_material_shaders).in_set(render::RenderSystems::Cleanup));
    app.run();
    DemoMaterial<1>::fragment = {};
    DemoMaterial<2>::fragment = {};
}
