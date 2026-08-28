#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <glm/glm.hpp>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::render_resource {

/** @brief Explicit GPU layout contract for a shader value.
 *
 * This is the C++ counterpart to encase's ShaderType / WriteInto contract.
 * It deliberately has no implicit POD fallback: every type admitted to a
 * typed GPU buffer must opt in with its encoded size and writer. */
template <typename T>
struct ShaderTypeInfo;

/** @brief Reusable opt-in encoder for types whose object representation has
 * already been intentionally laid out to match the shader representation. */
template <typename T>
    requires(std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>)
struct RawShaderType {
    static constexpr std::size_t shader_size = sizeof(T);

    static void write_into(const T& value, std::span<std::uint8_t> destination) {
        if (destination.size() < shader_size) throw std::out_of_range("Shader destination is too small.");
        std::memcpy(destination.data(), std::addressof(value), shader_size);
    }

    static std::expected<T, std::string> read_from(std::span<const std::uint8_t> source) {
        if (source.size() < shader_size) return std::unexpected("Shader source is too small.");
        T value{};
        std::memcpy(std::addressof(value), source.data(), shader_size);
        return value;
    }
};

template <typename T>
concept ShaderType = requires {
    { ShaderTypeInfo<T>::shader_size } -> std::convertible_to<std::size_t>;
} && (ShaderTypeInfo<T>::shader_size > 0);

template <typename T>
concept ShaderWritable = ShaderType<T> && requires(const T& value, std::span<std::uint8_t> destination) {
    ShaderTypeInfo<T>::write_into(value, destination);
};

template <typename T>
concept ShaderReadable = ShaderType<T> && requires(std::span<const std::uint8_t> source) {
    { ShaderTypeInfo<T>::read_from(source) } -> std::same_as<std::expected<T, std::string>>;
};

template <ShaderWritable T>
inline std::vector<std::uint8_t> encode_shader_values(std::span<const T> values) {
    std::vector<std::uint8_t> encoded(values.size() * ShaderTypeInfo<T>::shader_size);
    for (std::size_t index = 0; index < values.size(); ++index) {
        ShaderTypeInfo<T>::write_into(values[index],
                                      std::span<std::uint8_t>{encoded.data() + index * ShaderTypeInfo<T>::shader_size,
                                                              ShaderTypeInfo<T>::shader_size});
    }
    return encoded;
}

template <>
struct ShaderTypeInfo<float> : RawShaderType<float> {};
template <>
struct ShaderTypeInfo<std::int32_t> : RawShaderType<std::int32_t> {};
template <>
struct ShaderTypeInfo<std::uint32_t> : RawShaderType<std::uint32_t> {};

/** WGSL bool occupies a four-byte scalar, unlike C++ bool. */
template <>
struct ShaderTypeInfo<bool> {
    static constexpr std::size_t shader_size = sizeof(std::uint32_t);
    static void write_into(bool value, std::span<std::uint8_t> destination) {
        const std::uint32_t encoded = value ? 1u : 0u;
        RawShaderType<std::uint32_t>::write_into(encoded, destination);
    }
    static std::expected<bool, std::string> read_from(std::span<const std::uint8_t> source) {
        auto encoded = RawShaderType<std::uint32_t>::read_from(source);
        if (!encoded) return std::unexpected(encoded.error());
        return *encoded != 0;
    }
};

template <glm::length_t L, typename T, glm::qualifier Q>
    requires((L == 2 || L == 4) && std::same_as<T, float>)
struct ShaderTypeInfo<glm::vec<L, T, Q>> : RawShaderType<glm::vec<L, T, Q>> {};

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
template <ShaderWritable T>
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
        const std::size_t size = ShaderTypeInfo<T>::shader_size;
        if (!buffer || capacity < size || changed) {
            capacity = size;
            buffer =
                device.createBuffer(wgpu::BufferDescriptor().setLabel(label.c_str()).setUsage(usage).setSize(capacity));
            changed = false;
        }
        if (buffer) {
            std::vector<std::uint8_t> encoded(size);
            ShaderTypeInfo<T>::write_into(value, encoded);
            queue.writeBuffer(buffer, 0, encoded.data(), encoded.size());
        }
    }
};

