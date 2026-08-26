#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::render_resource {

/**
 * @brief C++ stand-in for encase's `ShaderType` bound: POD types whose
 * `sizeof` matches the WGSL layout. Users must keep std140/std430 alignment
 * themselves (same requirement as writing raw WGSL uniform structs).
 */
template <typename T>
concept ShaderType = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

/** @brief Round `value` up to a multiple of `alignment`. */
constexpr std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) / alignment * alignment;
}

/**
 * @brief Offset of the `index`-th element of `element_size` bytes inside a
 * dynamic uniform/storage buffer with the given alignment.
 */
constexpr std::size_t element_offset(std::size_t index, std::size_t element_size, std::size_t alignment) noexcept {
    return index * align_up(element_size, alignment);
}

/**
 * @brief Stores a single value to be transferred to the GPU and made
 * accessible to shaders as a uniform buffer (Bevy `UniformBuffer<T>`).
 */
template <ShaderType T>
struct UniformBuffer {
    /** @brief CPU-side value. */
    T value{};
    /** @brief GPU buffer; created lazily by `write_buffer`. */
    wgpu::Buffer buffer;
    /** @brief Allocated GPU size in bytes. */
    std::size_t capacity = 0;
    /** @brief Debug label used when creating the GPU buffer. */
    std::string label = "UniformBuffer";
    /** @brief Buffer usages; uniform by default, override for storage-like use. */
    wgpu::BufferUsage usage = wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst;
    /** @brief True when the CPU value or usages changed since the last upload
     * (Bevy UniformBuffer::changed). */
    bool changed = false;

    UniformBuffer() = default;
    explicit UniformBuffer(T v) : value(std::move(v)) {}

    /** @brief Mutable access to the CPU-side value. */
    T& get_mut() noexcept { return value; }
    /** @brief Read access to the CPU-side value. */
    const T& get() const noexcept { return value; }
    /** @brief Replace the CPU-side value (Bevy set; does not set the changed
     * flag - write_buffer always uploads the current value). */
    void set(T v) { value = std::move(v); }
    /** @brief The GPU buffer, if created (Bevy UniformBuffer::buffer). */
    const wgpu::Buffer& gpu_buffer() const noexcept { return buffer; }
    /** @brief Whether a GPU buffer has been created (Bevy buffer().is_some()). */
    bool has_buffer() const noexcept { return static_cast<bool>(buffer); }
    /** @brief Add more buffer usages (Bevy add_usages; only adds, never
     * removes, and marks the buffer changed). */
    void add_usages(wgpu::BufferUsage extra) {
        usage   = usage | extra;
        changed = true;
    }

    /** @brief Create (if needed) and upload the value into a GPU buffer. When
     * the buffer does not exist yet (or the changed flag is set), the buffer
     * is (re)created with the current value; otherwise the current value is
     * uploaded into the existing buffer (Bevy write_buffer,
     * uniform_buffer.rs:129-142). */
    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        const std::size_t size = sizeof(T);
        if (!buffer || capacity < size || changed) {
            capacity = size;
            buffer =
                device.createBuffer(wgpu::BufferDescriptor().setLabel(label.c_str()).setUsage(usage).setSize(capacity));
            changed = false;
        }
        if (buffer) {
            queue.writeBuffer(buffer, 0, &value, sizeof(T));
        }
    }
};

/**
 * @brief Stores a dynamic array of values accessible as a uniform buffer with
 * per-element dynamic offsets (Bevy `DynamicUniformBuffer<T>`). The CPU side
 * is a byte vector laid out with std140 element alignment.
 */
template <ShaderType T>
struct DynamicUniformBuffer {
    /** @brief Raw byte storage, one aligned element per `T`. */
    std::vector<std::uint8_t> values;
    /** @brief GPU buffer; created lazily by `write_buffer`. */
    wgpu::Buffer buffer;
    /** @brief Allocated GPU size in bytes. */
    std::size_t capacity = 0;
    /** @brief Per-element alignment; 0 means query the device limits. */
    std::size_t dynamic_offset_alignment = 0;
    /** @brief Debug label used when creating the GPU buffer. */
    std::string label = "DynamicUniformBuffer";
    /** @brief GPU buffer usages; UNIFORM|COPY_DST by default, callers may add
     * STORAGE (Bevy ViewUniforms adds STORAGE when available). */
    wgpu::BufferUsage usage = wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst;

    DynamicUniformBuffer() = default;

