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
#include <epix/pbr/light_plugin.hpp>
#include <epix/pbr/light_systems.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/material_plugin.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/pbr/shadow_phase.hpp>
#include <epix/pbr/spot_light.hpp>
#include <epix/render.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <expected>
#include <cmath>
#include <numbers>
#include <optional>
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
    // shadow view preparation is exercised without changing the unlit shader.
    cmd.spawn(pbr::DirectionalLight{.shadows_enabled = true},
              pbr::CascadeShadowConfig{.bounds = {10.0f, 100.0f}},
              transform::Transform::from_xyz(0.0f, 4.0f, 0.0f));
    cmd.spawn(pbr::DirectionalLight{.shadows_enabled = true},
              pbr::CascadeShadowConfig{.bounds = {5.0f, 20.0f, 100.0f}},
              pbr::SunDisk{.angular_size = 0.0235f, .intensity = 4.25f},
              transform::Transform::from_xyz(0.0f, 3.0f, 0.0f));
    // Exercise real cluster membership and point/spot extraction. These lights
    // do not cast shadows or change the default magenta, unlit mesh shader.
    cmd.spawn(pbr::PointLight{.intensity = 80.0f * std::numbers::pi_v<float>},
              transform::Transform::from_xyz(-1.0f, 1.0f, 1.0f));
    cmd.spawn(pbr::SpotLight{.intensity = 120.0f * std::numbers::pi_v<float>},
              transform::Transform::from_xyz(2.0f, 1.0f, 2.0f));
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

// Observe the real extracted maps without changing the unlit scene. This checks
// render-world view identities and independent cascade payloads, not shadow drawing.
void probe_directional_extraction(
    Query<Item<const pbr::ExtractedDirectionalLight&, const pbr::RenderCascadesVisibleEntities&>> lights,
    Query<const render::view::ExtractedView&> views, Local<unsigned> frames, Local<bool> logged) {
    if (*logged || ++*frames < 3) return;
    std::size_t light_count = 0, cascade_views = 0, frustum_views = 0, visible_views = 0;
    std::size_t cascade_count = 0, frustum_count = 0, list_count = 0;
    bool valid = true;
    for (auto&& [light, visible] : lights.iter()) {
        ++light_count;
        for (const auto& [view, cascades] : light.cascades) {
            ++cascade_views;
            cascade_count += cascades.size();
            valid = views.contains(view) && valid;
        }
        for (const auto& [view, frusta] : light.frusta) {
            ++frustum_views;
            frustum_count += frusta.size();
            valid = views.contains(view) && valid;
        }
        for (const auto& [view, lists] : visible.entities) {
            ++visible_views;
            list_count += lists.size();
            valid = views.contains(view) && valid;
        }
    }
    if (light_count == 0 || cascade_views == 0 || frustum_views == 0 || visible_views == 0) return;
    spdlog::info("[first-mesh] Directional extraction: lights={} cascade_views={} frustum_views={} visible_views={} "
                 "cascades={} frusta={} visible_lists={} render_keys_valid={}",
                 light_count, cascade_views, frustum_views, visible_views,
                 cascade_count, frustum_count, list_count, valid);
    if (!valid || light_count != 2 || cascade_views != 2 || frustum_views != 2 || visible_views != 2 ||
        cascade_count != 5 || frustum_count != 5 || list_count != 5)
        spdlog::error("[first-mesh] Directional extraction lost a view mapping or cascade payload");
    *logged = true;
}

// The unlit scene has no atmosphere. Observe the extracted parameters only;
// this is not evidence of visible sun-disk or atmospheric rendering.
void probe_sun_disks(Query<const pbr::ExtractedDirectionalLight&> lights,
                     Local<unsigned> frames, Local<bool> logged) {
    if (*logged || ++*frames < 3) return;
    std::size_t count = 0, earth = 0, custom = 0;
    for (const auto& light : lights.iter()) {
        ++count;
        if (light.sun_disk_angular_size == pbr::SunDisk::EARTH.angular_size &&
            light.sun_disk_intensity == pbr::SunDisk::EARTH.intensity) ++earth;
        if (light.sun_disk_angular_size == 0.0235f && light.sun_disk_intensity == 4.25f) ++custom;
    }
    if (count == 0) return;
    spdlog::info("[first-mesh] Sun disk extraction: lights={} earth_defaults={} custom={}", count, earth, custom);
    if (count != 2 || earth != 1 || custom != 1)
        spdlog::error("[first-mesh] Sun disk extraction lost default or custom parameters");
    *logged = true;
}

