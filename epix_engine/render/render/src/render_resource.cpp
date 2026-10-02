#include <spdlog/spdlog.h>

#include <cstring>
#include <epix/render/render_resource.hpp>
#include <string>

wgpu::Buffer epix::render::render_resource::create_buffer_with_data(const wgpu::Device& device,
                                                                    std::span<const std::byte> contents,
                                                                    wgpu::BufferUsage usage,
                                                                    std::string_view label) {
    const auto size = static_cast<std::uint64_t>(contents.size());
    const std::string label_str(label);
    auto buffer = device.createBuffer(
        wgpu::BufferDescriptor().setLabel(label_str.c_str()).setSize(size).setUsage(usage).setMappedAtCreation(true));
    // A failed creation still yields a handle in this wrapper, and then
    // `getMappedRange` returns null: copying into it would be a null write, and
    // the invalid buffer would later poison a command buffer. Report it instead.
    auto* mapped = static_cast<std::byte*>(buffer.getMappedRange(0, size));
    if (mapped == nullptr) {
        spdlog::error(
            "[render] create_buffer_with_data: no mapped range for a {} byte buffer with usage {:#x} "
            "(label '{}'); the buffer or device is invalid",
            size, static_cast<std::uint32_t>(usage), label);
        return buffer;
    }
    std::memcpy(mapped, contents.data(), static_cast<std::size_t>(size));
    buffer.unmap();
    return buffer;
}

namespace epix::render::render_resource {

CachedTexture TextureCache::get(const wgpu::Device& device,
                               const TextureCacheKey& key,
                               const wgpu::TextureDescriptor& descriptor) {
    auto& pool = textures[key];
    for (auto& meta : pool) {
        if (!meta.taken) {
            meta.taken                 = true;
            meta.frames_since_last_use = 0;
            return CachedTexture{meta.texture, meta.default_view};
        }
    }

    wgpu::Texture texture  = device.createTexture(descriptor);
    wgpu::TextureView view = texture.createView();
    pool.push_back(detail::CachedTextureMeta{texture, view, true, 0});
    return CachedTexture{texture, view};
}

void TextureCache::update() {
    std::erase_if(textures, [](auto& entry) {
        auto& pool = entry.second;
        for (auto& meta : pool) {
            meta.frames_since_last_use += 1;
            meta.taken = false;
        }
        std::erase_if(pool, [](const detail::CachedTextureMeta& meta) { return meta.frames_since_last_use >= 3; });
        return pool.empty();
    });
}

}  // namespace epix::render::render_resource