    /** @brief Number of stored elements. */
    std::size_t len() const noexcept { return values.size() / element_stride(); }
    bool is_empty() const noexcept { return values.empty(); }
    void clear() noexcept { values.clear(); }

    /** @brief Per-element byte stride in the CPU buffer. */
    std::size_t element_stride() const noexcept {
        const std::size_t alignment = dynamic_offset_alignment ? dynamic_offset_alignment : 256;
        return align_up(sizeof(T), alignment);
    }

    /** @brief Append one element; returns its byte offset (Bevy
     * DynamicUniformBuffer::push returns a u32 byte offset, uniform_buffer.rs:201-205). */
    std::size_t push(const T& item) {
        const std::size_t index = len();
        values.resize((index + 1) * element_stride());
        std::memcpy(values.data() + index * element_stride(), &item, sizeof(T));
        return index * element_stride();
    }

    /** @brief Mutable access to the raw CPU byte storage. */
    std::vector<std::uint8_t>& get_mut() noexcept { return values; }

    /** @brief Resolve the alignment from the device limits (call once per frame). */
    void update_alignment(const wgpu::Limits& limits) {
        dynamic_offset_alignment = static_cast<std::size_t>(limits.minUniformBufferOffsetAlignment);
        if (dynamic_offset_alignment == 0) dynamic_offset_alignment = 256;
    }

    /** @brief Create (if needed) and upload the byte storage into a GPU buffer. */
    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        if (values.empty()) return;
        const std::size_t size = values.size();
        if (!buffer || capacity < size) {
            capacity = size;
            buffer =
                device.createBuffer(wgpu::BufferDescriptor().setLabel(label.c_str()).setUsage(usage).setSize(capacity));
        }
        if (buffer) {
            queue.writeBuffer(buffer, 0, values.data(), size);
        }
    }
};

/**
 * @brief Stores a single value accessible to shaders as a storage buffer
 * (Bevy `StorageBuffer<T>`).
 */
template <ShaderType T>
struct StorageBuffer {
    T value{};
    wgpu::Buffer buffer;
    std::size_t capacity = 0;
    std::string label    = "StorageBuffer";

    StorageBuffer() = default;
    explicit StorageBuffer(T v) : value(std::move(v)) {}

    T& get_mut() noexcept { return value; }
    const T& get() const noexcept { return value; }
    void set(T v) { value = std::move(v); }

    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        const std::size_t size = sizeof(T);
        if (!buffer || capacity < size) {
            capacity = size;
            buffer   = device.createBuffer(wgpu::BufferDescriptor()
                                               .setLabel(label.c_str())
                                               .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst)
                                               .setSize(capacity));
        }
        if (buffer) {
            queue.writeBuffer(buffer, 0, &value, sizeof(T));
        }
    }
};

/**
 * @brief Stores a dynamic array of values accessible as a storage buffer
 * (Bevy `DynamicStorageBuffer<T>`).
 */
template <ShaderType T>
struct DynamicStorageBuffer {
    /** @brief Raw byte storage, one aligned element per T (Bevy
     * DynamicStorageBuffer uses an encase std430 scratch). */
    std::vector<std::uint8_t> bytes;
    wgpu::Buffer buffer;
    std::size_t capacity = 0;
    /** @brief Per-element alignment; 0 means query the device limits. */
    std::size_t dynamic_offset_alignment = 0;
    std::string label                    = "DynamicStorageBuffer";

    DynamicStorageBuffer() = default;

    /** @brief Number of stored elements. */
    std::size_t len() const noexcept { return bytes.size() / element_stride(); }
    bool is_empty() const noexcept { return bytes.empty(); }
    void clear() noexcept { bytes.clear(); }

    /** @brief Per-element byte stride (Bevy encase std430 alignment for the
     * element type; storage-buffer offset alignment fallback 256). */
    std::size_t element_stride() const noexcept {
        const std::size_t alignment = dynamic_offset_alignment ? dynamic_offset_alignment : 256;
        return align_up(sizeof(T), alignment);
    }

    /** @brief Append one element; returns its byte offset (Bevy
     * DynamicStorageBuffer::push returns a u32 byte offset,
     * storage_buffer.rs:226-228). */
    std::size_t push(const T& item) {
        const std::size_t index = len();
        bytes.resize((index + 1) * element_stride());
        std::memcpy(bytes.data() + index * element_stride(), &item, sizeof(T));
        return index * element_stride();
    }

