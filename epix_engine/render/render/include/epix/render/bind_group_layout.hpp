#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/common.hpp>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render {
/** @brief Hashable description of a bind group layout (Bevy
 * `BindGroupLayoutDescriptor`, `render_resource/pipeline.rs:90-104`).
 *
 * Bevy derives `Hash`/`Eq` over the label and the entries, which is what lets
 * the pipeline cache intern native layouts by content instead of by identity. */
EPIX_EXPORT struct EPIX_API BindGroupLayoutDescriptor {
    /** @brief Debug label of the bind group layout. */
    std::string label;
    /** @brief The layout's entries, in binding order. */
    std::vector<wgpu::BindGroupLayoutEntry> entries;

    /** @brief Create a descriptor from an owned entry vector (Bevy
     *  `BindGroupLayoutDescriptor::new`). */
    static BindGroupLayoutDescriptor create(std::string label, std::vector<wgpu::BindGroupLayoutEntry> entries) {
        return BindGroupLayoutDescriptor{std::move(label), std::move(entries)};
    }
    /** @brief Create a descriptor from the entry builders' span. */
    static BindGroupLayoutDescriptor create(std::string label, std::span<const wgpu::BindGroupLayoutEntry> entries) {
        return BindGroupLayoutDescriptor{std::move(label),
                                         std::vector<wgpu::BindGroupLayoutEntry>(entries.begin(), entries.end())};
    }

    /** @brief Chainable label setter, in the style of the sibling render
     *  descriptors. */
    auto&& set_label(this auto&& self, std::string label) {
        self.label = std::move(label);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Chainable entries setter, from an owned vector. */
    auto&& set_entries(this auto&& self, std::vector<wgpu::BindGroupLayoutEntry> entries) {
        self.entries = std::move(entries);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Chainable entries setter, from the entry builders' span. */
    auto&& set_entries(this auto&& self, std::span<const wgpu::BindGroupLayoutEntry> entries) {
        self.entries = std::vector<wgpu::BindGroupLayoutEntry>(entries.begin(), entries.end());
        return std::forward<decltype(self)>(self);
    }

    friend EPIX_API bool operator==(const BindGroupLayoutDescriptor& lhs,
                                    const BindGroupLayoutDescriptor& rhs) noexcept;
    [[nodiscard]] std::size_t hash() const noexcept;
};

/** @brief Hasher for `BindGroupLayoutDescriptor` keys. */
EPIX_EXPORT struct EPIX_API BindGroupLayoutDescriptorHash {
    std::size_t operator()(const BindGroupLayoutDescriptor& descriptor) const noexcept;
};

/** @brief Interns native bind group layouts by descriptor (Bevy
 * `BindGroupLayoutCache`, `render_resource/pipeline_cache.rs:211-220`).
 *
 * Bevy keeps this private to `PipelineCache` and exposes it through
 * `PipelineCache::get_bind_group_layout`, which is the entry point its passes
 * use to build bind groups. */
EPIX_EXPORT struct EPIX_API BindGroupLayoutCache {
    /** @brief Return the interned layout for `descriptor`, creating it on first
     *  use. Equal descriptors yield the same native layout. */
    wgpu::BindGroupLayout get(const wgpu::Device& device, const BindGroupLayoutDescriptor& descriptor);

    /** @brief Number of interned layouts. */
    [[nodiscard]] std::size_t size() const noexcept { return m_layouts.size(); }

   private:
    std::unordered_map<BindGroupLayoutDescriptor, wgpu::BindGroupLayout, BindGroupLayoutDescriptorHash> m_layouts;
};
}  // namespace epix::render
