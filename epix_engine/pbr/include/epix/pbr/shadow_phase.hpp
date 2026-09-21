#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <utility>
#include <epix/common.hpp>
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

    static Shadow create(BatchSetKey batch_set_key, BinKey bin_key,
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
EPIX_EXPORT inline std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT> cube_face_view_rotations() {
    std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT> rotations{};
    for (std::size_t face = 0; face < camera::CubeMapFaces.size(); ++face) {
        transform::Transform oriented;
        oriented.look_at(camera::CubeMapFaces[face].target, camera::CubeMapFaces[face].up);
        rotations[face] = glm::mat4_cast(oriented.rotation);
    }
    return rotations;
}

/** @brief One cube face's world-from-view matrix (Bevy `render/light.rs:1273-1288`):
 *  the light's translation combined with the face's rotation, and nothing else.
 *
 *  The light's own scale and rotation are deliberately ignored - scale so the
 *  light's radius and range are not scaled by the view transform, rotation so the
 *  shadow projections stay aligned with the world axes. */
EPIX_EXPORT inline glm::mat4 point_light_face_world_from_view(
    const std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT>& rotations, glm::vec3 light_translation,
    std::size_t face_index) {
    glm::mat4 world_from_view = rotations[face_index % POINT_LIGHT_CUBE_FACE_COUNT];
    world_from_view[3]        = glm::vec4(light_translation, 1.0f);
    return world_from_view;
}

/** @brief One face's clip-from-world matrix, which is what a shadow pass renders
 *  with (Bevy `render/light.rs:1280-1290`): the cube-face projection against the
 *  face's inverse view. */
EPIX_EXPORT inline glm::mat4 point_light_face_clip_from_world(
    const std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT>& rotations, glm::vec3 light_translation,
    std::size_t face_index, float shadow_map_near_z) {
    return cube_face_projection(shadow_map_near_z) *
           glm::inverse(point_light_face_world_from_view(rotations, light_translation, face_index));
}

/** @brief The texture array layer a point light's face writes to (Bevy
 *  `render/light.rs:1293`): each light owns six consecutive layers of the shared
 *  cubemap array. */
EPIX_EXPORT inline std::uint32_t point_light_shadow_array_layer(std::size_t light_index, std::size_t face_index) {
    return static_cast<std::uint32_t>(light_index * POINT_LIGHT_CUBE_FACE_COUNT + face_index);
}

/** @brief The pass name Bevy gives a point light's face (Bevy
 *  `render/light.rs:1328-1332`), which is what shows up in a capture and a
 *  profiler, so it is worth keeping identical. */
EPIX_EXPORT inline std::string point_light_shadow_pass_name(std::size_t light_index, std::size_t face_index) {
    return "shadow_point_light_" + std::to_string(light_index) + "_" +
           std::string(camera::face_index_to_name(face_index));
}

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
EPIX_EXPORT inline std::vector<ShadowViewRequest> point_light_shadow_view_requests(
    const std::vector<std::pair<ecs::Entity, glm::vec3>>& shadow_casting_lights, float shadow_map_near_z,
    std::uint32_t shadow_map_size) {
    std::vector<ShadowViewRequest> requests;
    requests.reserve(shadow_casting_lights.size() * POINT_LIGHT_CUBE_FACE_COUNT);

    const auto rotations = cube_face_view_rotations();
    for (std::size_t light_index = 0; light_index < shadow_casting_lights.size(); ++light_index) {
        const auto& [entity, translation] = shadow_casting_lights[light_index];
        for (std::size_t face = 0; face < POINT_LIGHT_CUBE_FACE_COUNT; ++face) {
            requests.push_back(ShadowViewRequest{
                .light_entity  = entity,
                .owner         = LightEntity{.kind = LightEntity::Kind::Point,
                                             .light_entity = entity,
                                             .cascade_index = 0,
                                             .face_index = face},
                .array_layer   = point_light_shadow_array_layer(light_index, face),
                .pass_name     = point_light_shadow_pass_name(light_index, face),
                .clip_from_world = point_light_face_clip_from_world(rotations, translation, face, shadow_map_near_z),
                .viewport_size = shadow_map_size});
        }
    }
    return requests;
}

/** @brief The pass name Bevy gives a directional light's cascade (Bevy
 *  `render/light.rs:1582-1584`). */
EPIX_EXPORT inline std::string directional_shadow_pass_name(std::size_t light_index, std::size_t cascade_index) {
    return "shadow_directional_light_" + std::to_string(light_index) + "_cascade_" + std::to_string(cascade_index);
}

/** @brief Fill a light's GPU cascades from its extracted cascades and its
 *  configuration's bounds (Bevy `render/light.rs:1536-1541`).
 *
 *  Two sources deliberately: the matrix and texel size come from the built
 *  cascade, but `far_bound` comes from `cascade_shadow_config.bounds` rather than
 *  from the cascade itself. The shader uses it to fade the cascade out at its
 *  configured far distance, so taking it from the wrong array would shift every
 *  cascade's fade. At most `MAX_CASCADES_PER_LIGHT` are written. */
EPIX_EXPORT inline void fill_gpu_directional_cascades(GpuDirectionalLight& light,
                                                     const std::vector<Cascade>& cascades,
                                                     const std::vector<float>& bounds) {
    const std::size_t count = std::min({cascades.size(), bounds.size(), MAX_CASCADES_PER_LIGHT});
    for (std::size_t cascade_index = 0; cascade_index < count; ++cascade_index) {
        light.cascades[cascade_index].clip_from_world = cascades[cascade_index].clip_from_world;
        light.cascades[cascade_index].texel_size      = cascades[cascade_index].texel_size;
        light.cascades[cascade_index].far_bound       = bounds[cascade_index];
    }
    for (std::size_t cascade_index = count; cascade_index < MAX_CASCADES_PER_LIGHT; ++cascade_index) {
        light.cascades[cascade_index] = GpuDirectionalCascade{};
    }
}

/** @brief Every cascade view a view's shadow-casting directional lights need
 *  (Bevy `prepare_lights`, `render/light.rs:1484-1600`).
 *
 *  Each light contributes one view per cascade, on consecutive layers of the
 *  shared array starting at the `base_index` its slice reports. Unlike point and
 *  spot lights, whose faces share an attachment, each directional view gets its
 *  own - Bevy's own note says so, because the view must be cleared per view - and
 *  the layers being distinct is what forces that. The viewport is the configured
 *  shadow map size, and the owner records the cascade index. */
EPIX_EXPORT inline std::vector<ShadowViewRequest> directional_shadow_view_requests(
    const std::vector<ecs::Entity>& light_entities, const std::vector<std::vector<Cascade>>& cascades,
    const std::vector<DirectionalShadowSlice>& slices, std::uint32_t shadow_map_size) {
    std::vector<ShadowViewRequest> requests;

    for (const DirectionalShadowSlice& slice : slices) {
        if (slice.num_cascades == 0) continue;
        if (slice.light_index >= light_entities.size() || slice.light_index >= cascades.size()) continue;

        const ecs::Entity entity = light_entities[slice.light_index];
        for (std::uint32_t cascade_index = 0; cascade_index < slice.num_cascades; ++cascade_index) {
            if (cascade_index >= cascades[slice.light_index].size()) break;
            requests.push_back(ShadowViewRequest{
                .light_entity  = entity,
                .owner         = LightEntity{.kind = LightEntity::Kind::Directional,
                                             .light_entity = entity,
                                             .cascade_index = cascade_index,
                                             .face_index = 0},
                .array_layer   = slice.base_index + cascade_index,
                .pass_name     = directional_shadow_pass_name(slice.light_index, cascade_index),
                .clip_from_world = cascades[slice.light_index][cascade_index].clip_from_world,
                .viewport_size = shadow_map_size});
        }
    }
    return requests;
}

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
EPIX_EXPORT inline std::array<camera::Frustum, POINT_LIGHT_CUBE_FACE_COUNT> point_light_cubemap_frusta(
    glm::vec3 light_translation, glm::vec3 light_back, float range, float shadow_map_near_z) {
    const auto rotations = cube_face_view_rotations();

    std::array<camera::Frustum, POINT_LIGHT_CUBE_FACE_COUNT> frusta{};
    for (std::size_t face = 0; face < POINT_LIGHT_CUBE_FACE_COUNT; ++face) {
        const glm::mat4 world_from_view = point_light_face_world_from_view(rotations, light_translation, face);
        const glm::mat4 clip_from_world = cube_face_projection(shadow_map_near_z) * glm::inverse(world_from_view);
        frusta[face] = camera::Frustum::from_clip_from_world_custom_far(clip_from_world, light_translation, light_back,
                                                                      range);
    }
    return frusta;
}

}  // namespace epix::pbr
