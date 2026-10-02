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

namespace {

/** @brief The alignment a buffer copy's offset and size must satisfy (wgpu
 *  `COPY_BUFFER_ALIGNMENT`). The allocator's rounded slot size is a multiple. */
constexpr std::uint64_t kCopyBufferAlignment = 4;

}  // namespace

namespace epix::render::render_resource {

StagingBelt::StagingBelt(const wgpu::Device& device, const wgpu::Queue& queue, std::uint64_t chunk_size)
    : m_device(device), m_queue(queue), m_queue_is_set(static_cast<bool>(queue)),
      m_chunk_size(chunk_size == 0 ? 64 * 1024 : chunk_size) {}

StagingBelt::Chunk* StagingBelt::acquire_chunk(std::uint64_t size) {
    // A request larger than a chunk gets a chunk of its own, as wgpu's belt does.
    const std::uint64_t capacity = size > m_chunk_size ? size : m_chunk_size;
    while (m_current_chunk < m_chunks.size()) {
        auto& chunk = m_chunks[m_current_chunk];
        if (chunk.capacity >= capacity && chunk.cursor + size <= chunk.capacity) return &chunk;
        ++m_current_chunk;
    }
    auto buffer = m_device.createBuffer(wgpu::BufferDescriptor()
                                            .setLabel("staging belt chunk")
                                            .setSize(capacity)
                                            .setUsage(wgpu::BufferUsage::eCopySrc)
                                            .setMappedAtCreation(true));
    m_chunks.push_back(Chunk{std::move(buffer), capacity, 0});
    return &m_chunks[m_current_chunk];
}

bool StagingBelt::write_buffer(const wgpu::Buffer& destination,
                               std::uint64_t destination_offset,
                               std::uint64_t size,
                               const std::function<void(std::span<std::byte>)>& fill) {
    if (!is_usable() || size == 0 || !destination) return false;
    if (size % kCopyBufferAlignment != 0 || destination_offset % kCopyBufferAlignment != 0) {
        spdlog::error("[render] staging belt: refusing a {} byte copy at offset {} - copies must be {}-byte aligned",
                      size, destination_offset, kCopyBufferAlignment);
        return false;
    }
    auto* chunk  = acquire_chunk(size);
    auto* mapped = static_cast<std::byte*>(chunk->buffer.getMappedRange(chunk->cursor, size));
    if (mapped == nullptr) {
        // Same failure `create_buffer_with_data` reports: a failed creation still yields a
        // handle in this wrapper, and then there is no range to write into.
        spdlog::error("[render] staging belt: no mapped range for a {} byte chunk; the write is dropped", size);
        return false;
    }
    fill(std::span<std::byte>(mapped, static_cast<std::size_t>(size)));
    m_pending.push_back(PendingCopy{chunk->buffer, chunk->cursor, destination, destination_offset, size});
    chunk->cursor += size;
    return true;
}

void StagingBelt::finish() {
    if (m_pending.empty()) return;
    auto encoder = m_device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("staging belt copy"));
    if (!encoder) {
        spdlog::error("[render] staging belt: no command encoder for {} recorded copies; they are dropped",
                      m_pending.size());
        m_pending.clear();
        return;
    }
    for (const auto& copy : m_pending) {
        encoder.copyBufferToBuffer(copy.source, copy.source_offset, copy.destination, copy.destination_offset, copy.size);
    }
    // Unmap before submitting: a mapped-at-creation buffer has to be unmapped before it
    // takes part in a submission, and these chunks are not reused afterwards (re-mapping
    // one needs `mapAsync` and a poll - see PRT-1).
    for (auto& chunk : m_chunks) {
        if (chunk.buffer) chunk.buffer.unmap();
    }
    m_queue.submit(encoder.finish());
    m_pending.clear();
    m_used_chunks.clear();
    m_chunks.clear();
    m_current_chunk = 0;
}

}  // namespace epix::render::render_resource

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
