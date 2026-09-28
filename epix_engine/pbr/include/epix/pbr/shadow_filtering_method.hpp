#pragma once

#include <cstdint>

#include <epix/pbr/mesh_pipeline_key.hpp>
#include <epix/render/extract.hpp>

namespace epix::pbr {

/** @brief How a camera's shadows are filtered (Bevy `ShadowFilteringMethod`,
 *  `bevy_light/src/lib.rs:235-262`).
 *
 *  Bevy declares this in `bevy_light` next to the light components; this module folds
 *  `bevy_light` into `pbr`, and the port groups camera-side light settings in their own
 *  header the way `light_markers.hpp` groups the mesh-side markers. */
EPIX_EXPORT enum class ShadowFilteringMethod : std::uint8_t {
    /** @brief Hardware 2x2. Fast but poor quality. */
    Hardware2x2,
    /** @brief A fixed Gaussian blur, good when TAA is not in use - Bevy's default,
     *  worth nine samples and smart filtering for directional and spot lights. */
    Gaussian,
    /** @brief A filter randomized over time, good when `TemporalAntiAliasing` is in
     *  use. */
    Temporal,
};

/** @brief Bevy's `#[default]` for `ShadowFilteringMethod`.
 *
 *  The value matters more than it looks: `Gaussian` is **not** the zero enumerator, and
 *  `MeshPipelineKey`'s shadow-filter field defaults to zero - `HARDWARE_2X2`. So a key
 *  built without consulting this component describes a different filter than a camera
 *  with no component at all, which is the divergence `PBR-115` records. Defaulting to
 *  this constant rather than to the field's zero is what keeps the two in step. */
EPIX_EXPORT inline constexpr ShadowFilteringMethod SHADOW_FILTERING_METHOD_DEFAULT =
    ShadowFilteringMethod::Gaussian;

/** @brief The `MeshPipelineKey` field value for a filtering method (Bevy's `match` in
 *  `check_views_need_specialization`, `render/mesh.rs:309-446`).
 *
 *  `Hardware2x2` is the field's zero value, which is why the mapping is spelled out for
 *  every arm rather than defaulted. */
[[nodiscard]] EPIX_EXPORT inline MeshPipelineKey shadow_filter_method_bits(
    ShadowFilteringMethod method) noexcept {
    switch (method) {
        case ShadowFilteringMethod::Hardware2x2: return MeshPipelineKey::SHADOW_FILTER_METHOD_HARDWARE_2X2;
        case ShadowFilteringMethod::Gaussian: return MeshPipelineKey::SHADOW_FILTER_METHOD_GAUSSIAN;
        case ShadowFilteringMethod::Temporal: return MeshPipelineKey::SHADOW_FILTER_METHOD_TEMPORAL;
    }
    return MeshPipelineKey::SHADOW_FILTER_METHOD_HARDWARE_2X2;
}

}  // namespace epix::pbr

namespace epix::render {

/** @brief Extract the camera shadow filtering method onto its render entity (Bevy
 *  `extract_shadow_filtering_method`, `bevy_pbr/src/render/light.rs:234-245`).
 *
 *  Bevy needs a *system* for this rather than an `ExtractComponent` impl, and its comment
 *  says why: Rust orphan rules forbid implementing a foreign trait on a foreign type, and
 *  `ShadowFilteringMethod` belongs to `bevy_light` while `ExtractComponent` belongs to
 *  `bevy_render`. C++ has no such rule, so this port says the same thing as a
 *  specialization - the mechanism differs, the effect does not.
 *
 *  Without it the view specialisation check cannot see a camera chosen method at all: the
 *  component lives in the main world, that system runs in `Render`, and its query would
 *  find nothing - which is exactly the state this port was in until batch 189. */
template <>
struct ExtractComponent<::epix::pbr::ShadowFilteringMethod> {
    using QueryData   = const ::epix::pbr::ShadowFilteringMethod&;
    using QueryFilter = epix::ecs::Filter<epix::ecs::With<::epix::camera::Camera3d>>;
    using Out         = ::epix::pbr::ShadowFilteringMethod;

    static std::optional<Out> extract_component(const ::epix::pbr::ShadowFilteringMethod& item) {
        return item;
    }
};

}  // namespace epix::render