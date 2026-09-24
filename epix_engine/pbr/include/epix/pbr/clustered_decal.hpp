#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <epix/render/bind_group_layout_entries.hpp>
#include <epix/render/render_resource.hpp>
#include <optional>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief One clustered decal as the shader sees it (Bevy `RenderClusteredDecal`,
 *  `decal/clustered.rs:188-214`).
 *
 *  The shader back-transforms a world position through `local_from_world` and
 *  reads the four `image_indices` in the order base colour, normal map,
 *  metallic-roughness and emissive; a slot with no texture holds -1. `tag` is
 *  passed through for application use, and the three trailing words are Bevy's
 *  explicit padding.
 *
 *  Bevy derives `Default`, so a default value is the zero matrix with zero
 *  indices; the extraction is what writes real values. */
EPIX_EXPORT struct RenderClusteredDecal {
    /** @brief The inverse of the model matrix, for back-transforming world
     *  positions into model space. */
    glm::mat4 local_from_world{0.0f};
    /** @brief The index of each decal texture in the binding array, in base
     *  colour, normal map, metallic-roughness, emissive order; -1 means the slot
     *  has no texture. */
    std::array<std::int32_t, 4> image_indices{0, 0, 0, 0};
    /** @brief A custom tag available for application-defined purposes. */
    std::uint32_t tag = 0;
    /** @brief Padding. */
    std::uint32_t pad_a = 0;
    /** @brief Padding. */
    std::uint32_t pad_b = 0;
    /** @brief Padding. */
    std::uint32_t pad_c = 0;
};

// Bevy's shader layout: a mat4 (64 bytes, 16-byte aligned), `array<i32, 4>`
// (16), then four scalars (16).
static_assert(sizeof(RenderClusteredDecal) == 96, "RenderClusteredDecal must match the shader's 96 bytes");
static_assert(offsetof(RenderClusteredDecal, image_indices) == 64, "the indices follow the matrix");
static_assert(offsetof(RenderClusteredDecal, tag) == 80, "the tag follows the indices");

/** @brief The decal count a view may use when the device supports partially
 *  bound binding arrays (Bevy `max_view_decals`' first arm,
 *  `decal/clustered.rs:543-551`; the number Bevy picked to cover common use
 *  cases such as bullet holes while keeping the shader from slowing down on very
 *  large scenes). */
EPIX_EXPORT inline constexpr std::uint32_t MAX_VIEW_DECALS_WITH_PARTIALLY_BOUND_BINDING_ARRAY = 1024;
/** @brief The decal count a view may use otherwise (Bevy `max_view_decals`'
 *  second arm, `decal/clustered.rs:552-554`): without partially bound arrays the
 *  whole array is bound, so the cap stays low to avoid paying the maximum
 *  overhead for an empty scene. */
EPIX_EXPORT inline constexpr std::uint32_t MAX_VIEW_DECALS = 8;

/** @brief The maximum number of decals in a view, taking the platform's
 *  partially-bound-binding-array support into account (Bevy `max_view_decals`,
 *  `decal/clustered.rs:536-555`). */
EPIX_EXPORT inline std::uint32_t max_view_decals(bool partially_bound_binding_array_supported) {
    return partially_bound_binding_array_supported ? MAX_VIEW_DECALS_WITH_PARTIALLY_BOUND_BINDING_ARRAY
                                                   : MAX_VIEW_DECALS;
}

/** @brief Whether clustered decals can be used at all (Bevy
 *  `clustered_decals_are_usable`, `decal/clustered.rs:520-534`).
 *
 *  Bevy's other half is the `pbr_clustered_decals` cargo feature, which is not
 *  in Bevy's default feature set, so it is a parameter here in the same way the
 *  other capability facts are: `decals_feature_enabled` is the caller's
 *  `cfg!(feature = "pbr_clustered_decals")`. */
EPIX_EXPORT inline bool clustered_decals_are_usable(bool binding_arrays_are_usable, bool decals_feature_enabled) {
    return binding_arrays_are_usable && decals_feature_enabled;
}

/** @brief The clustered-decal entries: the decals buffer, the decal texture
 *  array and their sampler, or nothing at all when the platform cannot use them
 *  (Bevy `decal::clustered::get_bind_group_layout_entries`,
 *  `decal/clustered.rs:399-420`).
 *
 *  Binding arrays are a precondition rather than a choice here, which is why the
 *  texture entry always carries a count and the function returns `std::nullopt`
 *  instead of a single-texture shape. The three entries land at the
 *  binding-array group's bindings 5, 6 and 7. */
EPIX_EXPORT inline std::optional<std::array<render::render_resource::BindGroupLayoutEntryBuilder, 3>>
clustered_decal_bind_group_layout_entries(bool binding_arrays_are_usable, bool decals_feature_enabled,
                                          bool partially_bound_binding_array_supported) {
    using render::render_resource::binding_types::sampler;
    using render::render_resource::binding_types::storage_buffer_read_only;
    using render::render_resource::binding_types::texture_2d;

    // Without binding arrays there are no entries at all, which is the same
    // shape Bevy returns on Metal and on any WebGL2 or WebGPU build.
    if (!clustered_decals_are_usable(binding_arrays_are_usable, decals_feature_enabled)) return std::nullopt;

    auto decal_textures = texture_2d(wgpu::TextureSampleType::eFloat);
    decal_textures.count(max_view_decals(partially_bound_binding_array_supported));

    return std::array<render::render_resource::BindGroupLayoutEntryBuilder, 3>{
        // `decals`: a read-only storage buffer of the objects above.
        storage_buffer_read_only(false, sizeof(RenderClusteredDecal)),
        // `decal_textures`: one array of the decals' base colour textures.
        decal_textures,
        // `decal_sampler`: every decal image in a scene shares one sampler.
        sampler(wgpu::SamplerBindingType::eFiltering),
    };
}

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief The decals buffer's element is the object representation, so the raw
 *  encoder applies (Bevy derives `ShaderType` and `Pod` on the same struct). */
template <>
struct ShaderTypeInfo<::epix::pbr::RenderClusteredDecal>
    : RawShaderType<::epix::pbr::RenderClusteredDecal> {};
}  // namespace epix::render::render_resource
