#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#endif

#include <epix/image.hpp>
#include <epix/render/extract.hpp>
#include <epix/render/storage.hpp>

namespace epix::render {
/**
 * @brief Component registering a wrapped handle for GPU readback, either a
 * texture or a buffer (Bevy 'Readback').
 */
EPIX_EXPORT struct Readback {
    /** @brief Texture handle to read back. */
    struct Texture {
        /** @brief Handle of the image to read back. */
        assets::Handle<epix::image::Image> image;
    };
    /** @brief Buffer handle (and optional byte range) to read back. */
    struct Buffer {
        /** @brief Handle of the shader storage buffer. */
        assets::Handle<ShaderStorageBuffer> buffer;
        /** @brief Optional (start offset, size) byte range; None reads the whole buffer. */
        std::optional<std::pair<std::uint64_t, std::uint64_t>> start_offset_and_size;
    };

    /** @brief The active readback request. */
    std::variant<Texture, Buffer> value;

    /** @brief Create a readback for a texture handle. */
    static Readback texture(assets::Handle<epix::image::Image> image) {
        return Readback{Texture{std::move(image)}};
    }
    /** @brief Create a readback for a full buffer. */
    static Readback buffer(assets::Handle<ShaderStorageBuffer> buffer) {
        return Readback{Buffer{std::move(buffer), std::nullopt}};
    }
    /** @brief Create a readback for a byte range of a buffer. */
    static Readback buffer_range(assets::Handle<ShaderStorageBuffer> buffer, std::uint64_t start, std::uint64_t size) {
        return Readback{Buffer{std::move(buffer), std::pair<std::uint64_t, std::uint64_t>{start, size}}};
    }
};

/**
 * @brief Event triggered when a GPU readback completes; holds the raw bytes
 * (Bevy 'ReadbackComplete').
 */
EPIX_EXPORT struct ReadbackComplete {
    /** @brief Entity the readback was requested on. */
    epix::ecs::Entity entity;
    /** @brief Raw bytes of the requested buffer or texture. */
    std::vector<std::uint8_t> data;

    /** @brief Interpret the raw bytes as a shader type T (Bevy
     * ReadbackComplete::to_shader_type, gpu_readback.rs:122-129). T must be
     * trivially copyable and fit the data; no bounds check (Bevy panics via
     * Reader on overflow - here the caller guarantees the size). */
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    T to_shader_type() const {
        T val{};
        if (data.size() >= sizeof(T)) {
            std::memcpy(&val, data.data(), sizeof(T));
        }
        return val;
    }
};

/** @brief Internal machinery of the GPU readback pipeline (Bevy
 * `GpuReadbackBufferPool` / `GpuReadbacks` / readback sources). */
namespace readback {

/**
 * @brief Thread-safe bounded(1) channel carrying completed readback results
 * from the wgpu map callback thread to the main world (Bevy async_channel
 * bounded(1) + try_send). `data == std::nullopt` marks a failed map, so the
 * receiver can return the staging buffer to the pool.
 */
EPIX_EXPORT class ReadbackChannel {
   public:
    /** @brief Try to enqueue a result; returns false when the slot is occupied
     * (Bevy `try_send` — the new result is dropped, matching bounded(1)). */
    bool try_send(epix::ecs::Entity entity, wgpu::Buffer buffer, std::optional<std::vector<std::uint8_t>> data) {
        std::lock_guard lock(m_mutex);
        if (!m_queue.empty()) {
            return false;
        }
        m_queue.emplace_back(entity, std::move(buffer), std::move(data));
        return true;
    }
    std::optional<std::tuple<epix::ecs::Entity, wgpu::Buffer, std::optional<std::vector<std::uint8_t>>>> try_recv() {
        std::lock_guard lock(m_mutex);
        if (m_queue.empty()) {
            return std::nullopt;
        }
        auto front = std::move(m_queue.front());
        m_queue.pop_front();
        return front;
    }

   private:
    std::mutex m_mutex;
    std::deque<std::tuple<epix::ecs::Entity, wgpu::Buffer, std::optional<std::vector<std::uint8_t>>>> m_queue;
};

/** @brief A pooled staging buffer for readback (Bevy `GpuReadbackBuffer`). */
EPIX_EXPORT struct GpuReadbackBuffer {
    /** @brief The staging buffer (COPY_DST | MAP_READ). */
    wgpu::Buffer buffer;
    /** @brief True while a readback is in flight. */
    bool taken = false;
    /** @brief Frames this buffer has been unused (evicted at the plugin limit). */
    std::size_t frames_unused = 0;
};

/**
 * @brief Pool of readback staging buffers keyed by size, reused across frames
 * (Bevy `GpuReadbackBufferPool`).
 */
EPIX_EXPORT struct GpuReadbackBufferPool {
    /** @brief Map of buffer size to the list of pooled buffers. */
    std::unordered_map<std::uint64_t, std::vector<GpuReadbackBuffer>> buffers;

    /** @brief Get an untaken buffer of at least `size` bytes, allocating one if needed. */
    wgpu::Buffer get(const wgpu::Device& device, std::uint64_t size);
    /** @brief Return a buffer to the pool for reuse in a future frame. */
    void return_buffer(const wgpu::Buffer& buffer);
    /** @brief Age unused buffers and evict those idle for `max_unused_frames` frames. */
    void update(std::size_t max_unused_frames);
};

/** @brief The GPU source of a readback (Bevy `ReadbackSource`). */
EPIX_EXPORT struct ReadbackSource {
    /** @brief Read back a texture region. */
    struct Texture {
        /** @brief Source texture. */
        wgpu::Texture texture;
        /** @brief Aligned buffer layout the texture is copied into. */
        wgpu::TexelCopyBufferLayout layout;
        /** @brief Copy size. */
        wgpu::Extent3D size;
    };
    /** @brief Read back a (range of a) buffer. */
    struct Buffer {
        /** @brief Source buffer. */
        wgpu::Buffer buffer;
        /** @brief Optional (start offset, size); None reads the whole buffer. */
        std::optional<std::pair<std::uint64_t, std::uint64_t>> start_offset_and_size;
    };
    /** @brief The active source. */
    std::variant<Texture, Buffer> value;
};

/** @brief One in-flight readback request (Bevy `GpuReadback`). */
EPIX_EXPORT struct GpuReadback {
    /** @brief Entity the readback was requested on. */
    epix::ecs::Entity entity;
    /** @brief GPU source. */
    ReadbackSource src;
    /** @brief Staging buffer receiving the data. */
    wgpu::Buffer buffer;
    /** @brief Channel the map callback delivers results through. */
    std::shared_ptr<ReadbackChannel> channel;
};

/** @brief Render-world resource holding requested/mapped readbacks (Bevy
 * `GpuReadbacks`). */
EPIX_EXPORT struct GpuReadbacks {
    /** @brief Readbacks with copy commands issued, awaiting map. */
    std::vector<GpuReadback> requested;
    /** @brief Readbacks currently mapped, awaiting delivery. */
    std::vector<GpuReadback> mapped;
};

/** @brief Render-world resource carrying the pool eviction limit (Bevy
 * `GpuReadbackMaxUnusedFrames`). */
EPIX_EXPORT struct GpuReadbackMaxUnusedFrames {
    /** @brief Frames a buffer may be unused before removal from the pool. */
    std::size_t value = 10;
};

/** @brief wgpu minimum alignment for copy bytes-per-row. */
constexpr std::uint32_t COPY_BYTES_PER_ROW_ALIGNMENT = 256;

/** @brief Round `value` up to a multiple of COPY_BYTES_PER_ROW_ALIGNMENT. */
EPIX_EXPORT std::uint32_t align_byte_size(std::uint32_t value);
/** @brief Size of a texture copy when rows are aligned to COPY_BYTES_PER_ROW_ALIGNMENT. */
EPIX_EXPORT std::uint32_t get_aligned_size(const wgpu::Extent3D& extent, std::uint32_t pixel_size);
/** @brief Bytes per texel for the given texture format; 0 if unknown. */
EPIX_EXPORT std::uint32_t texture_format_pixel_size(wgpu::TextureFormat format);
/** @brief Aligned TexelCopyBufferLayout for copying an image into a buffer. */
EPIX_EXPORT wgpu::TexelCopyBufferLayout layout_data(const wgpu::Extent3D& extent, wgpu::TextureFormat format);

}  // namespace readback

/** @brief ExtractSchedule system: deliver completed readbacks to the main
 * world as events and maintain the staging-buffer pool (Bevy `sync_readbacks`).
 */
EPIX_EXPORT void sync_readbacks(app::Extract<ecs::ResMut<ecs::Events<ReadbackComplete>>> events,
                                ecs::ResMut<readback::GpuReadbackBufferPool> buffer_pool,
                                ecs::ResMut<readback::GpuReadbacks> readbacks,
                                ecs::Res<readback::GpuReadbackMaxUnusedFrames> max_unused_frames);

/** @brief PrepareResources system: create readback requests from Readback
 * components (Bevy `prepare_buffers`). */
EPIX_EXPORT void prepare_buffers(ecs::Res<wgpu::Device> device,
                                 ecs::ResMut<readback::GpuReadbacks> readbacks,
                                 ecs::ResMut<readback::GpuReadbackBufferPool> buffer_pool,
                                 ecs::Res<RenderAssets<epix::image::Image>> gpu_images,
                                 ecs::Res<RenderAssets<ShaderStorageBuffer>> ssbos,
                                 ecs::Query<ecs::Item<ecs::Entity, const sync_world::MainEntity&, const Readback&>> handles);

/** @brief Render system (after the graph): map the staging buffers of
 * requested readbacks asynchronously (Bevy `map_buffers`). */
EPIX_EXPORT void map_buffers(ecs::ResMut<readback::GpuReadbacks> readbacks);

/**
 * @brief Issue the copy commands for all requested readbacks into the current
 * command encoder. Called from the render graph finalizer (Bevy
 * `submit_readback_commands`).
 */
EPIX_EXPORT void submit_readback_commands(epix::ecs::World& world, const wgpu::CommandEncoder& encoder);

/**
 * @brief Plugin enabling readback of GPU buffers/textures to the CPU (Bevy
 * 'GpuReadbackPlugin'). Registers the 'Readback' component extraction, the
 * staging-buffer pool, and the extract/prepare/map pipeline.
 */
EPIX_EXPORT struct GpuReadbackPlugin {
    /** @brief Frames a buffer may be unused before removal from the pool. */
    std::size_t max_unused_frames = 10;
    void attach(app::App& app);
};

}  // namespace epix::render

// Self-extraction specialization (Bevy `#[derive(ExtractComponent)]` default).
template <>
struct epix::render::ExtractComponent<epix::render::Readback> {
    using QueryData   = const epix::render::Readback&;
    using QueryFilter = ecs::Filter<>;  // Bevy type QueryFilter = ()
    using Out         = epix::render::Readback;
    static std::optional<Out> extract_component(const epix::render::Readback& item) { return item; }
};