/**
 * @brief Stores a dynamic array of values accessible as a uniform buffer with
 * per-element dynamic offsets (Bevy `DynamicUniformBuffer<T>`). The CPU side
 * is a byte vector laid out with std140 element alignment.
 */
template <ShaderWritable T>
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
        return align_up(ShaderTypeInfo<T>::shader_size, alignment);
    }

    /** @brief Append one element; returns its byte offset (Bevy
     * DynamicUniformBuffer::push returns a u32 byte offset, uniform_buffer.rs:201-205). */
    std::size_t push(const T& item) {
        const std::size_t index = len();
        values.resize((index + 1) * element_stride());
        ShaderTypeInfo<T>::write_into(item,
                                      std::span<std::uint8_t>{values.data() + index * element_stride(),
                                                              ShaderTypeInfo<T>::shader_size});
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
template <ShaderWritable T>
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
        const std::size_t size = ShaderTypeInfo<T>::shader_size;
        if (!buffer || capacity < size) {
            capacity = size;
            buffer   = device.createBuffer(wgpu::BufferDescriptor()
                                               .setLabel(label.c_str())
                                               .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst)
                                               .setSize(capacity));
        }
        if (buffer) {
            std::vector<std::uint8_t> encoded(size);
            ShaderTypeInfo<T>::write_into(value, encoded);
            queue.writeBuffer(buffer, 0, encoded.data(), encoded.size());
        }
    }
};

/**
 * @brief Stores a dynamic array of values accessible as a storage buffer
 * (Bevy `DynamicStorageBuffer<T>`).
 */
