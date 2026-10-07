#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <optional>
#include <utility>
#include <vector>
#endif

#include <epix/render/bind_group_layout_entries.hpp>

namespace epix::render::render_resource {

/** @brief The number of resources that can be stored in a slab when the platform does
 *  not choose one (Bevy `AUTO_BINDLESS_SLAB_RESOURCE_LIMIT`,
 *  `bevy_render/src/render_resource/bindless.rs:25-33`).
 *
 *  Bevy picks 64 on macOS and iOS and 2048 elsewhere; the same split is kept, since
 *  the lower value exists for Metal's resource limits. */
#if defined(__APPLE__)
inline constexpr std::uint32_t AUTO_BINDLESS_SLAB_RESOURCE_LIMIT = 64;
#else
inline constexpr std::uint32_t AUTO_BINDLESS_SLAB_RESOURCE_LIMIT = 2048;
#endif

/** @brief The index of the actual binding in the bind group (Bevy `BindingNumber`,
 *  `bindless.rs:213`): the value written as `@binding` in WGSL. */
EPIX_EXPORT struct BindingNumber {
    std::uint32_t number                               = 0;
    auto operator<=>(const BindingNumber&) const       = default;
    bool operator==(const BindingNumber&) const        = default;
};

/** @brief An index in the bindless index table (Bevy `BindlessIndex`,
 *  `bindless.rs:219`), which is conventionally bound to binding number 0. */
EPIX_EXPORT struct BindlessIndex {
    std::uint32_t index                          = 0;
    auto operator<=>(const BindlessIndex&) const = default;
    bool operator==(const BindlessIndex&) const  = default;
};

}  // namespace epix::render::render_resource

template <>
struct std::hash<epix::render::render_resource::BindlessIndex> {
    /** @brief Bevy derives `Hash`: the allocator maps bindless indices to slots. */
    std::size_t operator()(const epix::render::render_resource::BindlessIndex& index) const noexcept {
        return std::hash<std::uint32_t>{}(index.index);
    }
};

