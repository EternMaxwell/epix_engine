// One material type, three assets: only their AsBindGroup::Data differs.
// The material callback selects each fragment colour through a shader define.
// If preparation drops that callback, all three cubes fall back to magenta.
#include <cstdint>
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
#include <expected>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

struct VariantMaterial : pbr::Material<VariantMaterial> {
    std::uint32_t variant = 0;
    static shader::ShaderRef fragment_shader() {
        return shader::ShaderRef::from_str("embedded://examples/material_specialization/variant.slang");
    }
    static std::expected<void, render::SpecializedMeshPipelineError> specialize(
        const pbr::MaterialPipeline&,
        render::RenderPipelineDescriptor&,
        const mesh::MeshVertexBufferLayoutRef&,
        pbr::MaterialPipelineKey<VariantMaterial>);
};

template <>
struct epix::render::render_resource::AsBindGroup<VariantMaterial> : epix::render::render_resource::AsBindGroupBase {
    using Data  = std::uint32_t;
    using Param = std::tuple<>;
    static constexpr std::string_view label() { return "material specialization"; }
    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() { return {}; }
    static Data bind_group_data(const VariantMaterial& material) { return material.variant; }
    static std::expected<epix::render::render_resource::UnpreparedBindGroup,
                         epix::render::render_resource::AsBindGroupError>
    unprepared_bind_group(const wgpu::Device&, const wgpu::BindGroupLayout&, const VariantMaterial&, Param&) {
        return epix::render::render_resource::UnpreparedBindGroup{};
    }
};

std::expected<void, render::SpecializedMeshPipelineError> VariantMaterial::specialize(
    const pbr::MaterialPipeline&,
    render::RenderPipelineDescriptor& descriptor,
    const mesh::MeshVertexBufferLayoutRef&,
    pbr::MaterialPipelineKey<VariantMaterial> key) {
    if (descriptor.fragment)
        descriptor.fragment->shader_defs.push_back(
            shader::ShaderDefVal::from_uint("MATERIAL_VARIANT", key.bind_group_data));
    return {};
}

namespace {
constexpr std::string_view variant_shader = R"slang(
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

void spawn_scene(Commands commands,
                 ResMut<assets::Assets<mesh::Mesh>> meshes,
                 ResMut<assets::Assets<VariantMaterial>> materials) {
    const auto cube = meshes->emplace(mesh::Cuboid(1.0f, 1.0f, 1.0f).mesh().build());
    commands.spawn(camera::Camera3d{},
                   camera::Projection::orthographic(
                       camera::OrthographicProjection{.scaling_mode = camera::ScalingMode::FixedVertical{4.5f}}),
                   transform::Transform::from_xyz(0.0f, 0.0f, 6.0f));
    for (std::uint32_t variant = 1; variant <= 3; ++variant) {
        const auto material = materials->emplace(VariantMaterial{.variant = variant});
        commands.spawn(mesh::Mesh3d{cube}, pbr::MeshMaterial3d<VariantMaterial>{material},
                       transform::Transform::from_xyz(2.2f * (static_cast<float>(variant) - 2.0f), 0.0f, 0.0f));
    }
}
}  // namespace

int main() {
    App app = App::create();
    window::Window primary;
    primary.title = "Material specialization: one material type / three bind-group data keys";
    primary.size  = {1280, 720};
    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(
            window::WindowPlugin{.primary_window = primary, .exit_condition = window::ExitCondition::OnPrimaryClosed})
        .add_plugins(input::InputPlugin{}, time::TimePlugin{}, sdl3::SDL3Plugin{}, sdl3::SDL3RenderPlugin{})
        .add_plugins(transform::TransformPlugin{}, camera::CameraPlugin{}, assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{}, mesh::MeshPlugin{}, render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{}, core_graph::CoreGraphPlugin{})
        .add_plugins(pbr::MaterialPlugin<VariantMaterial>{}, pbr::PbrPlugin{.supports_storage_buffers = true});
    app.resource_mut<assets::EmbeddedAssetRegistry>().insert_asset_static(
        "examples/material_specialization/variant.slang", std::as_bytes(std::span{variant_shader}));
    app.add_systems(Startup, into(spawn_scene));
    app.run();
}