template <ShaderWritable T>
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
        return align_up(ShaderTypeInfo<T>::shader_size, alignment);
    }

    /** @brief Append one element; returns its byte offset (Bevy
     * DynamicStorageBuffer::push returns a u32 byte offset,
     * storage_buffer.rs:226-228). */
    std::size_t push(const T& item) {
        const std::size_t index = len();
        bytes.resize((index + 1) * element_stride());
        ShaderTypeInfo<T>::write_into(item,
                                      std::span<std::uint8_t>{bytes.data() + index * element_stride(),
                                                              ShaderTypeInfo<T>::shader_size});
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

/** @brief Temporary explicit no-uninitialized-bytes contract for `RawBufferVec`.
 *
 * This is the C++ counterpart to Bevy's `bytemuck::NoUninit`. C++ has no
 * standard trait that proves padding bytes are initialized, so aggregate
 * element types must opt in deliberately through `RawBufferElementInfo`.
 *
 * @todo Replace this marker once C++26 static reflection is available in our
 * supported toolchain. The reflected check must verify that a type has a
 * constructor, every reflected data member has an initializer, and every
 * member type is itself free of uninitialized bytes. That will let Epix infer
 * the equivalent of `bytemuck::NoUninit` instead of requiring this temporary
 * manual declaration. */
template <typename T>
struct RawBufferElementInfo;

template <typename T>
concept RawBufferElement = std::is_trivially_copyable_v<T> && requires {
    { RawBufferElementInfo<T>::has_no_uninit } -> std::convertible_to<bool>;
} && RawBufferElementInfo<T>::has_no_uninit;

template <std::integral T>
struct RawBufferElementInfo<T> {
    static constexpr bool has_no_uninit = true;
};
template <std::floating_point T>
struct RawBufferElementInfo<T> {
    static constexpr bool has_no_uninit = true;
};

/**
 * @brief GPU-layout encoded vector with a lazily-created GPU buffer (Bevy
 * `BufferVec<T>`).
 *
 * Unlike `RawBufferVec`, values are encoded at `push` time and cannot be
 * accessed as C++ objects afterward. This is required for layouts whose GPU
 * representation differs from their object representation (notably WGSL
 * booleans and padded structs).
 */
template <ShaderWritable T>
struct BufferVec {
private:
    std::vector<std::uint8_t> data;
    wgpu::Buffer gpu_buffer;
    std::size_t gpu_capacity = 0;
    wgpu::BufferUsage buffer_usage;
    std::optional<std::string> label;
    bool label_changed = false;

public:
    explicit BufferVec(wgpu::BufferUsage usage) : buffer_usage(usage) {}

    const wgpu::Buffer* buffer() const noexcept { return gpu_buffer ? std::addressof(gpu_buffer) : nullptr; }
    wgpu::Buffer* buffer() noexcept { return gpu_buffer ? std::addressof(gpu_buffer) : nullptr; }
    std::optional<std::reference_wrapper<const wgpu::Buffer>> binding() const noexcept {
        if (const auto* value = buffer()) return std::cref(*value);
        return std::nullopt;
    }
    std::size_t capacity() const noexcept { return gpu_capacity; }
    wgpu::BufferUsage usage() const noexcept { return buffer_usage; }

    /** @brief Number of stored elements. */
    std::size_t len() const noexcept { return data.size() / ShaderTypeInfo<T>::shader_size; }
    bool is_empty() const noexcept { return data.empty(); }
    void clear() noexcept { data.clear(); }
    /** @brief Append one element; returns its index. */
    std::size_t push(const T& item) {
        const auto index = len();
        const auto offset = data.size();
        data.resize(offset + ShaderTypeInfo<T>::shader_size, 0);
        ShaderTypeInfo<T>::write_into(item,
                                      std::span<std::uint8_t>{data.data() + offset, ShaderTypeInfo<T>::shader_size});
        return index;
    }

    void set_label(std::optional<std::string_view> new_label) {
        auto updated = new_label.transform([](std::string_view value) { return std::string(value); });
        if (updated != label) label_changed = true;
        label = std::move(updated);
    }
    std::optional<std::string_view> get_label() const noexcept {
        if (!label) return std::nullopt;
        return *label;
    }

    void reserve(std::size_t requested_capacity, const wgpu::Device& device) {
        if (requested_capacity <= gpu_capacity && !label_changed) return;
        gpu_capacity = requested_capacity;
        gpu_buffer = device.createBuffer(wgpu::BufferDescriptor()
                                             .setLabel(label ? label->c_str() : "")
                                             .setUsage(buffer_usage | wgpu::BufferUsage::eCopyDst)
                                             .setSize(gpu_capacity * ShaderTypeInfo<T>::shader_size));
        label_changed = false;
    }

    /** @brief Create (if needed) and upload the values into a GPU buffer. */
    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        if (data.empty()) return;
        reserve(len(), device);
        if (gpu_buffer) queue.writeBuffer(gpu_buffer, 0, data.data(), data.size());
    }

    /** @brief Upload only a range of elements (Bevy
     * BufferVec::write_buffer_range). */
    std::expected<void, WriteBufferRangeError> write_buffer_range(const wgpu::Queue& queue,
                                                                  std::pair<std::size_t, std::size_t> range) {
        if (data.empty()) return std::unexpected(WriteBufferRangeError::NoValuesToUpload);
        const std::size_t item_size = ShaderTypeInfo<T>::shader_size;
        if (range.first > range.second || range.second > item_size * gpu_capacity)
            return std::unexpected(WriteBufferRangeError::RangeBiggerThanBuffer);
        if (!gpu_buffer) return std::unexpected(WriteBufferRangeError::BufferNotInitialized);
        queue.writeBuffer(gpu_buffer, static_cast<std::uint64_t>(range.first * item_size), data.data() + range.first,
                          range.second - range.first);
        return {};
    }

    void truncate(std::size_t length) { data.resize(length * ShaderTypeInfo<T>::shader_size); }
};

/** @brief CPU-readable raw byte buffer (Bevy `RawBufferVec<T>`).
 *
 * Raw uploads use a representation explicitly marked as having no
 * uninitialized bytes. Use `BufferVec` for shader-encoded values instead. */
