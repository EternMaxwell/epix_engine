#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <expected>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::render_resource {

/**
 * @brief Trait to specialize for a bind-group-compatible type (Bevy
 * `AsBindGroup` derive; C++ has no derive macro, so the specialization
 * declares the layout entries and unprepared-resource factory explicitly).
 * Specialize with:
 * - `Data`      - associated data carried by the prepared bind group.
 * - `Param`     - system-param tuple of resources needed to build the group.
 * - `label()`   - stable label for the layout and bind group.
 * - `layout_entries()`  - raw declared layout entries, in binding order.
 * - `unprepared_bind_group(device, layout, component, param)` - owns the
 *   resources. The free `as_bind_group()` helper materializes them by default.
 * @tparam C The component type.
 */
EPIX_EXPORT template <typename C>
struct AsBindGroup;

/** @brief The bind group cannot be generated yet. Retry on the next update
 * (Bevy `AsBindGroupError::RetryNextUpdate`). */
EPIX_EXPORT struct RetryBindGroupNextUpdate {};

/** @brief The specialization must create its bind group itself because its
 * resources cannot be materialized by the default helper (Bevy
 * `AsBindGroupError::CreateBindGroupDirectly`). */
EPIX_EXPORT struct CreateBindGroupDirectly {};

/** @brief An image sampler is incompatible with its declared binding type
 * (Bevy `AsBindGroupError::InvalidSamplerType`). */
EPIX_EXPORT struct InvalidSamplerType {
    std::uint32_t binding = 0;
    std::string provided_sampler_type;
    std::string required_sampler_types;
};

/** @brief Bind-group preparation failures. This tagged union replaces the
 * former enum discriminator and preserves Bevy's payload-bearing errors. */
EPIX_EXPORT using AsBindGroupError =
    std::variant<RetryBindGroupNextUpdate, CreateBindGroupDirectly, InvalidSamplerType>;

[[nodiscard]] inline std::string as_bind_group_error_message(const AsBindGroupError& error) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<T, RetryBindGroupNextUpdate>) {
                return "the bind group could not be generated; retry next update";
            } else if constexpr (std::same_as<T, CreateBindGroupDirectly>) {
                return "the bind group must be created directly by its specialization";
            } else {
                return std::format("binding {} has sampler type '{}' but requires '{}'", value.binding,
                                   value.provided_sampler_type, value.required_sampler_types);
            }
        },
        error);
}

/** @brief Bytes owned by an AsBindGroup binding. This is not directly a
 * WebGPU binding; a higher-level allocator must first materialize it in a
 * buffer, as with Bevy's `OwnedData`. */
EPIX_EXPORT class OwnedData {
   public:
    OwnedData() = default;
    explicit OwnedData(std::vector<std::byte> bytes) : m_bytes(std::move(bytes)) {}

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return m_bytes; }
    [[nodiscard]] std::span<std::byte> bytes_mut() noexcept { return m_bytes; }

   private:
    std::vector<std::byte> m_bytes;
};

/** @brief An owned resource for one bind-group binding (Bevy's
 * `OwnedBindingResource`). Direct wgpu handles are used where C++ WebGPU has
 * no borrowed `BindingResource` counterpart. */
EPIX_EXPORT using OwnedBindingResource =
    std::variant<wgpu::Buffer, std::pair<wgpu::TextureViewDimension, wgpu::TextureView>,
                 std::pair<wgpu::SamplerBindingType, wgpu::Sampler>, OwnedData>;

/** @brief Ordered binding-index/resource ownership shared by prepared and
 * unprepared bind groups (Bevy's `BindingResources`). */
EPIX_EXPORT class BindingResources {
   public:
    using value_type = std::pair<std::uint32_t, OwnedBindingResource>;

    BindingResources() = default;
    explicit BindingResources(std::vector<value_type> resources) : m_resources(std::move(resources)) {}

    [[nodiscard]] static OwnedBindingResource buffer(wgpu::Buffer buffer) { return buffer; }
    [[nodiscard]] static OwnedBindingResource texture_view(wgpu::TextureViewDimension dimension,
                                                           wgpu::TextureView texture_view) {
        return std::pair{dimension, std::move(texture_view)};
    }
    [[nodiscard]] static OwnedBindingResource sampler(wgpu::SamplerBindingType type, wgpu::Sampler sampler) {
        return std::pair{type, std::move(sampler)};
    }
    [[nodiscard]] static OwnedBindingResource data(OwnedData data) { return data; }

    void push_back(std::uint32_t binding, OwnedBindingResource resource) {
        m_resources.emplace_back(binding, std::move(resource));
    }

    [[nodiscard]] std::span<const value_type> resources() const noexcept { return m_resources; }
    [[nodiscard]] std::span<value_type> resources_mut() noexcept { return m_resources; }

    /** @brief Materialize WebGPU entries in preserved input order. `OwnedData`
     * intentionally has no direct entry, so its specialization must use the
     * direct creation path. */
    [[nodiscard]] std::expected<std::vector<wgpu::BindGroupEntry>, AsBindGroupError> entries() const {
        std::vector<wgpu::BindGroupEntry> result;
        result.reserve(m_resources.size());
        for (const auto& [binding, resource] : m_resources) {
            if (const auto* buffer = std::get_if<wgpu::Buffer>(&resource)) {
                result.emplace_back(wgpu::BindGroupEntry()
                                        .setBinding(binding)
                                        .setBuffer(*buffer)
                                        .setOffset(0)
                                        .setSize(std::numeric_limits<std::uint64_t>::max()));
            } else if (const auto* texture =
                           std::get_if<std::pair<wgpu::TextureViewDimension, wgpu::TextureView>>(&resource)) {
                result.emplace_back(wgpu::BindGroupEntry().setBinding(binding).setTextureView(texture->second));
            } else if (const auto* sampler =
                           std::get_if<std::pair<wgpu::SamplerBindingType, wgpu::Sampler>>(&resource)) {
                result.emplace_back(wgpu::BindGroupEntry().setBinding(binding).setSampler(sampler->second));
            } else {
                return std::unexpected(CreateBindGroupDirectly{});
            }
        }
        return result;
    }

   private:
    std::vector<value_type> m_resources;
};