    /** @brief Resolve the alignment from the device limits (Bevy
     * min_storage_buffer_offset_alignment). */
    void update_alignment(const wgpu::Limits& limits) {
        dynamic_offset_alignment = static_cast<std::size_t>(limits.minStorageBufferOffsetAlignment);
        if (dynamic_offset_alignment == 0) dynamic_offset_alignment = 256;
    }

    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        if (bytes.empty()) return;
        const std::size_t size = bytes.size();
        if (!buffer || capacity < size) {
            capacity = size;
            buffer   = device.createBuffer(wgpu::BufferDescriptor()
                                               .setLabel(label.c_str())
                                               .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst)
                                               .setSize(capacity));
        }
        if (buffer) {
            queue.writeBuffer(buffer, 0, bytes.data(), size);
        }
    }
};

/**
 * @brief Error returned when BufferVec::write_buffer_range fails (Bevy
 * WriteBufferRangeError, buffer_vec.rs:576-587).
 */
EPIX_EXPORT enum class WriteBufferRangeError {
    RangeBiggerThanBuffer,
    BufferNotInitialized,
    NoValuesToUpload,
};

/**
 * @brief CPU vector of `T` with a lazily-created GPU buffer (Bevy `BufferVec<T>`).
 */
template <typename T>
struct BufferVec {
    /** @brief CPU-side values. */
    std::vector<T> values;
    /** @brief GPU buffer; created lazily by `write_buffer`. */
    wgpu::Buffer buffer;
    /** @brief Allocated GPU size in bytes. */
    std::size_t capacity = 0;
    /** @brief Buffer usages; must be supplied at construction. */
    wgpu::BufferUsage buffer_usage = wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst;
    /** @brief Debug label used when creating the GPU buffer. */
    std::string label = "BufferVec";

    BufferVec(wgpu::BufferUsage usage = wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst)
        : buffer_usage(usage) {}

    /** @brief Number of stored elements. */
    std::size_t len() const noexcept { return values.size(); }
    bool is_empty() const noexcept { return values.empty(); }
    void clear() noexcept { values.clear(); }
    /** @brief Append one element; returns its index. */
    std::size_t push(const T& item) {
        values.push_back(item);
        return values.size() - 1;
    }
    void reserve(std::size_t extra) { values.reserve(values.size() + extra); }
    std::vector<T>& get_mut() noexcept { return values; }

    /** @brief Create (if needed) and upload the values into a GPU buffer. */
    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        if (values.empty()) return;
        const std::size_t size = values.size() * sizeof(T);
        if (!buffer || capacity < size) {
            capacity = size;
            buffer   = device.createBuffer(
                wgpu::BufferDescriptor().setLabel(label.c_str()).setUsage(buffer_usage).setSize(capacity));
        }
        if (buffer) {
            queue.writeBuffer(buffer, 0, values.data(), size);
        }
    }

    /** @brief Upload only a range of elements (Bevy
     * BufferVec::write_buffer_range, buffer_vec.rs:196-215). */
    std::expected<void, WriteBufferRangeError> write_buffer_range(const wgpu::Queue& queue,
                                                                  std::pair<std::size_t, std::size_t> range) {
        if (values.empty()) return std::unexpected(WriteBufferRangeError::NoValuesToUpload);
        if (range.second > values.size()) return std::unexpected(WriteBufferRangeError::RangeBiggerThanBuffer);
        if (!buffer) return std::unexpected(WriteBufferRangeError::BufferNotInitialized);
        const std::size_t item_size = sizeof(T);
        const std::uint8_t* bytes   = reinterpret_cast<const std::uint8_t*>(values.data());
        queue.writeBuffer(buffer, static_cast<std::uint64_t>(range.first * item_size), bytes + range.first * item_size,
                          (range.second - range.first) * item_size);
        return {};
    }
};

/** @brief Alias kept for API parity: raw byte-vec buffer (Bevy `RawBufferVec<T>`). */
template <typename T>
using RawBufferVec = BufferVec<T>;

/**
 * @brief GPU-only allocation vector (Bevy `UninitBufferVec<T>`).
 *
 * Unlike `BufferVec`, this deliberately owns no CPU `T` values. GPU
 * preprocessing reserves output slots and the compute pass writes them, so
 * requiring `T` to be default constructible (or uploading zero-initialized
 * placeholder values) is both unnecessary and contrary to Bevy's contract.
 */