template <RawBufferElement T>
struct RawBufferVec {
private:
    std::vector<T> data;
    wgpu::Buffer gpu_buffer;
    std::size_t gpu_capacity = 0;
    wgpu::BufferUsage buffer_usage;
    std::optional<std::string> label;
    bool label_changed = false;

public:
    explicit RawBufferVec(wgpu::BufferUsage usage) : buffer_usage(usage) {}
    const wgpu::Buffer* buffer() const noexcept { return gpu_buffer ? std::addressof(gpu_buffer) : nullptr; }
    wgpu::Buffer* buffer() noexcept { return gpu_buffer ? std::addressof(gpu_buffer) : nullptr; }
    std::optional<std::reference_wrapper<const wgpu::Buffer>> binding() const noexcept {
        if (const auto* value = buffer()) return std::cref(*value);
        return std::nullopt;
    }
    std::size_t capacity() const noexcept { return gpu_capacity; }
    std::size_t len() const noexcept { return data.size(); }
    bool is_empty() const noexcept { return data.empty(); }
    void clear() noexcept { data.clear(); }
    std::size_t push(const T& item) { data.push_back(item); return data.size() - 1; }
    void append(RawBufferVec& other) { data.insert(data.end(), std::make_move_iterator(other.data.begin()), std::make_move_iterator(other.data.end())); other.data.clear(); }
    const T* get(std::uint32_t index) const noexcept { return index < data.size() ? std::addressof(data[index]) : nullptr; }
    void set(std::uint32_t index, const T& value) { data.at(index) = value; }
    void reserve_internal(std::size_t count) { data.reserve(data.size() + count); }
    void set_label(std::optional<std::string_view> new_label) {
        auto updated = new_label.transform([](std::string_view value) { return std::string(value); });
        if (updated != label) label_changed = true;
        label = std::move(updated);
    }
    std::optional<std::string_view> get_label() const noexcept {
        if (!label) return std::nullopt;
        return *label;
    }
    const std::vector<T>& values() const noexcept { return data; }
    std::vector<T>& values_mut() noexcept { return data; }

    void reserve(std::size_t requested_capacity, const wgpu::Device& device) {
        if (requested_capacity <= gpu_capacity && (!label_changed || requested_capacity == 0)) return;
        gpu_capacity = requested_capacity;
        gpu_buffer = device.createBuffer(wgpu::BufferDescriptor()
                                             .setLabel(label ? label->c_str() : "")
                                             .setUsage(buffer_usage | wgpu::BufferUsage::eCopyDst)
                                             .setSize(gpu_capacity * sizeof(T)));
        label_changed = false;
    }
    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        if (data.empty()) return;
        reserve(data.size(), device);
        if (gpu_buffer) queue.writeBuffer(gpu_buffer, 0, data.data(), data.size() * sizeof(T));
    }
    std::expected<void, WriteBufferRangeError> write_buffer_range(const wgpu::Queue& queue,
                                                                   std::pair<std::size_t, std::size_t> range) {
        if (data.empty()) return std::unexpected(WriteBufferRangeError::NoValuesToUpload);
        if (range.first > range.second || range.second > sizeof(T) * gpu_capacity)
            return std::unexpected(WriteBufferRangeError::RangeBiggerThanBuffer);
        if (!gpu_buffer) return std::unexpected(WriteBufferRangeError::BufferNotInitialized);
        queue.writeBuffer(gpu_buffer, static_cast<std::uint64_t>(range.first * sizeof(T)), data.data() + range.first,
                          (range.second - range.first) * sizeof(T));
        return {};
    }
    void truncate(std::size_t length) { data.resize(length); }
    std::optional<T> pop() {
        if (data.empty()) return std::nullopt;
        auto value = std::move(data.back());
        data.pop_back();
        return value;
    }
    void grow_set(std::uint32_t index, const T& value) requires std::default_initializable<T> {
        while (index >= data.size()) data.emplace_back();
        data[index] = value;
    }
};

/** @brief Concept for types that can live in a `GpuArrayBuffer` (Bevy `GpuArrayBufferable`). */
template <typename T>
concept GpuArrayBufferable = ShaderWritable<T> && std::is_copy_constructible_v<T>;

/**
 * @brief GPU-only allocation vector (Bevy `UninitBufferVec<T>`).
 *
 * Unlike `BufferVec`, this deliberately owns no CPU `T` values. GPU
 * preprocessing reserves output slots and the compute pass writes them, so
 * requiring `T` to be default constructible (or uploading zero-initialized
 * placeholder values) is both unnecessary and contrary to Bevy's contract.
 */