namespace epix::render::render_resource {

/** @brief An exclusive range of bindless indices, Bevy's `Range<BindlessIndex>`
 *  (`bindless.rs:201`). C++ has no equivalent of `Range`, so the two ends travel
 *  together with the two questions they are asked. */
EPIX_EXPORT struct BindlessIndexRange {
    BindlessIndex start{};
    /** @brief One past the last index, as a Rust range's end is. */
    BindlessIndex end{};

    /** @brief Whether @p index falls inside the range (Bevy's `Range::contains`). */
    [[nodiscard]] bool contains(BindlessIndex index) const noexcept {
        return index.index >= start.index && index.index < end.index;
    }
    /** @brief How many indices the range covers. */
    [[nodiscard]] std::uint32_t length() const noexcept { return end.index - start.index; }
};

/** @brief The type of a potentially-bindless resource (Bevy `BindlessResourceType`,
 *  `bindless.rs:117-158`). */
EPIX_EXPORT enum class BindlessResourceType {
    /** @brief No bindless resource: the placeholder that fills holes in a
     *  descriptor's resource list. */
    None,
    /** @brief A storage buffer. */
    Buffer,
    /** @brief A filtering sampler. */
    SamplerFiltering,
    /** @brief A non-filtering sampler. */
    SamplerNonFiltering,
    /** @brief A comparison sampler, typically used for shadow maps. */
    SamplerComparison,
    Texture1d,
    Texture2d,
    /** @brief A 2D texture array. Not a binding array: the members must share a size
     *  and format. */
    Texture2dArray,
    Texture3d,
    TextureCube,
    /** @brief A cubemap array, with the same caveat as `Texture2dArray`. */
    TextureCubeArray,
    /** @brief Multiple instances of plain old data concatenated into one buffer,
     *  Bevy's `#[data]` declaration. It maps to no GPU binding of its own: the
     *  material allocator creates a binding resource for it. */
    DataBuffer,
};

/** @brief The binding numbers of the built-in binding arrays (Bevy `BINDING_NUMBERS`,
 *  `bindless.rs:43-53`).
 *
 *  `bindless.slang` declares the arrays at these numbers, so the two have to move
 *  together; sorted by resource type, which is what makes the lookup below a binary
 *  search on both sides. */
EPIX_EXPORT inline constexpr std::array<std::pair<BindlessResourceType, BindingNumber>, 9> BINDING_NUMBERS{{
    {BindlessResourceType::SamplerFiltering, BindingNumber{1}},
    {BindlessResourceType::SamplerNonFiltering, BindingNumber{2}},
    {BindlessResourceType::SamplerComparison, BindingNumber{3}},
    {BindlessResourceType::Texture1d, BindingNumber{4}},
    {BindlessResourceType::Texture2d, BindingNumber{5}},
    {BindlessResourceType::Texture2dArray, BindingNumber{6}},
    {BindlessResourceType::Texture3d, BindingNumber{7}},
    {BindlessResourceType::TextureCube, BindingNumber{8}},
    {BindlessResourceType::TextureCubeArray, BindingNumber{9}},
}};

/** @brief The binding number of the common array for @p type, or null when the type
 *  has none (Bevy `BindlessResourceType::binding_number`, `bindless.rs:333-338`).
 *
 *  A pointer rather than a value because Bevy returns `Option<&'static BindingNumber>`
 *  - the reference form is what wgpu's binding-array API takes - and because `Buffer`
 *  and `DataBuffer` have per-binding numbers instead of a fixed array. */
[[nodiscard]] inline const BindingNumber* bindless_binding_number(BindlessResourceType type) noexcept {
    const auto found = std::lower_bound(
        BINDING_NUMBERS.begin(), BINDING_NUMBERS.end(), type,
        [](const auto& entry, BindlessResourceType wanted) { return entry.first < wanted; });
    if (found == BINDING_NUMBERS.end() || found->first != type) return nullptr;
    return &found->second;
}

/** @brief The resource type a texture view dimension materializes into (Bevy's
 *  `From<TextureViewDimension> for BindlessResourceType`, `bindless.rs:341-352`). */
[[nodiscard]] inline BindlessResourceType bindless_resource_type_from(wgpu::TextureViewDimension dimension) noexcept {
    switch (dimension) {
        case wgpu::TextureViewDimension::e1D: return BindlessResourceType::Texture1d;
        case wgpu::TextureViewDimension::e2D: return BindlessResourceType::Texture2d;
        case wgpu::TextureViewDimension::e2DArray: return BindlessResourceType::Texture2dArray;
        case wgpu::TextureViewDimension::eCube: return BindlessResourceType::TextureCube;
        case wgpu::TextureViewDimension::eCubeArray: return BindlessResourceType::TextureCubeArray;
        case wgpu::TextureViewDimension::e3D: return BindlessResourceType::Texture3d;
    }
    return BindlessResourceType::None;
}

/** @brief The resource type a sampler binding type materializes into (Bevy's
 *  `From<SamplerBindingType> for BindlessResourceType`, `bindless.rs:354-362`). */
[[nodiscard]] inline BindlessResourceType bindless_resource_type_from(wgpu::SamplerBindingType sampler) noexcept {
    switch (sampler) {
        case wgpu::SamplerBindingType::eFiltering: return BindlessResourceType::SamplerFiltering;
        case wgpu::SamplerBindingType::eNonFiltering: return BindlessResourceType::SamplerNonFiltering;
        case wgpu::SamplerBindingType::eComparison: return BindlessResourceType::SamplerComparison;
    }
    return BindlessResourceType::None;
}

/** @brief The resource limit for a slab (Bevy `BindlessSlabResourceLimit`,
 *  `bindless.rs:65-82`).
 *
 *  Bevy's two-arm enum - `Auto` and `Custom(u32)` - becomes an optional value, the
 *  spelling this port uses where one arm carries no data; `resolve` answers the
 *  actual limit either way. */
EPIX_EXPORT struct BindlessSlabResourceLimit {
    /** @brief Nothing means `Auto`, the platform's tuned default. */
    std::optional<std::uint32_t> custom;

    /** @brief The limit to use (Bevy's `resolve`, `bindless.rs:310-315`). */
    [[nodiscard]] std::uint32_t resolve() const noexcept {
        return custom.value_or(AUTO_BINDLESS_SLAB_RESOURCE_LIMIT);
    }
};

/** @brief A bindless buffer and where it sits (Bevy `BindlessBufferDescriptor`,
 *  `bindless.rs:177-194`).
 *
 *  Buffers differ from samplers and textures: they have no shared binding array, so
 *  each gets its own entry and therefore carries both its `@binding` number and its
 *  index in the index table. */
EPIX_EXPORT struct BindlessBufferDescriptor {
    /** @brief The `@binding` number of the buffer. */
    BindingNumber binding_number{};
    /** @brief Its index in the index table. */
    BindlessIndex bindless_index{};
    /** @brief The size in bytes, when known. */
    std::optional<std::size_t> size;
};

/** @brief The layout of one bindless index table, which maps bindless indices to
 *  indices within the binding arrays (Bevy `BindlessIndexTableDescriptor`,
 *  `bindless.rs:199-207`). */
EPIX_EXPORT struct BindlessIndexTableDescriptor {
    /** @brief The bindless indices this table covers. */
    BindlessIndexRange indices{};
    /** @brief The binding the table itself is bound at. Binding 0 unless
     *  `#[bindless(index_table(binding(B)))]` moved it. */
    BindingNumber binding_number{};
};

/** @brief The bindless resources a type uses (Bevy `BindlessDescriptor`,
 *  `bindless.rs:95-113`). Only present when the type is bindless.
 *
 *  Bevy stores each list as a `Cow<'static, [T]>` so a generated descriptor can be
 *  borrowed and an assembled one - `ExtendedMaterial` building a descriptor from its
 *  base and extension - owned. The port has no extended materials, so the lists are
 *  owned vectors; that is a representation choice forced by that difference rather
 *  than a behavioural one. */
EPIX_EXPORT struct BindlessDescriptor {
    /** @brief The resource type at each bindless index, with `None` filling holes. */
    std::vector<BindlessResourceType> resources;
    /** @brief One entry per bindless buffer, in no particular order. */
    std::vector<BindlessBufferDescriptor> buffers;
    /** @brief One entry per index table, sorted by first bindless index. */
    std::vector<BindlessIndexTableDescriptor> index_tables;
};

/** @brief Creates the bind group layout entries every bindless bind group shares
 *  (Bevy `create_bindless_bind_group_layout_entries`, `bindless.rs:227-306`).
 *
 *  The first entry is the index table itself, at the caller's binding number; the
 *  remaining nine are the common binding arrays, each declared with the slab's
 *  resource limit as its count because that is the widest one can need to be - the
 *  limit is reached exactly when every bindless resource is of the same kind.
 *
 *  All ten are visible to the fragment, vertex and compute stages, as Bevy declares
 *  them. */
[[nodiscard]] inline std::vector<wgpu::BindGroupLayoutEntry> create_bindless_bind_group_layout_entries(
    std::uint32_t bindless_index_table_length,
    std::uint32_t bindless_slab_resource_limit,
    BindingNumber bindless_index_table_binding_number) {
    // Bevy's `NonZeroU32::new(..).expect(..)`: a zero limit would declare a binding
    // array with no slots, which wgpu rejects rather than reporting at the call site.
    assert(bindless_slab_resource_limit != 0);

    const wgpu::ShaderStage kAllStages =
        wgpu::ShaderStage::eFragment | wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eCompute;

    std::vector<wgpu::BindGroupLayoutEntry> entries;
    entries.reserve(10);

    // The index table, at whatever binding the caller reserved for it.
    entries.push_back(binding_types::storage_buffer_read_only(
                          /*has_dynamic_offset=*/false,
                          static_cast<std::uint64_t>(bindless_index_table_length) * sizeof(std::uint32_t))
                          .build(bindless_index_table_binding_number.number, kAllStages));

    // Then the three sampler arrays, at the binding numbers the table above records.
    entries.push_back(binding_types::sampler(wgpu::SamplerBindingType::eFiltering)
                          .count(bindless_slab_resource_limit)
                          .build(1, kAllStages));
    entries.push_back(binding_types::sampler(wgpu::SamplerBindingType::eNonFiltering)
                          .count(bindless_slab_resource_limit)
                          .build(2, kAllStages));
    entries.push_back(binding_types::sampler(wgpu::SamplerBindingType::eComparison)
                          .count(bindless_slab_resource_limit)
                          .build(3, kAllStages));

    // And the six filterable texture arrays.
    const auto filterable = wgpu::TextureSampleType::eFloat;
    entries.push_back(
        binding_types::texture_1d(filterable).count(bindless_slab_resource_limit).build(4, kAllStages));
    entries.push_back(
        binding_types::texture_2d(filterable).count(bindless_slab_resource_limit).build(5, kAllStages));
    entries.push_back(
        binding_types::texture_2d_array(filterable).count(bindless_slab_resource_limit).build(6, kAllStages));
    entries.push_back(
        binding_types::texture_3d(filterable).count(bindless_slab_resource_limit).build(7, kAllStages));
    entries.push_back(
        binding_types::texture_cube(filterable).count(bindless_slab_resource_limit).build(8, kAllStages));
    entries.push_back(
        binding_types::texture_cube_array(filterable).count(bindless_slab_resource_limit).build(9, kAllStages));

    return entries;
}

}  // namespace epix::render::render_resource
