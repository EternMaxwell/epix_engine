#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/core_graph/core3d.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif
#include <epix/camera/projection.hpp>
#include <epix/camera/visibility.hpp>
#include <epix/pbr/ambient_light.hpp>
#include <epix/pbr/cascade.hpp>
#include <epix/pbr/cluster.hpp>
#include <epix/pbr/directional_light.hpp>
#include <epix/pbr/gpu_lights.hpp>
#include <epix/pbr/light_cluster.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/pbr/shadow_phase_item.hpp>
#include <epix/pbr/spot_light.hpp>
#include <epix/pbr/volumetric.hpp>

namespace epix::pbr {

/** @brief Holds one `GpuLights` per view, uploaded once per frame (Bevy
 *  `LightMeta`, `bevy_pbr/src/render/light.rs:685-688`).
 *
 *  `prepare_lights` acquires a device-aligned writer and stamps each sorted
 *  view with its entry's byte offset, used by the mesh view bind group. */
EPIX_EXPORT struct LightMeta {
    render::render_resource::DynamicUniformBuffer<GpuLights> view_gpu_lights;

    /** @brief Native preparation resolves device alignment through get_writer.
     *  The explicit constructor also supports manual push/write-buffer clients. */
    LightMeta() = default;
    explicit LightMeta(std::size_t dynamic_offset_alignment)
        : view_gpu_lights(
              render::render_resource::DynamicUniformBuffer<GpuLights>::new_with_alignment(dynamic_offset_alignment)) {}
};

/** @brief This view's `GpuLights` entry offset in `LightMeta` (Bevy
 *  `ViewLightsUniformOffset`, `light.rs:680-683`). */
EPIX_EXPORT struct ViewLightsUniformOffset {
    std::uint32_t offset = 0;
};

/** @brief The shadow cascade views associated with a camera (Bevy
 *  `ViewLightEntities`, `light.rs:670-678`). Despite the name it holds the
 *  cascade views, not the lights. */
EPIX_EXPORT struct ViewLightEntities {
    std::vector<ecs::Entity> lights;
};

/** @brief Per-light map from camera render entities to their shadow views
 * (Bevy LightViewEntities). PbrPlugin's Add/Remove observers initialize and
 * retire this map; ordinary component replacement retains its contents. */
EPIX_EXPORT struct EPIX_API LightViewEntities {
    std::unordered_map<ecs::Entity, std::vector<ecs::Entity>> entities;
};

/** @brief The depth textures a view's shadow maps render into (Bevy
 *  `ViewShadowBindings`, `light.rs:657-663`), one pair for point lights' cubemaps
 *  and one for directional lights' cascades. */
EPIX_EXPORT struct ViewShadowBindings {
    wgpu::Texture point_light_depth_texture;
    wgpu::TextureView point_light_depth_texture_view;
    wgpu::Texture directional_light_depth_texture;
    wgpu::TextureView directional_light_depth_texture_view;
};

/** @brief A shadow view: the depth attachment one light face or cascade renders
 *  into, and the pass name it runs under (Bevy `ShadowView`,
 *  `render/light.rs:651-655`). */
EPIX_EXPORT struct ShadowView {
    render::render_resource::DepthAttachment depth_attachment;
    std::string pass_name;

    ShadowView() = default;
    ShadowView(render::render_resource::DepthAttachment attachment, std::string name)
        : depth_attachment(std::move(attachment)), pass_name(std::move(name)) {}
};

/** @brief The ambient light that applies to a view (Bevy `prepare_lights`,
 *  `render/light.rs:1153-1158`).
 *
 *  Bevy copies the scene-wide `GlobalAmbientLight` resource into a local
 *  `AmbientLight` - the two are separate types with the same fields, and the copy
 *  is what lets the camera's override share a type with it - then a camera-level
 *  `AmbientLight` *replaces* it rather than blending with it. The resolved colour
 *  and brightness become the light uniform's `ambient_color`, and
 *  `affects_lightmapped_meshes` becomes its own flag. */
EPIX_EXPORT EPIX_API AmbientLight resolve_ambient_light(const AmbientLight* camera_override,
                                                      const GlobalAmbientLight& global);

/** @brief A point light's shadow map is a cubemap, so it takes six layers (Bevy
 *  `render/light.rs:1278`, which spawns one view per face). */
EPIX_EXPORT inline constexpr std::size_t POINT_LIGHT_CUBE_FACE_COUNT = 6;

/** @brief How many layers the point-light shadow texture needs (Bevy
 *  `render/light.rs:1030`): six per shadow-casting point light, and at least one
 *  cubemap even when nothing casts, because the texture is created regardless. */
EPIX_EXPORT inline std::uint32_t point_light_shadow_texture_layers(std::size_t point_light_shadow_maps_count) {
    return static_cast<std::uint32_t>(std::max<std::size_t>(point_light_shadow_maps_count, 1) *
                                      POINT_LIGHT_CUBE_FACE_COUNT);
}

/** @brief How many layers the directional shadow texture needs (Bevy
 *  `render/light.rs:1080-1082`): the directional cascades plus the spot-light
 *  shadow maps, again at least one.
 *
 *  The two share this single array - which is why a spot light's shadow map index
 *  is offset by the cascade count rather than starting at zero - and the spot
 *  count is already capped by the layers left over after the cascades. */
EPIX_EXPORT inline std::uint32_t directional_light_shadow_texture_layers(std::size_t num_directional_cascades_enabled,
                                                                        std::size_t spot_light_shadow_maps_count) {
    return static_cast<std::uint32_t>(
        std::max<std::size_t>(num_directional_cascades_enabled + spot_light_shadow_maps_count, 1));
}

/** @brief The side of the directional shadow map texture (Bevy
 *  `render/light.rs:1076-1079`): the configured size clamped to the device's
 *  maximum 2D dimension, so an oversized request degrades rather than failing.
 *  A point light's is not clamped separately, since its cubemap faces are the
 *  same size. */
EPIX_EXPORT inline std::uint32_t directional_light_shadow_texture_size(std::size_t shadow_map_size,
                                                                      std::size_t max_texture_dimension_2d) {
    return static_cast<std::uint32_t>(std::min(shadow_map_size, max_texture_dimension_2d));
}

/** @brief How many point lights get shadow maps (Bevy `render/light.rs:1250-1253`):
 *  the first `point_light_count` in the sorted list, capped by the device's
 *  cubemap count. The lights are sorted shadow-casting first, so the cap keeps the
 *  ones that matter, and each of these gets `POINT_LIGHT_CUBE_FACE_COUNT` views. */
EPIX_EXPORT inline std::size_t point_light_shadow_view_count(std::size_t point_light_count,
                                                             std::size_t max_texture_cubes) {
    return std::min(point_light_count, max_texture_cubes);
}

/** @brief What a shadow view belongs to (Bevy `LightEntity`,
 *  `light.rs:690-703`): a directional light's cascade, a point light's cubemap
 *  face, or a spot light. */
EPIX_EXPORT struct LightEntity {
    enum class Kind {
        Directional,
        Point,
        Spot,
    } kind = Kind::Spot;

    /** @brief The light this shadow view belongs to. */
    ecs::Entity light_entity{};
    /** @brief Which cascade, when `kind` is `Directional`. */
    std::size_t cascade_index = 0;
    /** @brief Which cubemap face, when `kind` is `Point`. */
    std::size_t face_index = 0;
};

/** @brief The bits a point light's `flags` word carries (Bevy `PointLightFlags`,
 *  `render/light.rs:112-122`); these must match `mesh_view_types.wgsl`.
 *
 *  `SPOT_LIGHT_Y_NEGATIVE` is the odd one: Bevy extracts spot lights *as* point
 *  lights and carries their cone in `spot_light_angles`, so the shader needs to
 *  know which way the cone's Y axis points. */
EPIX_EXPORT struct PointLightFlags {
    static constexpr std::uint32_t SHADOWS_ENABLED                  = 1u << 0;
    static constexpr std::uint32_t SPOT_LIGHT_Y_NEGATIVE            = 1u << 1;
    static constexpr std::uint32_t VOLUMETRIC                       = 1u << 2;
    static constexpr std::uint32_t AFFECTS_LIGHTMAPPED_MESH_DIFFUSE = 1u << 3;
    static constexpr std::uint32_t NONE                             = 0;
    static constexpr std::uint32_t UNINITIALIZED                    = 0xFFFF;
};

/** @brief The render-world twin of a light view's visible-entity list (Bevy
 *  `RenderVisibleMeshEntities`, `bevy_pbr/src/components.rs:8-13`): pairs of the
 *  render entity and the main-world entity it was synced from.
 *
 *  Deliberately not the camera's `VisibleMeshEntities`, which holds main-world
 *  entities only; this is what a light's shadow view actually draws. Bevy also
 *  derives `Deref`/`DerefMut` over the vector, which the public `entities` member
 *  spells in C++. */
EPIX_EXPORT struct RenderVisibleMeshEntities {
    std::vector<std::pair<ecs::Entity, ::epix::render::sync_world::MainEntity>> entities;
};

/** @brief The six faces' visible-entity lists a point light's cubemap view keeps
 *  (Bevy `RenderCubemapVisibleEntities`, `bevy_pbr/src/components.rs:15-38`).
 *
 *  Bevy keeps `data` crate-private and reads it through the four accessors below;
 *  the same split is kept here. */
EPIX_EXPORT struct RenderCubemapVisibleEntities {
    /** @brief The list for cube face @p i (Bevy `RenderCubemapVisibleEntities::get`). */
    const RenderVisibleMeshEntities& get(std::size_t i) const { return data[i]; }
    /** @brief The list for cube face @p i, for writing (Bevy `...::get_mut`). */
    RenderVisibleMeshEntities& get_mut(std::size_t i) { return data[i]; }
    /** @brief The six face lists in order (Bevy `...::iter`). */
    std::span<const RenderVisibleMeshEntities, 6> iter() const noexcept { return data; }
    /** @brief The six face lists in order, for writing (Bevy `...::iter_mut`). */
    std::span<RenderVisibleMeshEntities, 6> iter_mut() noexcept { return data; }

   private:
    std::array<RenderVisibleMeshEntities, 6> data;
};

/** @brief The visible entities of each cascade frustum, per view (Bevy
 *  `RenderCascadesVisibleEntities`, `bevy_pbr/src/components.rs:40-46`).
 *
 *  Bevy keys a map by the view and stores one list per cascade frustum, which is what
 *  the shadow phase walks - the directional counterpart of the cubemap type above,
 *  and the reason the two are shaped differently: a cube has six fixed faces, while a
 *  cascade count is a setting. */
EPIX_EXPORT struct RenderCascadesVisibleEntities {
    /** @brief Render-world view entity to the visible entities of each cascade frustum. */
    std::unordered_map<ecs::Entity, std::vector<RenderVisibleMeshEntities>> entities;
};

/** @brief A point light as extracted for the render world (Bevy
 *  `ExtractedPointLight`, `render/light.rs:69-86`).
 *
 *  Bevy stores a `LinearRgba`, which is a linear `glm::vec4` here. Spot lights
 *  are extracted into this same type, with their cone's inner and outer angles in
 *  `spot_light_angles` - which is why a point light has a spot-light flag in the
 *  shader. */
EPIX_EXPORT struct ExtractedPointLight {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Luminous intensity in lumens per steradian. */
    float intensity = 0.0f;
    float range     = 0.0f;
    float radius    = 0.0f;
    /** @brief The light's global transform (Bevy `ExtractedPointLight::transform`,
     *  `render/light.rs:412` for a point light and `:477` for a spot light - both take
     *  the whole transform). A spot light's cone direction is its forward axis
     *  (`light.rs:920`); only the light's *shadow view* basis is rebuilt from it, by
     *  `spot_light_world_from_view`. */
    glm::mat4 transform{1.0f};
    bool shadows_enabled     = false;
    float shadow_depth_bias  = 0.0f;
    float shadow_normal_bias = 0.0f;
    float shadow_map_near_z  = 0.0f;
    /** @brief The spot-light cone's inner and outer angles, when this point light
     *  is really a spot light. */
    std::optional<std::pair<float, float>> spot_light_angles;
    bool volumetric           = false;
    bool soft_shadows_enabled = false;
    /** @brief Whether this light contributes diffuse light to lightmapped
     *  meshes. */
    bool affects_lightmapped_mesh_diffuse = true;
};

/** @brief A directional light as extracted for the render world (Bevy
 *  `ExtractedDirectionalLight`, `render/light.rs:88-109`).
 *
 *  `cascades` and `frusta` are keyed by view, since a light's cascades are built
 *  per view. */
EPIX_EXPORT struct ExtractedDirectionalLight {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Illuminance in lux. */
    float illuminance = 0.0f;
    glm::mat4 transform{1.0f};
    bool shadows_enabled                  = false;
    bool volumetric                       = false;
    bool affects_lightmapped_mesh_diffuse = true;
    float shadow_depth_bias               = 0.0f;
    float shadow_normal_bias              = 0.0f;
    /** @brief The light's cascade configuration, which the shadow pass reads. */
    CascadeShadowConfig cascade_shadow_config{};
    /** @brief The cascades built for each view. */
    std::unordered_map<ecs::Entity, std::vector<Cascade>> cascades;
    /** @brief The shadow frusta built for each view. */
    std::unordered_map<ecs::Entity, std::vector<camera::Frustum>> frusta;
    /** @brief The render layers the light is visible through. Bevy's default is
     *  layer 0, which is distinct from belonging to none. */
    camera::RenderLayers render_layers = camera::RenderLayers::layer(0);
    /** @brief The soft shadow size, when PCSS is in use. */
    std::optional<float> soft_shadow_size;
    /** @brief Whether this light uses two-phase occlusion culling. */
    bool occlusion_culling      = false;
    float sun_disk_angular_size = 0.0f;
    float sun_disk_intensity    = 0.0f;
};

/** @brief How many lights of each kind a frame has, after the device's limits
 *  (Bevy `prepare_lights`, `render/light.rs:806-853`).
 *
 *  Each count is capped by what the device can bind: point lights and
 *  directional cascades by the cubemap and array-layer limits respectively, and
 *  spot lights by whatever layers the directional shadow maps have not already
 *  taken, since they share one texture array. */
EPIX_EXPORT struct LightCounts {
    /** @brief Point lights, i.e. those without spot angles. */
    std::size_t point_lights                   = 0;
    std::size_t point_light_volumetric_enabled = 0;
    std::size_t point_light_shadow_maps        = 0;
    /** @brief Spot lights, i.e. point lights carrying spot angles. */
    std::size_t spot_lights                   = 0;
    std::size_t spot_light_volumetric_enabled = 0;
    std::size_t spot_light_shadow_maps        = 0;
    /** @brief Directional lights, counted only up to `MAX_DIRECTIONAL_LIGHTS`. */
    std::size_t directional_volumetric_enabled = 0;
    std::size_t directional_shadow_enabled     = 0;
};

/** @brief Count a frame's lights against the device's texture limits (Bevy
 *  `prepare_lights`, `light.rs:806-853`).
 *
 *  `max_texture_array_layers` and `max_texture_cubes` come from the device
 *  (`max_texture_cubes` is a sixth of the layer count, since a cubemap is six
 *  layers). The directional counts are taken first because the spot-light counts
 *  subtract the layers the directional shadow maps occupy. */
EPIX_EXPORT EPIX_API LightCounts count_lights(const std::vector<ExtractedPointLight>& point_lights,
                                            const std::vector<ExtractedDirectionalLight>& directional_lights,
                                            std::size_t max_texture_array_layers,
                                            std::size_t max_texture_cubes);

/** @brief Copy the scene-wide ambient light into the render world (Bevy
 *  `extract_ambient_light_resource`, `render/light.rs:249-267`). */
EPIX_EXPORT EPIX_API void extract_ambient_light_resource(
    ecs::Commands commands, app::Extract<ecs::Res<GlobalAmbientLight>> ambient_light);

/** @brief Prepare all light buffers, shadow textures, views and phases in one
 * guarded flow (Bevy prepare_lights, render/light.rs:704-1670). The writer is
 * sized for the raw eligible camera query before any work; only sorted cameras
 * receive outputs. Actual shadow depth drawing remains a separate consumer. */
EPIX_EXPORT EPIX_API void prepare_lights(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    ecs::Res<wgpu::Queue> render_queue,
    ecs::ResMut<render::render_resource::TextureCache> texture_cache,
    ecs::Res<GlobalAmbientLight> ambient_light,
    ecs::ResMut<LightMeta> light_meta,
    ecs::ResMut<GlobalClusterableObjectMeta> global_light_meta,
    ecs::Res<PointLightShadowMap> point_light_shadow_map,
    ecs::Res<DirectionalLightShadowMap> directional_light_shadow_map,
    ecs::Res<render::camera::SortedCameras> sorted_cameras,
    ecs::Res<render::batching::GpuPreprocessingSupport> gpu_preprocessing_support,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<Shadow>> shadow_render_phases,
    ecs::Query<ecs::Item<ecs::Entity, const render::sync_world::MainEntity&,
                         const render::view::ExtractedView&, const ExtractedClusterConfig&,
                         ecs::Opt<const camera::RenderLayers&>, ecs::Has<render::view::NoIndirectDrawing>,
                         ecs::Opt<const AmbientLight&>>, ecs::With<camera::Camera3d>> views,
    ecs::Query<ecs::Item<ecs::Entity, const render::sync_world::MainEntity&, const ExtractedPointLight&,
                         ecs::Opt<const camera::CubemapFrusta&>, ecs::Opt<const camera::Frustum&>>,
               ecs::Or<ecs::With<camera::CubemapFrusta>, ecs::With<camera::Frustum>>> point_lights,
    ecs::Query<ecs::Item<ecs::Entity, const render::sync_world::MainEntity&,
                         const ExtractedDirectionalLight&>> directional_lights,
    ecs::Query<ecs::Item<ecs::Mut<LightViewEntities>>> light_view_entities,
    ecs::Local<bool> max_directional_lights_warning_emitted,
    ecs::Local<bool> max_cascades_per_light_warning_emitted,
    ecs::Local<std::unordered_set<render::view::RetainedViewEntity>> live_shadow_mapping_lights);

/** @brief Copy each visible light into the render world for the lighting prepare (Bevy
 *  `extract_lights`, `render/light.rs:280-599`).
 *
 *  Point/spot lights are gathered from GlobalVisibleClusterableObjects and require
 *  view visibility. Their previous visible lists are cleared even when they leave
 *  that set, without deleting their extracted light records. Directional lights
 *  instead remove their extracted pair when invisible. View maps and visible meshes
 *  are re-keyed through render identities, retaining placeholder mesh slots.
 *
 *  SunDisk is optional; an absent component uses SunDisk::EARTH. Its parameters
 *  are extracted unchanged and do not alter the light's illuminance or shadows. */
EPIX_EXPORT EPIX_API void extract_lights(
    ecs::Commands commands,
    app::Extract<ecs::Res<PointLightShadowMap>> point_light_shadow_map,
    app::Extract<ecs::Res<DirectionalLightShadowMap>> directional_light_shadow_map,
    app::Extract<ecs::Res<GlobalVisibleClusterableObjects>> global_visible_clusterable,
    ecs::Query<ecs::Entity, ecs::Filter<ecs::With<RenderCubemapVisibleEntities>,
                                      ecs::With<ExtractedPointLight>>> previous_point_lights,
    ecs::Query<ecs::Entity, ecs::Filter<ecs::With<RenderVisibleMeshEntities>,
                                      ecs::With<ExtractedPointLight>>> previous_spot_lights,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const ::epix::render::sync_world::RenderEntity&,
                                     const PointLight&, const ::epix::camera::CubemapVisibleEntities&,
                                     const ::epix::transform::GlobalTransform&,
                                     const ::epix::camera::ViewVisibility&, const ::epix::camera::CubemapFrusta&,
                                     ecs::Opt<const VolumetricLight&>>>> point_lights,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const ::epix::render::sync_world::RenderEntity&,
                                     const SpotLight&, const ::epix::camera::VisibleMeshEntities&,
                                     const ::epix::transform::GlobalTransform&,
                                     const ::epix::camera::ViewVisibility&, const ::epix::camera::Frustum&,
                                     ecs::Opt<const VolumetricLight&>>>> spot_lights,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const ::epix::render::sync_world::RenderEntity&,
                                     const DirectionalLight&, const ::epix::camera::CascadesVisibleEntities&,
                                     const Cascades&, const CascadeShadowConfig&,
                                     const ::epix::camera::CascadesFrusta&,
                                     const ::epix::transform::GlobalTransform&,
                                     const ::epix::camera::ViewVisibility&,
                                     ecs::Opt<const ::epix::camera::RenderLayers&>,
                                     ecs::Opt<const VolumetricLight&>,
                                     ecs::Has<::epix::render::experimental::OcclusionCulling>,
                                     ecs::Opt<const SunDisk&>>, ecs::Without<SpotLight>>>
        directional_lights,
    app::Extract<ecs::Query<ecs::Item<const ::epix::render::sync_world::RenderEntity&>>> mapper,
    ecs::Local<std::size_t> previous_point_lights_len,
    ecs::Local<std::size_t> previous_spot_lights_len);

/** @brief The clusterable form of an extracted point light (Bevy
 *  `point_or_spot_light_to_clusterable`, `light.rs:2362-2375`): a point light
 *  becomes `PointLight`, and one carrying spot angles becomes `SpotLight` with
 *  its outer angle. */
EPIX_EXPORT EPIX_API ClusterableObjectType point_or_spot_light_to_clusterable(const ExtractedPointLight& light);

/** @brief The key point lights are sorted by (Bevy `light.rs:860-865`):
 *  point-light-versus-spot-light and then shadows and volumetrics first, from the
 *  clusterable ordering, with the entity as a stable tiebreak.
 *
 *  Bevy's reasons, in its own order: the shader iterates point lights and spot
 *  lights as contiguous blocks, the sort puts the shadow-casting ones first so a
 *  light's index can select a shadow map, and the entity keys a consistent set
 *  when the light limit cuts the list. */
EPIX_EXPORT EPIX_API std::tuple<std::uint8_t, bool, bool, ecs::Entity> point_light_sort_key(
    ecs::Entity entity, const ExtractedPointLight& light);

/** @brief The key directional lights are sorted by (Bevy `light.rs:877-879`):
 *  volumetric first, then shadows, then the entity as a stable tiebreak.
 *
 *  The flags are Bevy's plain ascending booleans, so `false` sorts before `true`
 *  - which means lights with volumetric and shadows *disabled* come first, the
 *  opposite of what Bevy's comment above the sort describes. The code is what is
 *  ported, and the discrepancy is recorded rather than quietly "corrected",
 *  since the shadow-map indexing downstream is written against whichever order
 *  the sort actually produces. */
EPIX_EXPORT EPIX_API std::tuple<bool, bool, ecs::Entity> directional_light_sort_key(
    ecs::Entity entity, const ExtractedDirectionalLight& light);

/** @brief A spot light's packed cone data (Bevy `prepare_lights`,
 *  `render/light.rs:918-933`): the light direction's x and z components, then the
 *  spot scale and offset that the shader uses to attenuate between the inner and
 *  outer angles.
 *
 *  The scale is `1 / max(cos(inner) - cos(outer), 1e-4)`, so the guard keeps it
 *  finite when the two angles coincide or the inner one is the wider - the cone
 *  the shader computes would otherwise divide by zero. The returned second value
 *  is `tan(outer)`, the tangent the shader needs for the cone test. */
EPIX_EXPORT EPIX_API std::pair<glm::vec4, float> spot_light_custom_data(float inner_angle,
                                                                      float outer_angle,
                                                                      glm::vec3 light_direction);

/** @brief A point light's packed cube-face data (Bevy `render/light.rs:936-946`):
 *  the lower-right 2x2 values of the cube-face projection matrix, which the
 *  shader uses to reconstruct the direction to each of the six faces. */
EPIX_EXPORT inline glm::vec4 point_light_custom_data(const glm::mat4& cube_face_projection) {
    return glm::vec4(cube_face_projection[2][2], cube_face_projection[2][3], cube_face_projection[3][2],
                     cube_face_projection[3][3]);
}

/** @brief Whether a spot light's `SPOT_LIGHT_Y_NEGATIVE` flag is set (Bevy
 *  `render/light.rs:920-923`).
 *
 *  Bevy tests `is_sign_negative`, not `< 0.0`, so a direction of exactly `-0.0`
 *  counts as negative - which is what the shader expects, since it flips the cone
 *  around Y for these lights. */
EPIX_EXPORT inline bool spot_light_y_negative(glm::vec3 light_direction) { return std::signbit(light_direction.y); }

/** @brief A light's colour premultiplied by its intensity, with the inverse
 *  square of the range in w (Bevy `render/light.rs:952-957`).
 *
 *  Bevy multiplies the whole `vec4` including alpha - "we don't use the alpha at
 *  all", so there is no reason to touch only the first three components - then
 *  takes the xyz and puts `1 / range²` in w. */
EPIX_EXPORT inline glm::vec4 pack_light_color_intensity_range(glm::vec4 color, float intensity, float range) {
    return glm::vec4(glm::vec3(color * intensity), 1.0f / (range * range));
}

/** @brief The flags word for a point light, given its position in the sorted
 *  list (Bevy `prepare_lights`, `render/light.rs:887-916`).
 *
 *  The index arithmetic is the point of this function. The lights are sorted so
 *  point lights come first and, within each block, shadow-casting ones first -
 *  so a global index below `point_light_shadow_maps_count` selects a shadow map
 *  directly, while a spot light has to subtract the point-light count to find its
 *  own position in the spot block. Getting either comparison wrong silently
 *  assigns the wrong lights shadow maps.
 *
 *  Three further details: the volumetric flag requires shadows as well as
 *  volumetrics, the lightmapped flag stands alone, and a spot light carries
 *  `SPOT_LIGHT_Y_NEGATIVE` when its forward axis points below the horizon
 *  (`light.rs:920-923`), which is what tells the shader to flip the cone. */
EPIX_EXPORT EPIX_API std::uint32_t point_light_gpu_flags(std::size_t index,
                                                       const ExtractedPointLight& light,
                                                       const LightCounts& counts);

/** @brief How many directional cascade shadow maps the frame needs (Bevy
 *  `prepare_lights`, `render/light.rs:979-1010`).
 *
 *  Bevy sums each view's shadow-casting, layer-matching directional lights - each
 *  contributing `min(cascade bounds, MAX_CASCADES_PER_LIGHT)` cascades - and
 *  takes the largest total across views, then clamps it to the device's array
 *  layer count. The maximum rather than the sum matters because the views are
 *  rendered one at a time. */
EPIX_EXPORT EPIX_API std::size_t num_directional_cascades_enabled(
    const std::vector<camera::RenderLayers>& view_render_layers,
    const std::vector<ExtractedDirectionalLight>& directional_lights,
    std::size_t max_texture_array_layers);

/** @brief The ambient colour a view's lights carry (Bevy `render/light.rs:1230-1231`):
 *  the ambient light's colour, which is already linear, scaled by its
 *  brightness. The light is the camera's override when it has one and the global
 *  ambient otherwise, resolved by the caller. */
EPIX_EXPORT inline glm::vec4 pack_ambient_color(glm::vec4 linear_color, float brightness) {
    return linear_color * brightness;
}

/** @brief The cluster factors a view's lights carry (Bevy `render/light.rs:1232-1237`).
 *
 *  The x and y components are the cluster grid's dimensions divided by the view's
 *  width and height - how much of the screen one cluster covers - while z and w
 *  are `calculate_cluster_factors`' log-based pair, which the shader uses to
 *  recover a view Z from a slice index. */
EPIX_EXPORT inline glm::vec4 pack_cluster_factors(glm::uvec3 cluster_dimensions,
                                                 glm::vec2 viewport_size,
                                                 glm::vec2 log_factors) {
    return glm::vec4(static_cast<float>(cluster_dimensions.x) / viewport_size.x,
                     static_cast<float>(cluster_dimensions.y) / viewport_size.y, log_factors.x, log_factors.y);
}

/** @brief The dimensions word of a view's lights (Bevy `render/light.rs:1238`):
 *  x, y and z are the cluster counts, and w is how many clusters there are in
 *  total. */
EPIX_EXPORT inline glm::uvec4 pack_cluster_dimensions(glm::uvec3 cluster_dimensions) {
    return glm::uvec4(cluster_dimensions, cluster_dimensions.x * cluster_dimensions.y * cluster_dimensions.z);
}

/** @brief The offset that turns a spot light's index into its shadow map's
 *  (Bevy `render/light.rs:1240-1244`).
 *
 *  Bevy derives it in the comment: spot lights begin in the light array at
 *  `point_light_count`, while their shadow maps begin in the directional light
 *  array at the number of directional cascades - so subtracting the point-light
 *  count and adding the cascade count converts one index to the other. Adding the
 *  two in the wrong order, or the wrong way round, silently reads the wrong
 *  shadow map. */
EPIX_EXPORT inline std::int32_t spot_light_shadowmap_offset(std::size_t num_directional_cascades_enabled,
                                                            std::size_t point_light_count) {
    return static_cast<std::int32_t>(num_directional_cascades_enabled) - static_cast<std::int32_t>(point_light_count);
}

/** @brief A directional light's flags and the cascades it was granted (Bevy
 *  `render/light.rs:1171-1196`).
 *
 *  The two are one computation in Bevy because they are coupled: a light only has
 *  its shadow flag set if its cascades still fit in the array layers the earlier
 *  lights have not taken, and the number granted is what the caller adds to the
 *  running total. A light that does not fit keeps its other flags but loses its
 *  shadows silently.
 *
 *  As with point lights, the volumetric flag requires shadows as well as
 *  volumetrics, and the index is a position in the sorted list, which puts
 *  volumetric and shadow-casting lights first. */
EPIX_EXPORT struct DirectionalLightFlagsAndCascades {
    std::uint32_t flags = DirectionalLightFlags::NONE;
    /** @brief The cascades granted, zero when the light does not fit. */
    std::uint32_t num_cascades = 0;
};

/** @brief Compute a directional light's flags and granted cascades (Bevy
 *  `render/light.rs:1171-1196`). */
EPIX_EXPORT EPIX_API DirectionalLightFlagsAndCascades directional_light_gpu_flags(
    const ExtractedDirectionalLight& light,
    std::size_t index,
    std::size_t directional_volumetric_enabled_count,
    std::size_t cascades_used_so_far,
    std::size_t max_texture_array_layers);

/** @brief A directional light's colour premultiplied by its illuminance (Bevy
 *  `render/light.rs:1205-1207`), the whole `vec4` as for point lights. */
EPIX_EXPORT inline glm::vec4 pack_directional_color(glm::vec4 color, float illuminance) { return color * illuminance; }

/** @brief The sentinel a light's `decal_index` carries when it has no clustered
 *  decal (Bevy `render/light.rs:1219-1223`: `unwrap_or(u32::MAX)`).
 *
 *  The same value is used for point and directional lights, and the shader tests
 *  for it rather than reading decal zero. */
EPIX_EXPORT inline constexpr std::uint32_t NO_CLUSTERED_DECAL = 0xFFFFFFFFu;

/** @brief The projection a point light's cube faces use (Bevy
 *  `render/light.rs:900-904,1280-1284`): a 90-degree field of view, since each
 *  cube face spans a quarter turn, with a square aspect and the light's shadow map
 *  near plane. A spot light uses the same matrix with twice its outer angle
 *  (`bevy_light/src/spot_light.rs:187-190`).
 *
 *  Bevy calls that function inline at each site; it is named here because the
 *  parameters are the point, and it names the engine's single implementation
 *  rather than a second copy. The near plane is the light's `shadow_map_near_z`,
 *  and there is no far plane: the projection is infinite, and a light's `range`
 *  bounds what matters. */
EPIX_EXPORT inline glm::mat4 cube_face_projection(float shadow_map_near_z) {
    return camera::perspective_infinite_reverse_rh(std::numbers::pi_v<float> / 2.0f, 1.0f, shadow_map_near_z);
}

/** @brief Where one directional light's cascades sit in a view's shared shadow
 *  array (Bevy `prepare_lights`, `render/light.rs:1484-1525`). */
EPIX_EXPORT struct DirectionalShadowSlice {
    /** @brief The light's position in the view's sorted light list, which is also
     *  its index in the light uniform's directional array. */
    std::size_t light_index = 0;
    /** @brief The first layer of the shared array this light's cascades use, which
     *  is what its `depth_texture_base_index` carries. */
    std::uint32_t base_index = 0;
    /** @brief How many cascades it was granted; zero when it has no shadows. */
    std::uint32_t num_cascades = 0;
};

/** @brief Assign each of a view's directional lights its slice of the shadow
 *  array (Bevy `prepare_lights`, `render/light.rs:1484-1525`).
 *
 *  The lights are walked in sorted order and taken up to `MAX_DIRECTIONAL_LIGHTS`,
 *  skipping any whose render layers do not intersect the view's without consuming
 *  a layer. A light whose flags lack `SHADOWS_ENABLED` - either because it casts
 *  none or because its cascades did not fit the layers left - consumes nothing
 *  and leaves the running index where it was, which is why the index and the
 *  light's position in the list are tracked separately. */
EPIX_EXPORT EPIX_API std::vector<DirectionalShadowSlice> directional_shadow_slices(
    const std::vector<ExtractedDirectionalLight>& directional_lights,
    const camera::RenderLayers& view_layers,
    std::size_t directional_volumetric_enabled_count,
    std::size_t max_texture_array_layers,
    std::size_t* out_total_cascades = nullptr);

}  // namespace epix::pbr