template <typename T>
struct UninitBufferVec {
    /** @brief GPU buffer, materialized lazily by `write_buffer`. */
    wgpu::Buffer buffer;
    /** @brief Number of reserved elements for this frame. */
    std::size_t length = 0;
    /** @brief Allocated element capacity. */
    std::size_t capacity = 0;
    /** @brief Required usages excluding the internally added copy-destination bit. */
    wgpu::BufferUsage buffer_usage = wgpu::BufferUsage::eStorage;
    /** @brief Debug label used for materialized buffers. */
    std::string label = "UninitBufferVec";

    explicit UninitBufferVec(wgpu::BufferUsage usage = wgpu::BufferUsage::eStorage) : buffer_usage(usage) {}

    /** @brief Reserve one output element and return its index. */
    std::size_t add() noexcept { return add_multiple(1); }
    /** @brief Reserve `count` output elements and return the first index. */
    std::size_t add_multiple(std::size_t count) noexcept {
        const auto index = length;
        length += count;
        return index;
    }
    std::size_t len() const noexcept { return length; }
    bool is_empty() const noexcept { return length == 0; }
    void clear() noexcept { length = 0; }

    /** @brief Ensure GPU storage for at least `requested_capacity` elements. */
    void reserve(std::size_t requested_capacity, const wgpu::Device& device) {
        if (requested_capacity <= capacity && buffer) return;
        capacity = requested_capacity;
        buffer   = device.createBuffer(wgpu::BufferDescriptor()
                                           .setLabel(label.c_str())
                                           .setUsage(buffer_usage | wgpu::BufferUsage::eCopyDst)
                                           .setSize(capacity * sizeof(T)));
    }

    /** @brief Materialize storage for all slots reserved this frame. */
    void write_buffer(const wgpu::Device& device) {
        if (!is_empty()) reserve(length, device);
    }
};

/** @brief Concept for types that can live in a `GpuArrayBuffer` (Bevy `GpuArrayBufferable`). */
template <typename T>
concept GpuArrayBufferable = ShaderType<T> && std::is_copy_constructible_v<T>;

template <GpuArrayBufferable T>
struct GpuArrayBufferIndex {
    std::uint32_t index = 0;
    std::optional<std::uint32_t> dynamic_offset;
};

/**
 * @brief Dynamic-offset uniform array used as the fallback for `GpuArrayBuffer`
 * on platforms without storage buffers (Bevy `BatchedUniformBuffer<T>`).
 */
template <GpuArrayBufferable T>
struct BatchedUniformBuffer {
    /** @brief Cap on uniform buffer binding size to avoid oversized arrays on
     * macOS (Bevy MAX_REASONABLE_UNIFORM_BUFFER_BINDING_SIZE). */
    static constexpr std::size_t MAX_REASONABLE_UNIFORM_BUFFER_BINDING_SIZE = 1 << 20;

    /** @brief Elements of the current (incomplete) batch. */
    std::vector<T> values;
    /** @brief Byte storage of all flushed batches, offset-aligned (Bevy
     * DynamicUniformBuffer<MaxCapacityArray<Vec<T>>>). */
    std::vector<std::uint8_t> buffer_bytes;
    /** @brief The uploaded GPU buffer. */
    wgpu::Buffer buffer;
    /** @brief Number of elements per batch (Bevy batch_size). */
    std::size_t capacity = 0;
    /** @brief Dynamic offset alignment. */
    std::size_t alignment = 0;
    /** @brief Byte offset of the next batch in the buffer. */
    std::size_t current_offset = 0;
    /** @brief Number of logical elements pushed this frame. */
    std::size_t element_count = 0;
    std::string label         = "BatchedUniformBuffer";

    BatchedUniformBuffer() = default;
    explicit BatchedUniformBuffer(const wgpu::Limits& limits) {
        alignment = static_cast<std::size_t>(limits.minUniformBufferOffsetAlignment);
        if (alignment == 0) alignment = 256;
        capacity = batch_size(limits);
    }

    /** @brief Number of elements per batch (Bevy
     * BatchedUniformBuffer::batch_size: min(max_uniform_buffer_binding_size,
     * MAX_REASONABLE) / T::min_size). */
    static std::size_t batch_size(const wgpu::Limits& limits) {
        const std::size_t binding = std::min(static_cast<std::size_t>(limits.maxUniformBufferBindingSize),
                                             MAX_REASONABLE_UNIFORM_BUFFER_BINDING_SIZE);
        return std::max<std::size_t>(1, binding / sizeof(T));
    }

    void clear() noexcept {
        values.clear();
        buffer_bytes.clear();
        current_offset = 0;
        element_count  = 0;
    }
    /** @brief Total number of pushed elements (flushed batches + current). */
    std::size_t len() const noexcept { return element_count; }