void probe_clustered_extraction(Query<Item<Entity, const pbr::ExtractedPointLight&>> lights,
                                Res<pbr::GlobalClusterableObjectMeta> meta,
                                Local<unsigned> frames, Local<bool> logged) {
    if (*logged || ++*frames < 3) return;
    std::size_t points = 0, spots = 0;
    bool mapped = true, units = true;
    for (auto&& [entity, light] : lights.iter()) {
        mapped = meta->entity_to_index.contains(entity) && mapped;
        if (light.spot_light_angles) {
            ++spots;
            units = std::abs(light.intensity - 30.0f) < 1e-5f && units;
        } else {
            ++points;
            units = std::abs(light.intensity - 20.0f) < 1e-5f && units;
        }
    }
    if (points == 0 && spots == 0) return;
    const bool uploaded = meta->gpu_clusterable_objects.binding().has_value();
    spdlog::info("[first-mesh] Clustered extraction: points={} spots={} render_indices_valid={} "
                 "power_units_valid={} gpu_buffer_ready={}", points, spots, mapped, units, uploaded);
    if (points != 1 || spots != 1 || !mapped || !units || !uploaded)
        spdlog::error("[first-mesh] Point/spot extraction did not reach the global GPU upload");
    *logged = true;
}

// Read-only lifecycle probe, not proof of populated shadow views or depth draws.
void probe_light_view_lifecycle(Query<Item<const pbr::ExtractedDirectionalLight&, const pbr::LightViewEntities&>> suns,
                                Query<Item<const pbr::ExtractedPointLight&, const pbr::LightViewEntities&>> clustered,
                                Local<unsigned> frames, Local<bool> logged) {
    if (*logged || ++*frames < 3) return;
    const auto directional = std::ranges::distance(suns.iter());
    const auto point_spot = std::ranges::distance(clustered.iter());
    if (directional == 0 && point_spot == 0) return;
    spdlog::info("[first-mesh] Light view lifecycle: directional_maps={} point_spot_maps={}", directional, point_spot);
    if (directional != 2 || point_spot != 2)
        spdlog::error("[first-mesh] Extracted light is missing its observer-created view map");
    *logged = true;
}

struct ShadowMapTickProbe {
    std::optional<ecs::Tick> last;
    unsigned samples = 0;
    unsigned stable = 0;
    bool logged = false;
};

// Read-only end-to-end check of the registered producer, not a shadow draw.
void probe_shadow_views(
    Query<Item<const pbr::ShadowView&, const pbr::LightEntity&, const render::view::ExtractedView&>> shadows,
    Query<Item<const render::sync_world::MainEntity&>> identities,
    Query<Item<const render::sync_world::MainEntity&>, With<camera::Camera3d>> cameras,
    Res<render::phase::ViewBinnedRenderPhases<pbr::Shadow>> phases, Local<unsigned> frames, Local<bool> logged) {
    if (*logged || ++*frames < 8) return;
    std::size_t directional = 0, point = 0, spot = 0;
    bool valid = true;
    for (auto&& [attachment, owner, view] : shadows.iter()) {
        directional += owner.kind == pbr::LightEntity::Kind::Directional;
        point += owner.kind == pbr::LightEntity::Kind::Point;
        spot += owner.kind == pbr::LightEntity::Kind::Spot;
        const auto light = identities.get(owner.light_entity);
        valid = valid && light && std::get<0>(*light) == view.retained_view_entity.main_entity;
        bool camera_found = false;
        for (auto&& [main] : cameras.iter()) camera_found |= main == view.retained_view_entity.auxiliary_entity;
        valid = valid && camera_found && phases->phases.contains(view.retained_view_entity) && attachment.depth_attachment.view;
    }
    spdlog::info("[first-mesh] Shadow view preparation: directional={} point={} spot={} phases={} retained_keys_valid={}",
                 directional, point, spot, phases->phases.size(), valid);
    if (directional != 5 || point != 0 || spot != 0 || phases->phases.size() != 5 || !valid)
        spdlog::error("[first-mesh] Scheduled shadow-view preparation is incomplete");
    *logged = true;
}

void probe_temporal_views(
    Query<Item<const render::view::ExtractedView&, Has<camera::Camera3d>, Has<pbr::ShadowView>,
               Opt<const core_graph::PreviousViewUniformOffset&>>,
          Or<With<camera::Camera3d>, With<pbr::ShadowView>>> views,
    Res<core_graph::PreviousViewUniforms> uniforms, Local<unsigned> frames, Local<bool> logged) {
    if (*logged || ++*frames < 8) return;
    std::size_t cameras = 0, shadows = 0;
    bool ready = true;
    const auto* buffer = uniforms->uniforms.buffer();
    for (auto&& [view, camera, shadow, offset] : views.iter()) {
        cameras += camera;
        shadows += shadow;
        ready = ready && buffer && offset &&
            static_cast<std::uint64_t>(offset->get().offset) + sizeof(core_graph::PreviousViewData) <= buffer->getSize();
    }
    spdlog::info("[first-mesh] Temporal view uniforms: cameras={} shadows={} offsets_ready={}", cameras, shadows, ready);
    if (cameras != 1 || shadows != 5 || !ready)
        spdlog::error("[first-mesh] Camera/shadow temporal uniforms are not ready after PrepareResources");
    *logged = true;
}

