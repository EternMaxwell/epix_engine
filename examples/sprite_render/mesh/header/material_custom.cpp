// Top: one custom material type, three associated-data variants. A handle
// selects the fragment, a path selects the lifted vertex, and the vertex uses
// the shared mesh2d transform library. Bottom: three ordinary colour materials
// share one pipeline while retaining their own tint and UV uniforms.
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

struct VariantMaterial2d : sprite_render::Material2d<VariantMaterial2d> {
    std::uint32_t variant = 0;
    static inline shader::ShaderRef fragment;
    static shader::ShaderRef vertex_shader() {
        return shader::ShaderRef::from_str("embedded://examples/custom_material2d/lifted.slang");
    }
    static shader::ShaderRef fragment_shader() { return fragment; }
    static std::expected<void, render::SpecializedMeshPipelineError> specialize(
        render::RenderPipelineDescriptor&,
        const mesh::MeshVertexBufferLayoutRef&,
        sprite_render::Material2dKey<VariantMaterial2d>);
};

template <>
struct epix::render::render_resource::AsBindGroup<VariantMaterial2d> : epix::render::render_resource::AsBindGroupBase {
    using Data  = std::uint32_t;
    using Param = std::tuple<>;
    static constexpr std::string_view label() { return "custom 2d material variants"; }
    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() { return {}; }
    static Data bind_group_data(const VariantMaterial2d& material) { return material.variant; }
    static std::expected<epix::render::render_resource::UnpreparedBindGroup,
                         epix::render::render_resource::AsBindGroupError>
    unprepared_bind_group(const wgpu::Device&, const wgpu::BindGroupLayout&, const VariantMaterial2d&, Param&) {
        return epix::render::render_resource::UnpreparedBindGroup{};
    }
};

std::expected<void, render::SpecializedMeshPipelineError> VariantMaterial2d::specialize(
    render::RenderPipelineDescriptor& descriptor,
    const mesh::MeshVertexBufferLayoutRef&,
    sprite_render::Material2dKey<VariantMaterial2d> key) {
    if (descriptor.fragment)
        descriptor.fragment->shader_defs.push_back(
            shader::ShaderDefVal::from_uint("MATERIAL_VARIANT", key.bind_group_data));
    return {};
}

namespace {
constexpr std::string_view fragment_shader = R"slang(
[shader("fragment")]
float4 fragment() : SV_Target {
#if MATERIAL_VARIANT == 1
    return float4(1.0, 0.1, 0.1, 1.0);
#elif MATERIAL_VARIANT == 2
    return float4(0.0, 0.8, 0.2, 1.0);
#elif MATERIAL_VARIANT == 3
    return float4(0.1, 0.3, 1.0, 1.0);
#else
    return float4(1.0, 0.0, 1.0, 1.0);
#endif
}
)slang";
constexpr std::string_view vertex_shader   = R"slang(
import epix.mesh2d_functions;
using namespace epix::mesh2d_functions;
struct Vertex {
    [[vk::location(0)]] float3 position;
    uint instance_index : SV_VulkanInstanceID;
};
[shader("vertex")]
float4 vertex(Vertex input) : SV_Position {
    float4 world = mesh2d_position_local_to_world(
        get_world_from_local(input.instance_index), float4(input.position, 1.0));
    world.y += 0.25;
    return mesh2d_position_world_to_clip(world);
}
)slang";

void spawn_scene(Commands commands,
                 ResMut<assets::Assets<mesh::Mesh>> meshes,
                 ResMut<assets::Assets<VariantMaterial2d>> variants,
                 ResMut<assets::Assets<sprite_render::ColorMaterial>> colours) {
    auto projection         = camera::OrthographicProjection::default_2d();
    projection.scaling_mode = camera::ScalingMode::FixedVertical{4.5f};
    // Keep custom and stock fragment colours directly comparable.
    commands.spawn(camera::Camera2d{}, camera::Projection::orthographic(projection), transform::Transform{},
                   core_graph::Tonemapping::None);
    const auto quad       = meshes->emplace(mesh::Rectangle(1.0f, 1.0f).mesh().build());
    const glm::vec3 rgb[] = {{1.0f, 0.1f, 0.1f}, {0.0f, 0.8f, 0.2f}, {0.1f, 0.3f, 1.0f}};
    for (std::uint32_t variant = 1; variant <= 3; ++variant) {
        const float x = 2.2f * (static_cast<float>(variant) - 2.0f);
        commands.spawn(
            mesh::Mesh2d{quad},
            sprite_render::MeshMaterial2d<VariantMaterial2d>{variants->emplace(VariantMaterial2d{.variant = variant})},
            transform::Transform::from_xyz(x, 0.5f, 0.0f));
        const auto rgb_value = rgb[variant - 1];
        auto material        = sprite_render::ColorMaterial::from_color(
            color::Color::linear_rgba(rgb_value.r, rgb_value.g, rgb_value.b, 1.0f));
        material.uv_transform[0][0] = static_cast<float>(variant);
        auto mesh_transform         = transform::Transform::from_xyz(x, -0.9f, 0.0f);
        mesh_transform.scale(glm::vec3{0.75f});
        commands.spawn(
            mesh::Mesh2d{quad},
            sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{colours->emplace(std::move(material))},
            mesh_transform);
    }
}
}  // namespace

int main() {
    App app = App::create();
    window::Window primary;
    primary.title = "Custom 2D materials: handle / path / associated data; shared colour pipeline";
    primary.size  = {1280, 720};
    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(
            window::WindowPlugin{.primary_window = primary, .exit_condition = window::ExitCondition::OnPrimaryClosed})
        .add_plugins(input::InputPlugin{}, time::TimePlugin{}, sdl3::SDL3Plugin{}, sdl3::SDL3RenderPlugin{})
        .add_plugins(transform::TransformPlugin{}, camera::CameraPlugin{}, assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{}, mesh::MeshPlugin{}, sprite::SpritePlugin{}, render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{}, core_graph::CoreGraphPlugin{}, sprite_render::SpriteRenderPlugin{})
        .add_plugins(sprite_render::Material2dPlugin<VariantMaterial2d>{});
    app.resource_mut<assets::EmbeddedAssetRegistry>().insert_asset_static("examples/custom_material2d/lifted.slang",
                                                                          std::as_bytes(std::span{vertex_shader}));
    VariantMaterial2d::fragment = shader::ShaderRef::from_handle(
        app.resource_mut<assets::Assets<shader::Shader>>().emplace(shader::Shader::from_slang(
            std::string{fragment_shader}, assets::AssetPath{"manual/custom_material2d/variant.slang"})));
    app.add_systems(Startup, into(spawn_scene));
    app.run();
    VariantMaterial2d::fragment = {};
}
