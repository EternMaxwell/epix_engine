// Minimal end-to-end 3D material example: one cube is created at startup
// and another at frame 90. The default mesh fragment shader renders magenta,
// making extraction, batching and drawing visible without StandardMaterial.

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/directional_light.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/material_plugin.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/render.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <expected>
#include <spdlog/spdlog.h>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

using namespace epix;
using namespace epix::app;
using namespace epix::assets;
using namespace epix::ecs;

namespace assets_mod = epix::assets;
namespace pbr        = epix::pbr;

/** @brief The material this example draws with: no bind-group data, no shaders, so the mesh
 *  pipeline's defaults draw it.
 *
 *  Bevy can write this as a two-line struct because `Material` has `AsBindGroup` as a
 *  supertrait and the derive fills the rest in. In this port the specialization below *is*
 *  that derive's output, and it has to be visible before anything names the material's
 *  erased render asset. */
struct FlatMaterial : pbr::Material<FlatMaterial> {};

/** @brief The specialization supplying what `AsBindGroupImpl` requires, with the bindless
 *  members inherited from `AsBindGroupBase`: an empty layout and an empty bind group. */
template <>
struct epix::render::render_resource::AsBindGroup<FlatMaterial> : epix::render::render_resource::AsBindGroupBase {
    using Data  = std::monostate;
    using Param = std::tuple<>;

    static constexpr std::string_view label() { return "core3d_first_mesh_material"; }
    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() { return {}; }
    static Data bind_group_data(const FlatMaterial&) { return {}; }
    static std::expected<epix::render::render_resource::UnpreparedBindGroup,
                         epix::render::render_resource::AsBindGroupError>
    unprepared_bind_group(const wgpu::Device&, const wgpu::BindGroupLayout&, const FlatMaterial&, Param&) {
        return epix::render::render_resource::UnpreparedBindGroup{};
    }
};

namespace {

/** @brief Whether the mid-run cube has been spawned yet. */
struct LateSpawn {
    bool done = false;
};

/** @brief One cube at the origin and the material to draw it with.
 *
 *  The camera looks down its local -Z, which is the engine's convention, so a camera at
 *  `(0.75, 0, 4.5)` with a default transform faces the gap between the two cubes. */
void spawn_scene(Commands cmd, ResMut<Assets<mesh::Mesh>> meshes, ResMut<Assets<FlatMaterial>> materials) {
    const auto mesh_handle     = meshes->emplace(mesh::Cuboid(1.0f, 1.0f, 1.0f).mesh().build());
    const auto material_handle = materials->emplace(FlatMaterial{});

    cmd.spawn(camera::Camera3d{}, transform::Transform::from_xyz(0.75f, 0.0f, 4.5f));
    // Two lights require five shared shadow-array layers, not the largest
    // individual count (three). This unlit shader's appearance is unchanged;
    // shadow views/rendering are not implemented yet.
    cmd.spawn(pbr::DirectionalLight{.shadows_enabled = true},
              pbr::CascadeShadowConfig{.bounds = {10.0f, 100.0f}},
              transform::Transform::from_xyz(0.0f, 4.0f, 0.0f));
    cmd.spawn(pbr::DirectionalLight{.shadows_enabled = true},
              pbr::CascadeShadowConfig{.bounds = {5.0f, 20.0f, 100.0f}},
              transform::Transform::from_xyz(0.0f, 3.0f, 0.0f));
    cmd.spawn(mesh::Mesh3d{mesh_handle}, pbr::MeshMaterial3d<FlatMaterial>{material_handle}, transform::Transform{});
}

/** @brief Add a second cube and a second material mid-run, to tell an asset that is never
 *  extracted from one that is only missed when it existed before the first frame. */
void spawn_late_mesh(Res<render::FrameCount> frames,
                     Commands cmd,
                     ResMut<Assets<mesh::Mesh>> meshes,
                     ResMut<Assets<FlatMaterial>> materials,
                     ResMut<LateSpawn> late) {
    if (late->done || frames->count < 90u) return;
    late->done                 = true;
    const auto mesh_handle     = meshes->emplace(mesh::Cuboid(1.0f, 1.0f, 1.0f).mesh().build());
    const auto material_handle = materials->emplace(FlatMaterial{});
    cmd.spawn(mesh::Mesh3d{mesh_handle}, pbr::MeshMaterial3d<FlatMaterial>{material_handle},
              transform::Transform::from_xyz(1.5f, 0.0f, 0.0f));
}

void probe_shadow_allocation(Query<Item<const pbr::ViewShadowBindings&>> views, Local<unsigned> frames,
                            Local<bool> logged) {
    if (*logged || ++*frames < 3) return;
    for (auto&& [shadows] : views.iter()) {
        const auto point_layers = shadows.point_light_depth_texture.getDepthOrArrayLayers();
        const auto directional_layers = shadows.directional_light_depth_texture.getDepthOrArrayLayers();
        spdlog::info("[first-mesh] Shadow allocation: point_layers={} directional_layers={}",
                     point_layers, directional_layers);
        if (point_layers != 6 || directional_layers != 5)
            spdlog::error("[first-mesh] Expected one fallback cube and five directional cascade layers");
        *logged = true;
        return;
    }
}

}  // namespace

int main() {
    App app = App::create();

    window::Window primary_window;
    primary_window.title = "3D First Mesh (Camera3d + Mesh3d + material)";
    primary_window.size  = {1280, 720};

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{
            .primary_window = primary_window,
            .exit_condition = window::ExitCondition::OnPrimaryClosed,
        })
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(sdl3::SDL3Plugin{})
        .add_plugins(sdl3::SDL3RenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets_mod::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(mesh::MeshPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        // The material's plugin: it registers the material asset, the allocator it is
        // prepared through, the draw sequences for the four forward phases, and the
        // prepare/specialize/queue systems that have to be in place before anything draws.
        .add_plugins(pbr::MaterialPlugin<FlatMaterial>{})
        // `PbrPlugin` brings `MeshRenderPlugin` (the mesh instance buffers and the mesh
        // bind groups), `MaterialsPlugin` (the registries and the phase commands) and the
        // light plugin whose clusters the extraction reads.
        .add_plugins(pbr::PbrPlugin{.supports_storage_buffers = true});

    app.add_systems(Startup, into(spawn_scene).set_name("spawn first mesh scene"));
    app.world_mut().init_resource<LateSpawn>();
    app.add_systems(Update, into(spawn_late_mesh).set_name("spawn late mesh"));
    // Only allocation is exercised here; small maps avoid paying for unused shadow passes.
    app.world_mut().insert_resource(pbr::PointLightShadowMap{.size = 64});
    app.world_mut().insert_resource(pbr::DirectionalLightShadowMap{.size = 64});
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_shadow_allocation)
                            .in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe shared shadow allocation"));
    app.run();
}