/** @brief Binding ownership before a bind group is constructed (Bevy's
 * `UnpreparedBindGroup`). */
EPIX_EXPORT struct UnpreparedBindGroup {
    BindingResources bindings;
};

/**
 * @brief A prepared bind group plus per-binding resources (Bevy
 * `PreparedBindGroup`).
 */
EPIX_EXPORT struct PreparedBindGroup {
    /** @brief The created bind group. */
    wgpu::BindGroup bind_group;
    /** @brief Ordered resource ownership for all bindings. */
    BindingResources bindings;
};

/** @brief Concept satisfied by valid `AsBindGroup` specializations. */
EPIX_EXPORT template <typename C>
concept AsBindGroupImpl = requires {
    typename AsBindGroup<C>::Data;
    typename AsBindGroup<C>::Param;
    { AsBindGroup<C>::label() } -> std::convertible_to<std::string_view>;
    { AsBindGroup<C>::layout_entries() } -> std::same_as<std::vector<wgpu::BindGroupLayoutEntry>>;
    { AsBindGroup<C>::bind_group_data(std::declval<const C&>()) } -> std::same_as<typename AsBindGroup<C>::Data>;
    {
        AsBindGroup<C>::unprepared_bind_group(std::declval<const wgpu::Device&>(),
                                              std::declval<const wgpu::BindGroupLayout&>(), std::declval<const C&>(),
                                              std::declval<typename AsBindGroup<C>::Param&>())
    } -> std::same_as<std::expected<UnpreparedBindGroup, AsBindGroupError>>;
};

/** @brief Create a bind group layout from the declared entries. */
EPIX_EXPORT inline wgpu::BindGroupLayout create_bind_group_layout(const wgpu::Device& device,
                                                                  std::span<const wgpu::BindGroupLayoutEntry> entries,
                                                                  std::string_view label = "AsBindGroup Layout") {
    return device.createBindGroupLayout(
        wgpu::BindGroupLayoutDescriptor().setLabel(label.data()).setEntries(entries));
}

/** @brief Create a bind group from per-binding entries. */
EPIX_EXPORT inline wgpu::BindGroup create_bind_group(const wgpu::Device& device,
                                                     const wgpu::BindGroupLayout& layout,
                                                     std::span<const wgpu::BindGroupEntry> entries,
                                                     std::string_view label = "AsBindGroup") {
    std::vector<wgpu::BindGroupEntry> raw_entries(entries.begin(), entries.end());
    return device.createBindGroup(
        wgpu::BindGroupDescriptor().setLabel(label.data()).setLayout(layout).setEntries(std::move(raw_entries)));
}

/** @brief Create this type's layout with its stable label (Bevy
 * `AsBindGroup::bind_group_layout`). */
EPIX_EXPORT template <AsBindGroupImpl C>
wgpu::BindGroupLayout bind_group_layout(const wgpu::Device& device) {
    const auto entries = AsBindGroup<C>::layout_entries();
    return create_bind_group_layout(device, entries, AsBindGroup<C>::label());
}

/** @brief Create a prepared bind group from a specialization's unprepared
 * owned resources (Bevy's default `AsBindGroup::as_bind_group`). A C++ free
 * function provides the default without forcing every specialization to
 * duplicate it. */
EPIX_EXPORT template <AsBindGroupImpl C>
std::expected<PreparedBindGroup, AsBindGroupError> as_bind_group(const wgpu::Device& device,
                                                                  const wgpu::BindGroupLayout& layout,
                                                                  const C& component,
                                                                  typename AsBindGroup<C>::Param& param) {
    auto unprepared = AsBindGroup<C>::unprepared_bind_group(device, layout, component, param);
    if (!unprepared) {
        return std::unexpected(std::move(unprepared.error()));
    }
    auto entries = unprepared->bindings.entries();
    if (!entries) {
        return std::unexpected(std::move(entries.error()));
    }
    PreparedBindGroup prepared;
    prepared.bind_group = create_bind_group(device, layout, *entries, AsBindGroup<C>::label());
    prepared.bindings   = std::move(unprepared->bindings);
    return prepared;
}

}  // namespace epix::render::render_resource
