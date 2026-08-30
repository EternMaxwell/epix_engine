#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::render_resource {

/** @brief Deferred binding number, visibility, and resource layout (Bevy
 * `BindGroupLayoutEntryBuilder`). Binding-array counts are unavailable until
 * R1 bindless support can be implemented over wgpu-native. */
EPIX_EXPORT class BindGroupLayoutEntryBuilder {
   public:
    explicit BindGroupLayoutEntryBuilder(wgpu::BindGroupLayoutEntry entry,
                                        bool preserve_visibility = false)
        : m_entry(std::move(entry)),
          m_visibility(preserve_visibility ? std::optional{m_entry.visibility} : std::nullopt) {}

    BindGroupLayoutEntryBuilder& visibility(wgpu::ShaderStage visibility) & {
        m_visibility = visibility;
        return *this;
    }
    BindGroupLayoutEntryBuilder&& visibility(wgpu::ShaderStage visibility) && {
        m_visibility = visibility;
        return std::move(*this);
    }

    [[nodiscard]] wgpu::BindGroupLayoutEntry build(std::uint32_t binding,
                                                    wgpu::ShaderStage default_visibility) const {
        auto entry = m_entry;
        entry.setBinding(binding).setVisibility(m_visibility.value_or(default_visibility));
        return entry;
    }

   private:
    wgpu::BindGroupLayoutEntry m_entry;
    std::optional<wgpu::ShaderStage> m_visibility;
};

namespace detail {
template <typename T>
concept BindGroupLayoutEntryInput = std::same_as<std::remove_cvref_t<T>, BindGroupLayoutEntryBuilder> ||
                                    std::same_as<std::remove_cvref_t<T>, wgpu::BindGroupLayoutEntry>;

inline BindGroupLayoutEntryBuilder make_layout_entry(wgpu::BindGroupLayoutEntry entry) {
    return BindGroupLayoutEntryBuilder{std::move(entry)};
}
inline BindGroupLayoutEntryBuilder to_builder(BindGroupLayoutEntryBuilder entry) { return entry; }
inline BindGroupLayoutEntryBuilder to_builder(wgpu::BindGroupLayoutEntry entry) {
    // Raw WebGPU descriptors retain explicit visibility; collections only
    // assign their binding numbers.
    return BindGroupLayoutEntryBuilder{std::move(entry), true};
}
}  // namespace detail

/** @brief Fixed-size layout entries (Bevy `BindGroupLayoutEntries`). */
EPIX_EXPORT template <std::size_t N = 1>
class BindGroupLayoutEntries {
   public:
    template <typename... Entries>
        requires((detail::BindGroupLayoutEntryInput<Entries>) && ...)
    static auto sequential(wgpu::ShaderStage default_visibility, Entries&&... entries)
        -> BindGroupLayoutEntries<sizeof...(Entries)> {
        std::array<BindGroupLayoutEntryBuilder, sizeof...(Entries)> builders{
            detail::to_builder(std::forward<Entries>(entries))...};
        std::array<wgpu::BindGroupLayoutEntry, sizeof...(Entries)> result{};
        for (std::uint32_t binding = 0; binding < result.size(); ++binding) {
            result[binding] = builders[binding].build(binding, default_visibility);
        }
        return BindGroupLayoutEntries<sizeof...(Entries)>{std::move(result)};
    }

    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>,
                               std::pair<std::uint32_t, BindGroupLayoutEntryBuilder>> ||
                  std::same_as<std::remove_cvref_t<Entries>, std::pair<std::uint32_t, wgpu::BindGroupLayoutEntry>>) && ...)
    static auto with_indices(wgpu::ShaderStage default_visibility, Entries&&... entries)
        -> BindGroupLayoutEntries<sizeof...(Entries)> {
        return BindGroupLayoutEntries<sizeof...(Entries)>{std::array<wgpu::BindGroupLayoutEntry, sizeof...(Entries)>{
            (detail::to_builder(std::move(entries.second)).build(entries.first, default_visibility))...}};
    }

    template <detail::BindGroupLayoutEntryInput Entry>
    static std::array<wgpu::BindGroupLayoutEntry, 1> single(wgpu::ShaderStage visibility, Entry&& entry) {
        return {detail::to_builder(std::forward<Entry>(entry)).build(0, visibility)};
    }

    [[nodiscard]] std::span<const wgpu::BindGroupLayoutEntry> entries() const noexcept { return m_entries; }
    [[nodiscard]] auto begin() const noexcept { return m_entries.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_entries.end(); }

   private:
    template <std::size_t>
    friend class BindGroupLayoutEntries;
    explicit BindGroupLayoutEntries(std::array<wgpu::BindGroupLayoutEntry, N> entries) : m_entries(std::move(entries)) {}
    std::array<wgpu::BindGroupLayoutEntry, N> m_entries;
};

