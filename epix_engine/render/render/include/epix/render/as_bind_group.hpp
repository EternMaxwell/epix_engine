#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#include <epix/ecs.hpp>
#endif

namespace epix::render::render_resource {

/**
 * @brief One declared binding of an AsBindGroup: binding index + layout entry.
 */
EPIX_EXPORT struct BindGroupLayoutEntryInfo {
    /** @brief The binding index (matches the shader's binding). */
    std::uint32_t binding = 0;
    /** @brief The layout entry for this binding. */
    wgpu::BindGroupLayoutEntry entry;
};

/** @brief Build a uniform-buffer layout entry (Bevy #[uniform(..)]). */
EPIX_EXPORT inline BindGroupLayoutEntryInfo uniform_binding(std::uint32_t binding, wgpu::ShaderStage stage,
                                                            std::uint64_t min_binding_size = 0) {
    BindGroupLayoutEntryInfo info;
    info.binding = binding;
    info.entry.setBinding(binding).setVisibility(stage).setBuffer(
        wgpu::BufferBindingLayout()
            .setType(wgpu::BufferBindingType::eUniform)
            .setHasDynamicOffset(wgpu::Bool(false))  // Bevy derive default
            .setMinBindingSize(min_binding_size));
    return info;
}

/** @brief Build a storage-buffer layout entry (Bevy #[storage(..)]). */
EPIX_EXPORT inline BindGroupLayoutEntryInfo storage_binding(std::uint32_t binding, wgpu::ShaderStage stage,
                                                            bool read_only = false, std::uint64_t min_binding_size = 0) {
    BindGroupLayoutEntryInfo info;
    info.binding = binding;
    // Bevy derive default: read_only = false (eStorage); callers pass true
    // for the read-only variant.
    info.entry.setBinding(binding).setVisibility(stage).setBuffer(
        wgpu::BufferBindingLayout()
            .setType(read_only ? wgpu::BufferBindingType::eReadOnlyStorage : wgpu::BufferBindingType::eStorage)
            .setHasDynamicOffset(wgpu::Bool(false))
            .setMinBindingSize(min_binding_size));
    return info;
}

/** @brief Build a texture layout entry (Bevy #[texture(..)]). */
EPIX_EXPORT inline BindGroupLayoutEntryInfo texture_binding(
    std::uint32_t binding, wgpu::ShaderStage stage,
    wgpu::TextureSampleType sample_type = wgpu::TextureSampleType::eFloat,
    wgpu::TextureViewDimension dimension = wgpu::TextureViewDimension::e2D) {
    BindGroupLayoutEntryInfo info;
    info.binding = binding;
    info.entry.setBinding(binding).setVisibility(stage).setTexture(
        wgpu::TextureBindingLayout()
            .setSampleType(sample_type)
            .setViewDimension(dimension)
            .setMultisampled(wgpu::Bool(false)));
    return info;
}

/** @brief Build a sampler layout entry (Bevy #[sampler(..)]). */
EPIX_EXPORT inline BindGroupLayoutEntryInfo sampler_binding(
    std::uint32_t binding, wgpu::ShaderStage stage,
    wgpu::SamplerBindingType type = wgpu::SamplerBindingType::eFiltering) {
    BindGroupLayoutEntryInfo info;
    info.binding = binding;
    info.entry.setBinding(binding).setVisibility(stage).setSampler(wgpu::SamplerBindingLayout().setType(type));
    return info;
}

/** @brief Build a storage-texture layout entry (Bevy #[storage_texture(..)]). */
EPIX_EXPORT inline BindGroupLayoutEntryInfo storage_texture_binding(
    std::uint32_t binding, wgpu::ShaderStage stage, wgpu::StorageTextureAccess access, wgpu::TextureFormat format,
    wgpu::TextureViewDimension dimension = wgpu::TextureViewDimension::e2D) {
    BindGroupLayoutEntryInfo info;
    info.binding = binding;
    info.entry.setBinding(binding).setVisibility(stage).setStorageTexture(
        wgpu::StorageTextureBindingLayout().setAccess(access).setFormat(format).setViewDimension(dimension));
    return info;
}

/**
 * @brief Trait to specialize for a bind-group-compatible type (Bevy
 * `AsBindGroup` derive; C++ has no derive macro, so the specialization
 * declares the layout entries and the bind-group factory explicitly).
 * Specialize with:
 * - `Data`      - associated data carried by the prepared bind group.
 * - `Param`     - system-param tuple of resources needed to build the group.
 * - `layout_entries()`  - the declared bindings, in binding order.
 * - `as_bind_group(device, layout, component, param)` - builds the group.
 * @tparam C The component type.
 */
EPIX_EXPORT template <typename C>
struct AsBindGroup;

/**
 * @brief A prepared bind group plus per-binding resources (Bevy
 * `PreparedBindGroup`).
 */
EPIX_EXPORT template <typename C>
struct PreparedBindGroup {
    /** @brief The created bind group. */
    wgpu::BindGroup bind_group;
    /** @brief Associated data (e.g. uniform buffers kept alive). */
    typename AsBindGroup<C>::Data data{};
    /** @brief Late-bound buffer resources per binding index. */
    std::vector<std::pair<std::uint32_t, wgpu::Buffer>> buffer_bindings;
    /** @brief Late-bound texture view resources per binding index. */
    std::vector<std::pair<std::uint32_t, wgpu::TextureView>> texture_bindings;
    /** @brief Late-bound sampler resources per binding index. */
    std::vector<std::pair<std::uint32_t, wgpu::Sampler>> sampler_bindings;
};

/** @brief Concept satisfied by valid `AsBindGroup` specializations. */
EPIX_EXPORT template <typename C>
concept AsBindGroupImpl = requires {
    typename AsBindGroup<C>::Data;
    typename AsBindGroup<C>::Param;
    { AsBindGroup<C>::layout_entries() } -> std::same_as<std::vector<BindGroupLayoutEntryInfo>>;
    { AsBindGroup<C>::as_bind_group(std::declval<const wgpu::Device&>(),
                                    std::declval<const wgpu::BindGroupLayout&>(),
                                    std::declval<const C&>(),
                                    std::declval<typename AsBindGroup<C>::Param&>()) }
        -> std::same_as<PreparedBindGroup<C>>;
};

/** @brief Create a bind group layout from the declared entries. */
EPIX_EXPORT inline wgpu::BindGroupLayout create_bind_group_layout(const wgpu::Device& device,
                                                                  std::span<const BindGroupLayoutEntryInfo> entries,
                                                                  std::string_view label = "AsBindGroup Layout") {
    std::vector<wgpu::BindGroupLayoutEntry> raw_entries;
    raw_entries.reserve(entries.size());
    for (const auto& info : entries) {
        raw_entries.push_back(info.entry);
    }
    return device.createBindGroupLayout(wgpu::BindGroupLayoutDescriptor()
                                            .setLabel(label.data())
                                            .setEntries(std::move(raw_entries)));
}

/** @brief Create a bind group from per-binding entries. */
EPIX_EXPORT inline wgpu::BindGroup create_bind_group(const wgpu::Device& device,
                                                     const wgpu::BindGroupLayout& layout,
                                                     std::span<const wgpu::BindGroupEntry> entries,
                                                     std::string_view label = "AsBindGroup") {
    std::vector<wgpu::BindGroupEntry> raw_entries(entries.begin(), entries.end());
    return device.createBindGroup(wgpu::BindGroupDescriptor()
                                      .setLabel(label.data())
                                      .setLayout(layout)
                                      .setEntries(std::move(raw_entries)));
}

/** @brief Errors that can occur while creating bind group data (Bevy
 * `AsBindGroupError`). */
EPIX_EXPORT enum class AsBindGroupError {
    /** @brief Failed to create a texture. */
    CreateTexture,
    /** @brief Failed to create a texture view. */
    CreateTextureView,
    /** @brief Failed to create a sampler. */
    CreateSampler,
    /** @brief Failed to create a buffer. */
    CreateBuffer,
    /** @brief Failed to create the bind group. */
    CreateBindGroup,
};

}  // namespace epix::render::render_resource