void probe_light_writer(
    Query<Item<const render::view::ExtractedView&, const pbr::ExtractedClusterConfig&,
               const render::sync_world::MainEntity&>, With<camera::Camera3d>> cameras,
    Res<pbr::LightMeta> lights, Res<wgpu::Device> device, Local<unsigned> frames, Local<bool> logged) {
    if (*logged || ++*frames < 8) return;
    const auto count = static_cast<std::size_t>(std::ranges::distance(cameras.iter()));
    wgpu::Limits limits{};
    device->getLimits(&limits);
    const auto stride = render::render_resource::align_up(
        render::render_resource::ShaderTypeInfo<pbr::GpuLights>::shader_size, limits.minUniformBufferOffsetAlignment);
    const auto* buffer = lights->view_gpu_lights.buffer();
    const bool sufficient = buffer && buffer->getSize() >= count * stride;
    spdlog::info("[first-mesh] Light writer: raw_cameras={} capacity_sufficient={}", count, sufficient);
    if (count != 1 || !sufficient) spdlog::error("[first-mesh] Native light writer was not prepared for the raw camera query");
    *logged = true;
}

void observe_shadow_map_ticks(std::string_view world, const Res<pbr::DirectionalLightShadowMap>& map,
                             ShadowMapTickProbe& state) {
    if (state.logged) return;
    if (state.last && *state.last == map.last_modified()) ++state.stable;
    else state.stable = 0;
    state.last = map.last_modified();
    if (++state.samples < 12) return;
    spdlog::info("[first-mesh] Shadow resource ticks: world={} samples={} stable_samples={} size={}",
                 world, state.samples, state.stable, map->size);
    if (state.stable != 11 || map->size != 64)
        spdlog::error("[first-mesh] Unchanged shadow-map resource acquired a spurious write in {}", world);
    state.logged = true;
}

void probe_main_shadow_map_ticks(Res<pbr::DirectionalLightShadowMap> map, Local<ShadowMapTickProbe> state) {
    observe_shadow_map_ticks("main", map, *state);
}

void probe_render_shadow_map_ticks(Res<pbr::DirectionalLightShadowMap> map, Local<ShadowMapTickProbe> state) {
    observe_shadow_map_ticks("render", map, *state);
}

struct LightReplacementProbe {
    unsigned samples = 0;
    bool logged = false;
    std::vector<std::pair<Entity, Entity>> identities;
};

void probe_light_replacement(Commands commands,
                             Query<Item<Entity, const pbr::DirectionalLight&,
                                        const render::sync_world::RenderEntity&>> lights,
                             Local<LightReplacementProbe> state) {
    if (state->logged) return;
    if (state->identities.empty()) {
        if (++state->samples < 16) return;
        // Reinsert equal values through real deferred Commands, after lighting's
        // main-world work. The next extraction must retain render identities.
        std::vector<std::pair<Entity, pbr::DirectionalLight>> replacements;
        for (auto&& [entity, light, render_entity] : lights.iter()) {
            state->identities.emplace_back(entity, render_entity.id());
            replacements.emplace_back(entity, light);
        }
        commands.try_insert_batch(std::move(replacements));
        return;
    }
    bool stable = state->identities.size() == 2;
    for (const auto& [entity, original_render] : state->identities) {
        const auto current = lights.get(entity);
        stable = stable && current.has_value() && std::get<2>(*current).id() == original_render;
    }
    spdlog::info("[first-mesh] Synced replacement: lights={} render_identity_stable={}",
                 state->identities.size(), stable);
    if (!stable) spdlog::error("[first-mesh] Replacing a light value discarded its render identity");
    state->logged = true;
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
    // Exercise allocation and view preparation with small maps; depth draws remain unported.
    app.world_mut().insert_resource(pbr::PointLightShadowMap{.size = 64});
    app.world_mut().insert_resource(pbr::DirectionalLightShadowMap{.size = 64});
    app.add_systems(PostUpdate, into(probe_main_shadow_map_ticks)
                                   .after(ecs::SystemSetLabel(pbr::validate_shadow_map_size))
                                   .set_name("probe main shadow resource ticks"));
    app.add_systems(PostUpdate, into(probe_light_replacement)
                                   .after(pbr::SimulationLightSystems::CheckLightVisibility)
                                   .set_name("probe synced light replacement"));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_shadow_allocation)
                            .in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe shared shadow allocation"));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_directional_extraction)
                            .in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe extracted directional identities"));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_sun_disks)
                            .in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe extracted sun disk parameters"));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_clustered_extraction)
                            .in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe point and spot extraction"));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_render_shadow_map_ticks)
                            .in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe render shadow resource ticks"));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_light_view_lifecycle)
                            .in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe observer-created light view maps"));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_shadow_views)
                            .in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe prepared shadow views"));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_light_writer)
                            .in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe native light writer"));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_temporal_views)
                            .in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe scheduled temporal uniforms"));
    app.run();
}
