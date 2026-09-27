#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <utility>
#endif
#include <epix/pbr/light_meta.hpp>

namespace epix::pbr {

/** @brief Information that must be identical to place items in the same batch
 *  set in the shadow map phase (Bevy `ShadowBatchSetKey`,
 *  `bevy_pbr/src/render/light.rs:2100-2123`).
 *
 *  The same fields as the opaque 3D key minus the lightmap: shadow-casting
 *  geometry is not lit, so there is nothing lightmap-specific to separate. */
EPIX_EXPORT struct ShadowBatchSetKey {
    /** @brief The identifier of the render pipeline. */
    render::CachedPipelineId pipeline_id;
    /** @brief The function used to draw. */
    render::phase::DrawFunctionId draw_func;
    /** @brief The ID of a bind group specific to the material instance. */
    std::optional<std::uint32_t> material_bind_group_index;
    /** @brief The ID of the slab of GPU memory that contains vertex data. For
     *  non-mesh items, fill this with 0 if your items can be multi-drawn, or
     *  with a unique value if they cannot. */
    render::mesh::SlabId vertex_slab{};
    /** @brief The ID of the slab of GPU memory that contains index data, if
     *  present. For non-mesh items this is empty. */
    std::optional<render::mesh::SlabId> index_slab;

    /** @brief Whether this batch set is indexed, which selects the indexed or
     *  non-indexed indirect-command layout. */
    bool indexed() const noexcept { return index_slab.has_value(); }
    auto operator<=>(const ShadowBatchSetKey&) const = default;
};

/** @brief Data that must be identical to *bin* shadow map items together (Bevy
 *  `ShadowBinKey`, `render/light.rs:2131-2136`): the asset, normally a mesh. */
EPIX_EXPORT struct ShadowBinKey {
    /** @brief The asset that this phase item is associated with. Normally the
     *  mesh ID, but for non-mesh items another asset type. */
    assets::UntypedAssetId asset_id;
    auto operator<=>(const ShadowBinKey&) const = default;
};

/** @brief A binned shadow map phase item (Bevy `Shadow`,
 *  `render/light.rs:2082-2093`).
 *
 *  One item per shadow-casting object per shadow view, where a view is a
 *  directional light's cascade or one face of a point light's cubemap. Its keys
 *  are what group those items into bins and batch sets. */
EPIX_EXPORT struct Shadow {
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    ShadowBatchSetKey batch_set_key_value;
    ShadowBinKey bin_key_value;
    std::pair<std::uint32_t, std::uint32_t> batch_range_value;
    render::phase::PhaseItemExtraIndex extra_index_value{};

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    render::phase::DrawFunctionId draw_function() const noexcept { return batch_set_key_value.draw_func; }
    render::CachedPipelineId cached_pipeline() const noexcept { return batch_set_key_value.pipeline_id; }
    const std::pair<std::uint32_t, std::uint32_t>& batch_range() const noexcept { return batch_range_value; }
    std::pair<std::uint32_t, std::uint32_t>& batch_range() noexcept { return batch_range_value; }
    const render::phase::PhaseItemExtraIndex& extra_index() const noexcept { return extra_index_value; }
    render::phase::PhaseItemExtraIndex& extra_index() noexcept { return extra_index_value; }

    using BinKey      = ShadowBinKey;
    using BatchSetKey = ShadowBatchSetKey;

    static Shadow create(BatchSetKey batch_set_key,
                         BinKey bin_key,
                         std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity,
                         std::pair<std::uint32_t, std::uint32_t> batch_range,
                         render::phase::PhaseItemExtraIndex extra_index) {
        return {std::move(representative_entity), std::move(batch_set_key), std::move(bin_key), batch_range,
                extra_index};
    }
};
static_assert(render::phase::PhaseItem<Shadow>);
static_assert(render::phase::CachedRenderPipelinePhaseItem<Shadow>);
static_assert(render::phase::PhaseItemBatchSetKey<ShadowBatchSetKey>);

/** @brief The rotation of each cubemap face's shadow view (Bevy `prepare_lights`,
 *  `render/light.rs:757-760`): the identity transform aimed at each face's target
 *  with that face's own up vector, precomputed once because every point light's
 *  faces use the same six.
 *
 *  The table's +Z and -Z entries are deliberately swapped relative to their names,
 *  which is what reconciles right-handed view space with left-handed cubemap
 *  coordinates; the forward axis here is -Z, so each entry's -Z points at its
 *  target. */
EPIX_EXPORT EPIX_API std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT> cube_face_view_rotations();

/** @brief One cube face's world-from-view matrix (Bevy `render/light.rs:1273-1288`):
 *  the light's translation combined with the face's rotation, and nothing else.
 *
 *  The light's own scale and rotation are deliberately ignored - scale so the
 *  light's radius and range are not scaled by the view transform, rotation so the
 *  shadow projections stay aligned with the world axes. */
EPIX_EXPORT EPIX_API glm::mat4 point_light_face_world_from_view(
    const std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT>& rotations,
    glm::vec3 light_translation,
    std::size_t face_index);

/** @brief One face's clip-from-world matrix, which is what a shadow pass renders
 *  with (Bevy `render/light.rs:1280-1290`): the cube-face projection against the
 *  face's inverse view. */
EPIX_EXPORT EPIX_API glm::mat4 point_light_face_clip_from_world(
    const std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT>& rotations,
    glm::vec3 light_translation,
    std::size_t face_index,
    float shadow_map_near_z);

/** @brief The texture array layer a point light's face writes to (Bevy
 *  `render/light.rs:1293`): each light owns six consecutive layers of the shared
 *  cubemap array. */
EPIX_EXPORT EPIX_API std::uint32_t point_light_shadow_array_layer(std::size_t light_index, std::size_t face_index);

/** @brief The pass name Bevy gives a point light's face (Bevy
 *  `render/light.rs:1328-1332`), which is what shows up in a capture and a
 *  profiler, so it is worth keeping identical. */
EPIX_EXPORT EPIX_API std::string point_light_shadow_pass_name(std::size_t light_index, std::size_t face_index);

/** @brief One shadow view a view's lights need (Bevy `prepare_lights`,
 *  `render/light.rs:1290-1360` for the point-light case). */
EPIX_EXPORT struct ShadowViewRequest {
    /** @brief The render-world light entity this view belongs to. */
    ecs::Entity light_entity{};
    /** @brief What the view belongs to, as Bevy's `LightEntity` records it. */
    LightEntity owner{};
    /** @brief The layer of the shared shadow texture array the view writes. */
    std::uint32_t array_layer = 0;
    /** @brief The pass name the view is labelled with. */
    std::string pass_name;
    /** @brief The view's projection into the world. */
    glm::mat4 clip_from_world{1.0f};
    /** @brief The shadow map's side in pixels, which is the view's viewport. */
    std::uint32_t viewport_size = 0;
};

/** @brief Every face view a view's shadow-casting point lights need (Bevy
 *  `prepare_lights`, `render/light.rs:1250-1360`).
 *
 *  The caller passes the lights in sorted order, already trimmed to
 *  `point_light_shadow_view_count`, with their translations. Each contributes six
 *  faces on consecutive array layers, each with its own projection. Bevy creates
 *  one single-layer depth view per face and caches it by layer, so a light's six
 *  faces never share an attachment. */
EPIX_EXPORT EPIX_API std::vector<ShadowViewRequest> point_light_shadow_view_requests(
    const std::vector<std::pair<ecs::Entity, glm::vec3>>& shadow_casting_lights,
    float shadow_map_near_z,
    std::uint32_t shadow_map_size);

/** @brief The pass name Bevy gives a directional light's cascade (Bevy
 *  `render/light.rs:1582-1584`). */
EPIX_EXPORT EPIX_API std::string directional_shadow_pass_name(std::size_t light_index, std::size_t cascade_index);

/** @brief Fill a light's GPU cascades from its extracted cascades and its
 *  configuration's bounds (Bevy `render/light.rs:1536-1541`).
 *
 *  Two sources deliberately: the matrix and texel size come from the built
 *  cascade, but `far_bound` comes from `cascade_shadow_config.bounds` rather than
 *  from the cascade itself. The shader uses it to fade the cascade out at its
 *  configured far distance, so taking it from the wrong array would shift every
 *  cascade's fade. At most `MAX_CASCADES_PER_LIGHT` are written. */
EPIX_EXPORT EPIX_API void fill_gpu_directional_cascades(GpuDirectionalLight& light,
                                                      const std::vector<Cascade>& cascades,
                                                      const std::vector<float>& bounds);

/** @brief Every cascade view a view's shadow-casting directional lights need
 *  (Bevy `prepare_lights`, `render/light.rs:1484-1600`).
 *
 *  Each light contributes one view per cascade, on consecutive layers of the
 *  shared array starting at the `base_index` its slice reports. Unlike point and
 *  spot lights, whose faces share an attachment, each directional view gets its
 *  own - Bevy's own note says so, because the view must be cleared per view - and
 *  the layers being distinct is what forces that. The viewport is the configured
 *  shadow map size, and the owner records the cascade index. */
EPIX_EXPORT EPIX_API std::vector<ShadowViewRequest> directional_shadow_view_requests(
    const std::vector<ecs::Entity>& light_entities,
    const std::vector<std::vector<Cascade>>& cascades,
    const std::vector<DirectionalShadowSlice>& slices,
    std::uint32_t shadow_map_size);

/** @brief The six cubemap frusta of a point light (Bevy `update_point_light_frusta`,
 *  `bevy_light/src/point_light.rs:196-244`).
 *
 *  Each face's frustum is its clip-from-world with an explicit far plane at the
 *  light's range, which is what makes the shadow map's far plane follow the light
 *  rather than an arbitrary distance - and what the `custom_far` variant exists
 *  for.
 *
 *  Note what that far plane is: Bevy orients it by the light's *backward* axis and
 *  uses the same one for all six faces, rather than one per face. It therefore cuts
 *  the space behind the light, and the forward faces are bounded by it only
 *  indirectly - a face whose direction is perpendicular to the backward axis keeps
 *  a frustum reaching the range because its own extent stays on the near side. A
 *  reader would expect six far planes; there is one, and this reproduces Bevy.
 *
 *  The light's scale and rotation are ignored here as they are for the views: its
 *  translation alone places them, and `light_back` is the light's backward axis,
 *  the local +Z axis after the engine's handedness fix. */
EPIX_EXPORT EPIX_API std::array<camera::Frustum, POINT_LIGHT_CUBE_FACE_COUNT> point_light_cubemap_frusta(
    glm::vec3 light_translation, glm::vec3 light_back, float range, float shadow_map_near_z);

/** @brief The frustum of each of a directional light's cascades (Bevy
 *  `update_directional_light_frusta`, `bevy_light/src/directional_light.rs:228-240`):
 *  one per cascade, straight from its clip-from-world, keyed by view. */
EPIX_EXPORT EPIX_API std::vector<camera::Frustum> directional_cascade_frusta(const std::vector<Cascade>& cascades);

/** @brief A cascade view's frustum with its near plane pushed to infinity (Bevy
 *  `prepare_lights`, `render/light.rs:1565-1571`).
 *
 *  A cascade's projection is infinite, so its near plane must not cull what is in
 *  front of the light: Bevy replaces the plane's distance with infinity, keeping
 *  its normal and removing the bound. Without this, geometry between the light and
 *  the cascade's near plane would be culled out of shadows it should cast - and the
 *  cascade's own near plane is the light's, not the camera's. */
EPIX_EXPORT EPIX_API camera::Frustum directional_view_frustum(const camera::Frustum& frustum);

/** @brief Fill the frusta of every cascade a light built, keyed by the same views
 *  (Bevy `update_directional_light_frusta`, `bevy_light/src/directional_light.rs:228-240`).
 *
 *  Bevy's system rebuilds the whole map from the light's `Cascades` rather than
 *  patching it, so a view the light no longer has cascades for loses its frusta -
 *  the keys are the *view* entities and must be preserved rather than renumbered.
 *  Only shadow-casting lights are processed; the caller filters those. */
EPIX_EXPORT EPIX_API camera::CascadesFrusta to_cascades_frusta(const Cascades& cascades);

/** @brief Whether a point light's cubemap frusta need rebuilding (Bevy
 *  `update_point_light_frusta`, `bevy_light/src/point_light.rs:205-219`).
 *
 *  Two early-outs, and both matter. The first skips the light when neither it nor
 *  the set of clustered lights changed, which is what keeps a static scene from
 *  recomputing every frustum each frame. The second skips it when it casts no
 *  shadows - the frusta exist to cull meshes into the shadow map, so a light
 *  without shadows has no use for them - or when it is in no cluster, in which case
 *  it is not being lit for any view either.
 *
 *  The change flag is per light: a light whose transform or parameters changed is
 *  rebuilt even when the cluster set did not, and every light is rebuilt when the
 *  cluster set changed, since a light's range may then bound different geometry. */
EPIX_EXPORT EPIX_API bool should_update_point_light_frusta(bool light_changed,
                                                         bool global_lights_changed,
                                                         bool shadows_enabled,
                                                         bool in_global_lights);

/** @brief Whether a directional light's cascade frusta need rebuilding (Bevy
 *  `update_directional_light_frusta`, `bevy_light/src/directional_light.rs:220-226`).
 *
 *  Deliberately simpler than the point-light gate, and the difference is the
 *  point: this one has no change detection and no cluster-membership test, so it
 *  rebuilds the frusta every frame for any light that casts shadows and is itself
 *  visible. Only the light's own visibility matters - the frusta cull meshes into
 *  the cascades, so a light nothing is looking at has none to cull for. Adding a
 *  change check here would be a divergence from Bevy rather than an optimization.
 *
 *  Bevy's query also excludes entities with a `Camera`, which is a query-shape
 *  constraint to keep it from conflicting with camera queries rather than a
 *  runtime condition, so it has no counterpart here. */
EPIX_EXPORT EPIX_API bool should_update_directional_cascade_frusta(bool shadows_enabled, bool light_visible);

/** @brief Bound a visible-entity list's capacity (Bevy `shrink_entities`,
 *  `bevy_light/src/lib.rs:286-300`).
 *
 *  The lists grow to the scene's mesh count and then shrink again as meshes leave
 *  a light, so Bevy only reclaims when the capacity is more than twice the length,
 *  and then only down to half the ratio - a list that is already tight is left
 *  alone, which is what keeps this from reallocating every frame. An empty list
 *  shrinks to nothing, matching Rust's `checked_div` yielding `None` there.
 *
 *  C++ has no `shrink_to(n)`, so the reclaimed case rebuilds the vector at the
 *  computed capacity - what Rust's own `shrink_to` does internally. */
EPIX_EXPORT EPIX_API void shrink_entities(std::vector<ecs::Entity>& visible_entities);

}  // namespace epix::pbr