/** @brief Dynamic layout entries (Bevy `DynamicBindGroupLayoutEntries`). */
EPIX_EXPORT class DynamicBindGroupLayoutEntries {
   public:
    explicit DynamicBindGroupLayoutEntries(wgpu::ShaderStage default_visibility) : m_default_visibility(default_visibility) {}

    template <typename... Entries>
        requires((detail::BindGroupLayoutEntryInput<Entries>) && ...)
    static DynamicBindGroupLayoutEntries sequential(wgpu::ShaderStage default_visibility, Entries&&... entries) {
        DynamicBindGroupLayoutEntries result{default_visibility};
        result.extend_sequential(std::forward<Entries>(entries)...);
        return result;
    }

    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>,
                               std::pair<std::uint32_t, BindGroupLayoutEntryBuilder>> ||
                  std::same_as<std::remove_cvref_t<Entries>, std::pair<std::uint32_t, wgpu::BindGroupLayoutEntry>>) && ...)
    static DynamicBindGroupLayoutEntries new_with_indices(wgpu::ShaderStage default_visibility,
                                                           Entries&&... entries) {
        DynamicBindGroupLayoutEntries result{default_visibility};
        result.extend_with_indices(std::move(entries)...);
        return result;
    }

    template <typename... Entries>
        requires((detail::BindGroupLayoutEntryInput<Entries>) && ...)
    DynamicBindGroupLayoutEntries& extend_sequential(Entries&&... entries) & {
        std::uint32_t binding = m_entries.empty() ? 0 : m_entries.back().binding + 1;
        (m_entries.push_back(detail::to_builder(std::forward<Entries>(entries)).build(binding++, m_default_visibility)), ...);
        return *this;
    }
    template <typename... Entries>
        requires((detail::BindGroupLayoutEntryInput<Entries>) && ...)
    DynamicBindGroupLayoutEntries&& extend_sequential(Entries&&... entries) && {
        extend_sequential(std::forward<Entries>(entries)...);
        return std::move(*this);
    }
    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>,
                               std::pair<std::uint32_t, BindGroupLayoutEntryBuilder>> ||
                  std::same_as<std::remove_cvref_t<Entries>, std::pair<std::uint32_t, wgpu::BindGroupLayoutEntry>>) && ...)
    DynamicBindGroupLayoutEntries& extend_with_indices(Entries&&... entries) & {
        (m_entries.push_back(
             detail::to_builder(std::move(entries.second)).build(entries.first, m_default_visibility)),
         ...);
        return *this;
    }
    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>,
                               std::pair<std::uint32_t, BindGroupLayoutEntryBuilder>> ||
                  std::same_as<std::remove_cvref_t<Entries>, std::pair<std::uint32_t, wgpu::BindGroupLayoutEntry>>) && ...)
    DynamicBindGroupLayoutEntries&& extend_with_indices(Entries&&... entries) && {
        extend_with_indices(std::forward<Entries>(entries)...);
        return std::move(*this);
    }

    [[nodiscard]] std::span<const wgpu::BindGroupLayoutEntry> entries() const noexcept { return m_entries; }
    [[nodiscard]] auto begin() const noexcept { return m_entries.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_entries.end(); }

   private:
    wgpu::ShaderStage m_default_visibility;
    std::vector<wgpu::BindGroupLayoutEntry> m_entries;
};

/** @brief Bevy's `binding_types` helpers, expressed through the WebGPU C++
 * descriptor API. */