template <GpuArrayBufferable T>
struct UninitBufferVec {
private:
    wgpu::Buffer gpu_buffer;
    std::size_t length = 0;
    std::size_t gpu_capacity = 0;
    wgpu::BufferUsage buffer_usage;
    std::optional<std::string> label;
    bool label_changed = false;

public:
    explicit UninitBufferVec(wgpu::BufferUsage usage) : buffer_usage(usage) {}
    const wgpu::Buffer* buffer() const noexcept { return gpu_buffer ? std::addressof(gpu_buffer) : nullptr; }
    wgpu::Buffer* buffer() noexcept { return gpu_buffer ? std::addressof(gpu_buffer) : nullptr; }
    std::optional<std::reference_wrapper<const wgpu::Buffer>> binding() const noexcept {
        if (const auto* value = buffer()) return std::cref(*value);
        return std::nullopt;
    }
    std::size_t capacity() const noexcept { return gpu_capacity; }

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
        if (requested_capacity <= gpu_capacity && !label_changed) return;
        gpu_capacity = requested_capacity;
        gpu_buffer   = device.createBuffer(wgpu::BufferDescriptor()
                                           .setLabel(label ? label->c_str() : "")
                                           .setUsage(buffer_usage | wgpu::BufferUsage::eCopyDst)
                                           .setSize(gpu_capacity * sizeof(T)));
        label_changed = false;
    }

    /** @brief Materialize storage for all slots reserved this frame. */
    void write_buffer(const wgpu::Device& device) {
        if (!is_empty()) reserve(length, device);
    }
    void set_label(std::optional<std::string_view> new_label) {
        auto updated = new_label.transform([](std::string_view value) { return std::string(value); });
        if (updated != label) label_changed = true;
        label = std::move(updated);
    }
    std::optional<std::string_view> get_label() const noexcept {
        if (!label) return std::nullopt;
        return *label;
    }
};

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
private:
    /** @brief Cap on uniform buffer binding size to avoid oversized arrays on
     * macOS (Bevy MAX_REASONABLE_UNIFORM_BUFFER_BINDING_SIZE). */
    static constexpr std::size_t MAX_REASONABLE_UNIFORM_BUFFER_BINDING_SIZE = 1 << 20;

    /** @brief Elements of the current (incomplete) batch. */
    std::vector<T> temporary_values;
    /** @brief Byte storage of all flushed batches, offset-aligned (Bevy
     * DynamicUniformBuffer<MaxCapacityArray<Vec<T>>>). */
    std::vector<std::uint8_t> buffer_bytes;
    /** @brief The uploaded dynamic-uniform buffer. */
    wgpu::Buffer gpu_buffer;
    /** @brief Number of elements per batch (Bevy batch_size). */
    std::size_t capacity = 0;
    /** @brief Dynamic offset alignment. */
    std::size_t alignment = 0;
    /** @brief Byte offset of the next batch in the buffer. */
    std::size_t current_offset = 0;
