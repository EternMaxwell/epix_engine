#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <concepts>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::render_resource {

/** @brief Fixed-size bind-group entries (Bevy `BindGroupEntries`). It is a
 * range over raw `wgpu::BindGroupEntry` values, so it can be passed directly
 * to `BindGroupDescriptor::setEntries`. */
EPIX_EXPORT template <std::size_t N = 1>
class BindGroupEntries {
   public:
    /** @brief Build a raw WebGPU buffer entry. The entries collection assigns
     * the binding number, leaving buffer range ownership explicit. */
    [[nodiscard]] static wgpu::BindGroupEntry buffer_binding(const wgpu::Buffer& buffer,
                                                              std::uint64_t offset,
                                                              std::uint64_t size) {
        return wgpu::BindGroupEntry().setBuffer(buffer).setOffset(offset).setSize(size);
    }

    /** @brief Build a raw WebGPU texture-view entry. */
    [[nodiscard]] static wgpu::BindGroupEntry texture_binding(const wgpu::TextureView& texture_view) {
        return wgpu::BindGroupEntry().setTextureView(texture_view);
    }

    /** @brief Build a raw WebGPU sampler entry. */
    [[nodiscard]] static wgpu::BindGroupEntry sampler_binding(const wgpu::Sampler& sampler) {
        return wgpu::BindGroupEntry().setSampler(sampler);
    }

    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>, wgpu::BindGroupEntry>) && ...)
    static auto sequential(Entries&&... entries) -> BindGroupEntries<sizeof...(Entries)> {
        std::array<wgpu::BindGroupEntry, sizeof...(Entries)> result{std::forward<Entries>(entries)...};
        for (std::uint32_t binding = 0; binding < result.size(); ++binding) result[binding].setBinding(binding);
        return BindGroupEntries<sizeof...(Entries)>{std::move(result)};
    }

    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>, std::pair<std::uint32_t, wgpu::BindGroupEntry>>) && ...)
    static auto with_indices(Entries&&... entries) -> BindGroupEntries<sizeof...(Entries)> {
        return BindGroupEntries<sizeof...(Entries)>{std::array<wgpu::BindGroupEntry, sizeof...(Entries)>{
            ([](auto&& indexed_entry) {
                auto entry = std::move(indexed_entry.second);
                entry.setBinding(indexed_entry.first);
                return entry;
            }(std::forward<Entries>(entries)))...}};
    }

    static std::array<wgpu::BindGroupEntry, 1> single(wgpu::BindGroupEntry entry) {
        entry.setBinding(0);
        return {std::move(entry)};
    }

    [[nodiscard]] std::span<const wgpu::BindGroupEntry> entries() const noexcept { return m_entries; }
    [[nodiscard]] auto begin() const noexcept { return m_entries.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_entries.end(); }

   private:
    template <std::size_t>
    friend class BindGroupEntries;
    explicit BindGroupEntries(std::array<wgpu::BindGroupEntry, N> entries) : m_entries(std::move(entries)) {}
    std::array<wgpu::BindGroupEntry, N> m_entries;
};

/** @brief Dynamically sized bind-group entries (Bevy
 * `DynamicBindGroupEntries`). */
EPIX_EXPORT class DynamicBindGroupEntries {
   public:
    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>, wgpu::BindGroupEntry>) && ...)
    static DynamicBindGroupEntries sequential(Entries&&... entries) {
        DynamicBindGroupEntries result;
        result.extend_sequential(std::forward<Entries>(entries)...);
        return result;
    }

    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>, std::pair<std::uint32_t, wgpu::BindGroupEntry>>) && ...)
    static DynamicBindGroupEntries new_with_indices(Entries&&... entries) {
        DynamicBindGroupEntries result;
        result.extend_with_indices(std::forward<Entries>(entries)...);
        return result;
    }

    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>, wgpu::BindGroupEntry>) && ...)
    DynamicBindGroupEntries& extend_sequential(Entries&&... entries) & {
        const std::uint32_t start = m_entries.empty() ? 0 : m_entries.back().binding + 1;
        std::uint32_t binding = start;
        (m_entries.push_back([&] {
            auto entry = std::forward<Entries>(entries);
            entry.setBinding(binding++);
            return entry;
        }()), ...);
        return *this;
    }

    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>, wgpu::BindGroupEntry>) && ...)
    DynamicBindGroupEntries&& extend_sequential(Entries&&... entries) && {
        extend_sequential(std::forward<Entries>(entries)...);
        return std::move(*this);
    }

    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>, std::pair<std::uint32_t, wgpu::BindGroupEntry>>) && ...)
    DynamicBindGroupEntries& extend_with_indices(Entries&&... entries) & {
        (m_entries.push_back([&] {
            auto entry = std::move(entries.second);
            entry.setBinding(entries.first);
            return entry;
        }()), ...);
        return *this;
    }

    template <typename... Entries>
        requires((std::same_as<std::remove_cvref_t<Entries>, std::pair<std::uint32_t, wgpu::BindGroupEntry>>) && ...)
    DynamicBindGroupEntries&& extend_with_indices(Entries&&... entries) && {
        extend_with_indices(std::forward<Entries>(entries)...);
        return std::move(*this);
    }

    [[nodiscard]] std::span<const wgpu::BindGroupEntry> entries() const noexcept { return m_entries; }
    [[nodiscard]] auto begin() const noexcept { return m_entries.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_entries.end(); }

   private:
    std::vector<wgpu::BindGroupEntry> m_entries;
};

}  // namespace epix::render::render_resource