    /** @brief Push one element into the current batch; when the batch fills,
     * flush it to the buffer (Bevy BatchedUniformBuffer::push). The returned
     * index is the in-batch element index; the dynamic offset is the byte
     * offset of the batch start. */
    GpuArrayBufferIndex<T> push(const T& value) {
        // Bevy captures the batch-start offset BEFORE the push (and possible
        // flush) so the first element of a batch points at that batch
        // (batched_uniform_buffer.rs:82-93).
        const std::uint32_t index  = static_cast<std::uint32_t>(values.size());
        const std::uint32_t offset = static_cast<std::uint32_t>(current_offset);
        values.push_back(value);
        ++element_count;
        if (values.size() == capacity) {
            flush();
        }
        return GpuArrayBufferIndex<T>{index, offset};
    }

    /** @brief Write the current batch into the byte buffer at the aligned
     * offset (Bevy BatchedUniformBuffer::flush). */
    void flush() {
        if (values.empty()) return;
        // The shader sees a fixed-size `array<T, capacity>` at every dynamic
        // offset (Bevy's MaxCapacityArray). Upload a full, zero-padded batch
        // even when the final batch is only partially populated; otherwise a
        // dynamic bind group with the required batch size would run past the
        // end of the native wgpu buffer.
        const std::size_t batch_bytes     = capacity * sizeof(T);
        const std::size_t populated_bytes = values.size() * sizeof(T);
        buffer_bytes.insert(buffer_bytes.end(), reinterpret_cast<const std::uint8_t*>(values.data()),
                            reinterpret_cast<const std::uint8_t*>(values.data()) + populated_bytes);
        // The insertion above copied only the populated prefix; make the
        // remaining fixed-capacity elements zero-initialized.
        if (values.size() < capacity) {
            buffer_bytes.resize(buffer_bytes.size() + (capacity - values.size()) * sizeof(T), 0);
        }
        values.clear();
        current_offset += align_up(batch_bytes, alignment);
        buffer_bytes.resize(current_offset, 0);
    }

    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        flush();
        if (buffer_bytes.empty()) return;
        if (!buffer || buffer.getSize() < buffer_bytes.size()) {
            buffer = device.createBuffer(wgpu::BufferDescriptor()
                                             .setLabel(label.c_str())
                                             .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst)
                                             .setSize(buffer_bytes.size()));
        }
        if (buffer) {
            queue.writeBuffer(buffer, 0, buffer_bytes.data(), buffer_bytes.size());
        }
    }
};

/**
 * @brief Array of GPU-accessible elements, storage buffer where supported,
 * falling back to a dynamic-offset uniform buffer (Bevy `GpuArrayBuffer<T>`).
 */
template <GpuArrayBufferable T>
struct GpuArrayBuffer {
    /** @brief Backing storage: uniform fallback or storage buffer. */
    std::variant<BatchedUniformBuffer<T>, BufferVec<T>> storage;

    explicit GpuArrayBuffer(const wgpu::Limits& limits) {
        if (limits.maxStorageBuffersPerShaderStage == 0) {
            storage.emplace<0>(limits);
        } else {
            storage.emplace<1>(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst);
        }
    }

    void clear() {
        std::visit([](auto& s) { s.clear(); }, storage);
    }

