#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render {
namespace detail {
inline void hash_combine(std::size_t& seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}  // namespace detail

/** @brief Hashable description of a bind group layout (Bevy
 * `BindGroupLayoutDescriptor`, `render_resource/pipeline.rs:90-104`).
 *
 * Bevy derives `Hash`/`Eq` over the label and the entries, which is what lets
 * the pipeline cache intern native layouts by content instead of by identity. */
struct BindGroupLayoutDescriptor {
    /** @brief Debug label of the bind group layout. */
    std::string label;
    /** @brief The layout's entries, in binding order. */
    std::vector<wgpu::BindGroupLayoutEntry> entries;

    static BindGroupLayoutDescriptor create(std::string label, std::vector<wgpu::BindGroupLayoutEntry> entries) {
        return BindGroupLayoutDescriptor{std::move(label), std::move(entries)};
    }
    static BindGroupLayoutDescriptor create(std::string label, std::span<const wgpu::BindGroupLayoutEntry> entries) {
        return BindGroupLayoutDescriptor{std::move(label),
                                         std::vector<wgpu::BindGroupLayoutEntry>(entries.begin(), entries.end())};
    }

    /** @brief Chainable label setter (Bevy `BindGroupLayoutDescriptor::new`'s
     * label argument, in the style of the other render descriptors). */
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

    friend bool operator==(const BindGroupLayoutDescriptor& lhs, const BindGroupLayoutDescriptor& rhs) noexcept {
        if (lhs.label != rhs.label || lhs.entries.size() != rhs.entries.size()) return false;
        for (std::size_t index = 0; index < lhs.entries.size(); ++index) {
            const auto& a = lhs.entries[index];
            const auto& b = rhs.entries[index];
            if (a.binding != b.binding || a.visibility != b.visibility) return false;
            if (a.buffer.type != b.buffer.type || bool(a.buffer.hasDynamicOffset) != bool(b.buffer.hasDynamicOffset) ||
                a.buffer.minBindingSize != b.buffer.minBindingSize) {
                return false;
            }
            if (a.sampler.type != b.sampler.type) return false;
            if (a.texture.sampleType != b.texture.sampleType || a.texture.viewDimension != b.texture.viewDimension ||
                bool(a.texture.multisampled) != bool(b.texture.multisampled)) {
                return false;
            }
            if (a.storageTexture.access != b.storageTexture.access || a.storageTexture.format != b.storageTexture.format ||
                a.storageTexture.viewDimension != b.storageTexture.viewDimension) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::size_t hash() const noexcept {
        std::size_t seed = std::hash<std::string>{}(label);
        detail::hash_combine(seed, entries.size());
        for (const auto& entry : entries) {
            detail::hash_combine(seed, entry.binding);
            detail::hash_combine(seed, static_cast<std::size_t>(entry.visibility));
            detail::hash_combine(seed, static_cast<std::size_t>(entry.buffer.type));
            detail::hash_combine(seed, static_cast<std::size_t>(bool(entry.buffer.hasDynamicOffset)));
            detail::hash_combine(seed, static_cast<std::size_t>(entry.buffer.minBindingSize));
            detail::hash_combine(seed, static_cast<std::size_t>(entry.sampler.type));
            detail::hash_combine(seed, static_cast<std::size_t>(entry.texture.sampleType));
            detail::hash_combine(seed, static_cast<std::size_t>(entry.texture.viewDimension));
            detail::hash_combine(seed, static_cast<std::size_t>(bool(entry.texture.multisampled)));
            detail::hash_combine(seed, static_cast<std::size_t>(entry.storageTexture.access));
            detail::hash_combine(seed, static_cast<std::size_t>(entry.storageTexture.format));
            detail::hash_combine(seed, static_cast<std::size_t>(entry.storageTexture.viewDimension));
        }
        return seed;
    }
};

/** @brief Hasher for `BindGroupLayoutDescriptor` keys. */
struct BindGroupLayoutDescriptorHash {
    std::size_t operator()(const BindGroupLayoutDescriptor& descriptor) const noexcept { return descriptor.hash(); }
};

/** @brief Interns native bind group layouts by descriptor (Bevy
 * `BindGroupLayoutCache`, `render_resource/pipeline_cache.rs:211-220`).
 *
 * Bevy keeps this private to `PipelineCache` and exposes it through
 * `PipelineCache::get_bind_group_layout`, which is the entry point its passes
 * use to build bind groups. */
struct BindGroupLayoutCache {
    /** @brief Return the interned layout for `descriptor`, creating it on first
     *  use. Equal descriptors yield the same native layout. */
    wgpu::BindGroupLayout get(const wgpu::Device& device, const BindGroupLayoutDescriptor& descriptor) {
        const auto found = m_layouts.find(descriptor);
        if (found != m_layouts.end()) return found->second;
        const std::span<const wgpu::BindGroupLayoutEntry> entries{descriptor.entries};
        const auto layout = device.createBindGroupLayout(wgpu::BindGroupLayoutDescriptor()
                                                             .setLabel(descriptor.label.c_str())
                                                             .setEntries(entries));
        m_layouts.emplace(descriptor, layout);
        return layout;
    }

    /** @brief Number of interned layouts. */
    [[nodiscard]] std::size_t size() const noexcept { return m_layouts.size(); }

   private:
    std::unordered_map<BindGroupLayoutDescriptor, wgpu::BindGroupLayout, BindGroupLayoutDescriptorHash> m_layouts;
};
}  // namespace epix::render
