#include <epix/render/bind_group_layout.hpp>

namespace epix::render {
namespace {
void hash_combine(std::size_t& seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}  // namespace

bool operator==(const BindGroupLayoutDescriptor& lhs, const BindGroupLayoutDescriptor& rhs) noexcept {
    if (lhs.label != rhs.label || lhs.entries.size() != rhs.entries.size()) return false;
    for (std::size_t index = 0; index < lhs.entries.size(); ++index) {
        const auto& a = lhs.entries[index];
        const auto& b = rhs.entries[index];
        if (a.binding != b.binding || a.visibility != b.visibility || a.bindingArraySize != b.bindingArraySize) {
            return false;
        }
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

std::size_t BindGroupLayoutDescriptor::hash() const noexcept {
    std::size_t seed = std::hash<std::string>{}(label);
    hash_combine(seed, entries.size());
    for (const auto& entry : entries) {
        hash_combine(seed, entry.binding);
        hash_combine(seed, static_cast<std::size_t>(entry.visibility));
        hash_combine(seed, static_cast<std::size_t>(entry.bindingArraySize));
        hash_combine(seed, static_cast<std::size_t>(entry.buffer.type));
        hash_combine(seed, static_cast<std::size_t>(bool(entry.buffer.hasDynamicOffset)));
        hash_combine(seed, static_cast<std::size_t>(entry.buffer.minBindingSize));
        hash_combine(seed, static_cast<std::size_t>(entry.sampler.type));
        hash_combine(seed, static_cast<std::size_t>(entry.texture.sampleType));
        hash_combine(seed, static_cast<std::size_t>(entry.texture.viewDimension));
        hash_combine(seed, static_cast<std::size_t>(bool(entry.texture.multisampled)));
        hash_combine(seed, static_cast<std::size_t>(entry.storageTexture.access));
        hash_combine(seed, static_cast<std::size_t>(entry.storageTexture.format));
        hash_combine(seed, static_cast<std::size_t>(entry.storageTexture.viewDimension));
    }
    return seed;
}

std::size_t BindGroupLayoutDescriptorHash::operator()(const BindGroupLayoutDescriptor& descriptor) const noexcept {
    return descriptor.hash();
}

wgpu::BindGroupLayout BindGroupLayoutCache::get(const wgpu::Device& device,
                                                const BindGroupLayoutDescriptor& descriptor) {
    const auto found = m_layouts.find(descriptor);
    if (found != m_layouts.end()) return found->second;
    const std::span<const wgpu::BindGroupLayoutEntry> entries{descriptor.entries};
    const auto layout = device.createBindGroupLayout(
        wgpu::BindGroupLayoutDescriptor().setLabel(descriptor.label.c_str()).setEntries(entries));
    m_layouts.emplace(descriptor, layout);
    return layout;
}
}  // namespace epix::render