    GpuArrayBufferIndex<T> push(const T& value) {
        return std::visit(
            [&](auto& s) -> GpuArrayBufferIndex<T> {
                if constexpr (std::is_same_v<std::decay_t<decltype(s)>, BufferVec<T>>) {
                    // Storage-buffer path: BufferVec::push returns a plain
                    // element index; no dynamic offset.
                    return GpuArrayBufferIndex<T>{static_cast<std::uint32_t>(s.push(value)), std::nullopt};
                } else {
                    return s.push(value);
                }
            },
            storage);
    }

    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        std::visit([&](auto& s) { s.write_buffer(device, queue); }, storage);
    }

    /** @brief Layout of the buffer binding required by the selected backing
     * store (Bevy `GpuArrayBuffer::binding_layout`).  Epix exposes the native
     * wgpu layout object directly; the caller supplies the binding number and
     * shader visibility when adding it to a bind-group layout. */
    static wgpu::BufferBindingLayout binding_layout(const wgpu::Limits& limits) {
        if (limits.maxStorageBuffersPerShaderStage == 0) {
            return wgpu::BufferBindingLayout()
                .setType(wgpu::BufferBindingType::eUniform)
                .setHasDynamicOffset(wgpu::Bool(true))
                // The fallback is a runtime-sized array.  As in Bevy, leave
                // validation of its concrete size to wgpu at bind time.
                .setMinBindingSize(0);
        }
        return wgpu::BufferBindingLayout()
            .setType(wgpu::BufferBindingType::eReadOnlyStorage)
            .setHasDynamicOffset(wgpu::Bool(false))
            .setMinBindingSize(0);
    }

    /** @brief The GPU buffer after `write_buffer`, if one has been allocated
     * (Bevy `GpuArrayBuffer::binding`).  The native wgpu binding entry owns
     * the offset and size, so Epix returns the resource directly. */
    std::optional<wgpu::Buffer> binding() const {
        return std::visit(
            [](const auto& s) -> std::optional<wgpu::Buffer> {
                if (!s.buffer) return std::nullopt;
                return s.buffer;
            },
            storage);
    }

    /** @brief Number of elements addressable through one dynamic uniform
     * binding, or `nullopt` when the storage-buffer path is selected (Bevy
     * `GpuArrayBuffer::batch_size`). */
    static std::optional<std::uint32_t> batch_size(const wgpu::Limits& limits) {
        if (limits.maxStorageBuffersPerShaderStage == 0) {
            return static_cast<std::uint32_t>(BatchedUniformBuffer<T>::batch_size(limits));
        }
        return std::nullopt;
    }
};

/**
 * @brief Hashable key describing a cached texture (Bevy keys on the full
 * `TextureDescriptor`; here on its meaningful fields).
 */
struct TextureCacheKey {
    wgpu::TextureFormat format          = wgpu::TextureFormat::eUndefined;
    wgpu::TextureDimension dimension    = wgpu::TextureDimension::e2D;
    std::uint32_t width                 = 0;
    std::uint32_t height                = 0;
    std::uint32_t depth_or_array_layers = 1;
    std::uint32_t mip_level_count       = 1;
    std::uint32_t sample_count          = 1;
    wgpu::TextureUsage usage            = wgpu::TextureUsage::eNone;
    /** @brief Debug label (Bevy TextureCacheKey includes the descriptor,
     * hence the label and view formats). */
    std::string label;
    /** @brief View formats (Bevy descriptor.view_formats). */
    std::vector<wgpu::TextureFormat> view_formats;

    bool operator==(const TextureCacheKey&) const = default;
};

struct TextureCacheKeyHash {
    std::size_t operator()(const TextureCacheKey& k) const noexcept {
        std::size_t h = static_cast<std::size_t>(k.format);
        h ^= static_cast<std::size_t>(k.dimension) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.width) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.height) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.depth_or_array_layers) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.mip_level_count) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.sample_count) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.usage) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(k.label) + 0x9e3779b9 + (h << 6) + (h >> 2);
        for (auto format : k.view_formats) {
            h ^= static_cast<std::size_t>(format) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

/** @brief A cached GPU texture with its default view (Bevy `CachedTexture`). */
struct CachedTexture {
    wgpu::Texture texture;
    wgpu::TextureView default_view;
};

namespace detail {
struct CachedTextureMeta {
    wgpu::Texture texture;
    wgpu::TextureView default_view;
    bool taken                        = false;
    std::size_t frames_since_last_use = 0;
};
}  // namespace detail

/**
 * @brief Caches textures that are created repeatedly (each frame) to reduce
 * GPU memory allocations (Bevy `TextureCache`).
 */
EPIX_EXPORT struct TextureCache {
    std::unordered_map<TextureCacheKey, std::vector<detail::CachedTextureMeta>, TextureCacheKeyHash> textures;

    /** @brief Retrieve a texture matching the key, creating it from the
     * descriptor if needed. The key must describe the same texture as the
     * descriptor (the wgpu wrapper exposes setters only, so the caller builds
     * the key from the same fields). */
    CachedTexture get(const wgpu::Device& device,
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

    /** @brief Returns true if the cache contains no textures. */
    bool is_empty() const noexcept { return textures.empty(); }

    /** @brief Updates the cache and only retains recently used textures
     * (Bevy TextureCache::update): every texture ages one frame and is
     * released; textures unused for 3 or more frames are evicted. */
    void update() {
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
};

/** @brief System that ages and evicts unused cached textures each frame (Bevy
 * `update_texture_cache_system`, run in `RenderSystems::Cleanup`). */
EPIX_EXPORT inline void update_texture_cache_system(ecs::ResMut<TextureCache> texture_cache) {
    texture_cache->update();
}

}  // namespace epix::render::render_resource