public:
    explicit BatchedUniformBuffer(const wgpu::Limits& limits) {
        alignment = static_cast<std::size_t>(limits.minUniformBufferOffsetAlignment);
        if (alignment == 0) alignment = 256;
        capacity = batch_size(limits);
        temporary_values.reserve(capacity);
    }

    /** @brief Number of elements per batch (Bevy
     * BatchedUniformBuffer::batch_size: min(max_uniform_buffer_binding_size,
     * MAX_REASONABLE) / T::min_size). */
    static std::size_t batch_size(const wgpu::Limits& limits) {
        const std::size_t binding = std::min(static_cast<std::size_t>(limits.maxUniformBufferBindingSize),
                                             MAX_REASONABLE_UNIFORM_BUFFER_BINDING_SIZE);
        return binding / ShaderTypeInfo<T>::shader_size;
    }

    void clear() noexcept {
        temporary_values.clear();
        buffer_bytes.clear();
        current_offset = 0;
    }

    /** @brief Size of one fixed-capacity runtime array binding (Bevy
     * `BatchedUniformBuffer::size`). `std::size_t` is the C++ substitute for
     * Rust's `NonZero<u64>`; the `max(1, capacity)` rule preserves the same
     * nonzero size. */
    std::size_t size() const noexcept { return std::max<std::size_t>(1, capacity) * ShaderTypeInfo<T>::shader_size; }

    /** @brief Push one element into the current batch; when the batch fills,
     * flush it to the buffer (Bevy BatchedUniformBuffer::push). The returned
     * index is the in-batch element index; the dynamic offset is the byte
     * offset of the batch start. */
    GpuArrayBufferIndex<T> push(const T& value) {
        // Bevy captures the batch-start offset BEFORE the push (and possible
        // flush) so the first element of a batch points at that batch
        // (batched_uniform_buffer.rs:82-93).
        const std::uint32_t index  = static_cast<std::uint32_t>(temporary_values.size());
        const std::uint32_t offset = static_cast<std::uint32_t>(current_offset);
        temporary_values.push_back(value);
        if (temporary_values.size() == capacity) {
            flush();
        }
        return GpuArrayBufferIndex<T>{index, offset};
    }

    /** @brief Write the current batch into the byte buffer at the aligned
     * offset (Bevy BatchedUniformBuffer::flush). */
    void flush() {
        // The shader sees a fixed-size `array<T, capacity>` at every dynamic
        // offset (Bevy's MaxCapacityArray). Upload a full, zero-padded batch
        // even when the final batch is only partially populated; otherwise a
        // dynamic bind group with the required batch size would run past the
        // end of the native wgpu buffer.
        const std::size_t batch_bytes     = capacity * ShaderTypeInfo<T>::shader_size;
        auto encoded_values               = encode_shader_values<T>(temporary_values);
        buffer_bytes.insert(buffer_bytes.end(), encoded_values.begin(), encoded_values.end());
        // The insertion above copied only the populated prefix; make the
        // remaining fixed-capacity elements zero-initialized.
        if (temporary_values.size() < capacity) {
            buffer_bytes.resize(buffer_bytes.size() + (capacity - temporary_values.size()) * ShaderTypeInfo<T>::shader_size,
                                0);
        }
        temporary_values.clear();
        current_offset += align_up(batch_bytes, alignment);
        buffer_bytes.resize(current_offset, 0);
    }

    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        if (!temporary_values.empty()) flush();
        if (buffer_bytes.empty()) return;
        if (!gpu_buffer || gpu_buffer.getSize() < buffer_bytes.size()) {
            gpu_buffer = device.createBuffer(wgpu::BufferDescriptor()
                                             .setLabel("BatchedUniformBuffer")
                                             .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst)
                                             .setSize(buffer_bytes.size()));
        }
        if (gpu_buffer) {
            queue.writeBuffer(gpu_buffer, 0, buffer_bytes.data(), buffer_bytes.size());
        }
    }

    /** @brief The uploaded dynamic-uniform buffer, if any (Bevy
     * `BatchedUniformBuffer::binding`, without Rust's `BindingResource`
     * wrapper). The binding's required size is `size()`. */
    std::optional<std::reference_wrapper<const wgpu::Buffer>> binding() const noexcept {
        if (!gpu_buffer) return std::nullopt;
        return std::cref(gpu_buffer);
    }
};

/**
 * @brief Array of GPU-accessible elements, storage buffer where supported,
 * falling back to a dynamic-offset uniform buffer (Bevy `GpuArrayBuffer<T>`).
 */
template <GpuArrayBufferable T>
struct GpuArrayBuffer {
    /** @brief Backing storage: uniform fallback or storage buffer. */
    using Storage = std::variant<BatchedUniformBuffer<T>, BufferVec<T>>;

private:
    static Storage make_storage(const wgpu::Limits& limits) {
        if (limits.maxStorageBuffersPerShaderStage == 0) {
            return Storage{std::in_place_index<0>, limits};
        }
        return Storage{std::in_place_index<1>, wgpu::BufferUsage::eStorage};
    }

public:
    Storage storage;

    explicit GpuArrayBuffer(const wgpu::Limits& limits) : storage(make_storage(limits)) {}

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
                using Storage = std::decay_t<decltype(s)>;
                if constexpr (std::same_as<Storage, BufferVec<T>>) {
                    if (const auto* buffer = s.buffer()) return *buffer;
                    return std::nullopt;
                } else {
                    if (auto binding = s.binding()) return binding->get();
                    return std::nullopt;
                }
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