namespace binding_types {
inline BindGroupLayoutEntryBuilder storage_buffer(bool has_dynamic_offset, std::uint64_t min_binding_size = 0) {
    return detail::make_layout_entry(wgpu::BindGroupLayoutEntry().setBuffer(
        wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eStorage)
            .setHasDynamicOffset(wgpu::Bool(has_dynamic_offset)).setMinBindingSize(min_binding_size)));
}
inline BindGroupLayoutEntryBuilder storage_buffer_read_only(bool has_dynamic_offset,
                                                            std::uint64_t min_binding_size = 0) {
    return detail::make_layout_entry(wgpu::BindGroupLayoutEntry().setBuffer(
        wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage)
            .setHasDynamicOffset(wgpu::Bool(has_dynamic_offset)).setMinBindingSize(min_binding_size)));
}
inline BindGroupLayoutEntryBuilder uniform_buffer(bool has_dynamic_offset, std::uint64_t min_binding_size = 0) {
    return detail::make_layout_entry(wgpu::BindGroupLayoutEntry().setBuffer(
        wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eUniform)
            .setHasDynamicOffset(wgpu::Bool(has_dynamic_offset)).setMinBindingSize(min_binding_size)));
}
inline BindGroupLayoutEntryBuilder texture(wgpu::TextureSampleType sample_type,
                                           wgpu::TextureViewDimension dimension,
                                           bool multisampled = false) {
    return detail::make_layout_entry(wgpu::BindGroupLayoutEntry().setTexture(
        wgpu::TextureBindingLayout().setSampleType(sample_type).setViewDimension(dimension)
            .setMultisampled(wgpu::Bool(multisampled))));
}
inline BindGroupLayoutEntryBuilder texture_1d(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::e1D);
}
inline BindGroupLayoutEntryBuilder texture_2d(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::e2D);
}
inline BindGroupLayoutEntryBuilder texture_2d_multisampled(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::e2D, true);
}
inline BindGroupLayoutEntryBuilder texture_2d_array(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::e2DArray);
}
inline BindGroupLayoutEntryBuilder texture_2d_array_multisampled(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::e2DArray, true);
}
inline BindGroupLayoutEntryBuilder texture_depth_2d() { return texture_2d(wgpu::TextureSampleType::eDepth); }
inline BindGroupLayoutEntryBuilder texture_depth_2d_multisampled() {
    return texture_2d_multisampled(wgpu::TextureSampleType::eDepth);
}
inline BindGroupLayoutEntryBuilder texture_cube(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::eCube);
}
inline BindGroupLayoutEntryBuilder texture_cube_multisampled(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::eCube, true);
}
inline BindGroupLayoutEntryBuilder texture_cube_array(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::eCubeArray);
}
inline BindGroupLayoutEntryBuilder texture_cube_array_multisampled(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::eCubeArray, true);
}
inline BindGroupLayoutEntryBuilder texture_3d(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::e3D);
}
inline BindGroupLayoutEntryBuilder texture_3d_multisampled(wgpu::TextureSampleType sample_type) {
    return texture(sample_type, wgpu::TextureViewDimension::e3D, true);
}
inline BindGroupLayoutEntryBuilder sampler(wgpu::SamplerBindingType sampler_type) {
    return detail::make_layout_entry(wgpu::BindGroupLayoutEntry().setSampler(wgpu::SamplerBindingLayout().setType(sampler_type)));
}
inline BindGroupLayoutEntryBuilder texture_storage(wgpu::TextureFormat format,
                                                    wgpu::StorageTextureAccess access,
                                                    wgpu::TextureViewDimension dimension) {
    return detail::make_layout_entry(wgpu::BindGroupLayoutEntry().setStorageTexture(
        wgpu::StorageTextureBindingLayout().setFormat(format).setAccess(access).setViewDimension(dimension)));
}
inline BindGroupLayoutEntryBuilder texture_storage_2d(wgpu::TextureFormat format, wgpu::StorageTextureAccess access) {
    return texture_storage(format, access, wgpu::TextureViewDimension::e2D);
}
inline BindGroupLayoutEntryBuilder texture_storage_2d_array(wgpu::TextureFormat format,
                                                             wgpu::StorageTextureAccess access) {
    return texture_storage(format, access, wgpu::TextureViewDimension::e2DArray);
}
inline BindGroupLayoutEntryBuilder texture_storage_3d(wgpu::TextureFormat format, wgpu::StorageTextureAccess access) {
    return texture_storage(format, access, wgpu::TextureViewDimension::e3D);
}
}  // namespace binding_types

}  // namespace epix::render::render_resource
