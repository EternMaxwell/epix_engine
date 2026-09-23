#pragma once

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/camera.hpp>
#include <epix/pbr/light_cluster.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief The largest number of clusterable objects a uniform buffer can hold
 *  (Bevy `MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS`, `cluster.rs:21`). Kept in sync
 *  with `mesh_view_types.wgsl`. */
EPIX_EXPORT inline constexpr std::size_t MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS = 204;

/** @brief Clustered-forward rendering needs three storage-buffer bindings; the
 *  device support check uses this count (Bevy
 *  `CLUSTERED_FORWARD_STORAGE_BUFFER_COUNT`, `cluster.rs:29`). */
EPIX_EXPORT inline constexpr std::uint32_t CLUSTERED_FORWARD_STORAGE_BUFFER_COUNT = 3;

/** @brief One clustered light, probe or decal as the shader sees it (Bevy
 *  `GpuClusterableObject`, `cluster.rs:56-70`).
 *
 *  The field order is Bevy's, because the shader reads it by offset, and the
 *  size is 80 bytes: three `vec4`s then eight scalars, which is what Bevy's own
 *  `assert!(size_of::<GpuClusterableObject>() * MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS <= 16384)`
 *  (`:24-25`) depends on to stay inside a WebGL2 uniform buffer. */
EPIX_EXPORT struct GpuClusterableObject {
    /** @brief Point lights: the lower-right 2x2 of the projection matrix.
     *  Spot lights: direction (x, z), spot scale, spot offset. */
    glm::vec4 light_custom_data{0.0f};
    glm::vec4 color_inverse_square_range{0.0f};
    glm::vec4 position_radius{0.0f};
    std::uint32_t flags = 0;
    float shadow_depth_bias = 0.0f;
    float shadow_normal_bias = 0.0f;
    float spot_light_tan_angle = 0.0f;
    float soft_shadow_size = 0.0f;
    float shadow_map_near_z = 0.0f;
    std::uint32_t decal_index = 0;
    float pad = 0.0f;
};

// Bevy's compile-time guard (`cluster.rs:24-25`): the full uniform array has to
// stay inside the WebGL2 uniform binding, so a future field addition that
// overruns it fails the build instead of the device.
static_assert(sizeof(GpuClusterableObject) * MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS <= 16384,
              "GpuClusterableObject must fit the 16 KiB WebGL2 uniform binding");

/** @brief The fixed-size form: a full uniform array of clusterable objects
 *  (Bevy `GpuClusterableObjectsUniform`, `cluster.rs:83-86`). */
EPIX_EXPORT struct GpuClusterableObjectsUniform {
    std::array<GpuClusterableObject, MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS> data{};
};

/** @brief The runtime-sized form: only the objects actually in use (Bevy
 *  `GpuClusterableObjectsStorage`, `:88-92`, whose only member is
 *  `#[shader(size(runtime))] data: Vec<GpuClusterableObject>`). */
EPIX_EXPORT struct GpuClusterableObjectsStorage {
    std::vector<GpuClusterableObject> data;
};

/** @brief A cluster's starting offset followed by its point-light, spot-light,
 *  reflection-probe and irradiance-volume counts (Bevy
 *  `GpuClusterOffsetsAndCountsStorage.data`, `cluster.rs:124-131`). */
EPIX_EXPORT using GpuClusterOffsetsAndCounts = std::array<glm::uvec4, 2>;

/** @brief The runtime-sized cluster index list (Bevy
 *  `GpuClusterableObjectIndexListsStorage`, `:118-122`). */
EPIX_EXPORT struct GpuClusterableObjectIndexListsStorage {
    std::vector<std::uint32_t> data;
};

/** @brief The runtime-sized offsets and counts (Bevy
 *  `GpuClusterOffsetsAndCountsStorage`, `:124-131`). */
EPIX_EXPORT struct GpuClusterOffsetsAndCountsStorage {
    std::vector<GpuClusterOffsetsAndCounts> data;
};

/** @brief Bits each of a cluster's two counts occupies in the packed word; this
 *  must match `CLUSTER_COUNT_SIZE` in `pbr.wgsl` and be large enough for
 *  `MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS` (Bevy `cluster.rs:31-36`). */
EPIX_EXPORT inline constexpr std::uint32_t CLUSTER_COUNT_SIZE = 9;
/** @brief The mask for the offset field, which takes the remaining bits. */
EPIX_EXPORT inline constexpr std::uint32_t CLUSTER_OFFSET_MASK = (1u << (32 - (CLUSTER_COUNT_SIZE * 2))) - 1;
/** @brief The mask for each count field. */
EPIX_EXPORT inline constexpr std::uint32_t CLUSTER_COUNT_MASK = (1u << CLUSTER_COUNT_SIZE) - 1;

/** @brief Pack a cluster's offset and its point and spot light counts into one
 *  word (Bevy `pack_offset_and_counts`, `cluster.rs:551-555`).
 *
 *  This is the path taken when the device cannot offer the three storage
 *  bindings clustered forward wants, so the cluster data goes into uniform
 *  buffers instead. Each field is masked to its width, which is what Bevy's
 *  `& MASK` does: an out-of-range value silently keeps only the low bits. */
EPIX_EXPORT inline std::uint32_t pack_offset_and_counts(std::size_t offset, std::uint32_t point_count,
                                                        std::uint32_t spot_count) {
    return ((static_cast<std::uint32_t>(offset) & CLUSTER_OFFSET_MASK) << (CLUSTER_COUNT_SIZE * 2)) |
           ((point_count & CLUSTER_COUNT_MASK) << CLUSTER_COUNT_SIZE) | (spot_count & CLUSTER_COUNT_MASK);
}

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief Three `vec4`s then eight scalars: Bevy's uniform layout is the object
 *  representation, so the raw encoder applies. */
template <>
struct ShaderTypeInfo<::epix::pbr::GpuClusterableObject>
    : RawShaderType<::epix::pbr::GpuClusterableObject> {};

/** @brief The fixed uniform array is a plain array of those objects. */
template <>
struct ShaderTypeInfo<::epix::pbr::GpuClusterableObjectsUniform>
    : RawShaderType<::epix::pbr::GpuClusterableObjectsUniform> {};

/** @brief A runtime-sized storage buffer (Bevy `#[shader(size(runtime))]`): the
 *  minimum binding size is zero because the struct is nothing but the runtime
 *  array, and the written size follows the element count. */
template <>
struct ShaderTypeInfo<::epix::pbr::GpuClusterableObjectsStorage> {
    static constexpr std::size_t shader_size = 0;
    static std::size_t size_for(const ::epix::pbr::GpuClusterableObjectsStorage& value) noexcept {
        return value.data.size() * sizeof(::epix::pbr::GpuClusterableObject);
    }
    static void write_into(const ::epix::pbr::GpuClusterableObjectsStorage& value,
                           std::span<std::uint8_t> destination) {
        const std::size_t size = size_for(value);
        if (destination.size() < size) throw std::out_of_range("Shader destination is too small.");
        if (size != 0) std::memcpy(destination.data(), value.data.data(), size);
    }
};

/** @brief A runtime-sized `u32` list (Bevy `GpuClusterableObjectIndexListsStorage`). */
template <>
struct ShaderTypeInfo<::epix::pbr::GpuClusterableObjectIndexListsStorage> {
    static constexpr std::size_t shader_size = 0;
    static std::size_t size_for(const ::epix::pbr::GpuClusterableObjectIndexListsStorage& value) noexcept {
        return value.data.size() * sizeof(std::uint32_t);
    }
    static void write_into(const ::epix::pbr::GpuClusterableObjectIndexListsStorage& value,
                           std::span<std::uint8_t> destination) {
        const std::size_t size = size_for(value);
        if (destination.size() < size) throw std::out_of_range("Shader destination is too small.");
        if (size != 0) std::memcpy(destination.data(), value.data.data(), size);
    }
};

/** @brief A runtime-sized `array<vec4<u32>, 2>` list (Bevy
 *  `GpuClusterOffsetsAndCountsStorage`). */
template <>
struct ShaderTypeInfo<::epix::pbr::GpuClusterOffsetsAndCountsStorage> {
    static constexpr std::size_t shader_size = 0;
    static std::size_t size_for(const ::epix::pbr::GpuClusterOffsetsAndCountsStorage& value) noexcept {
        return value.data.size() * sizeof(::epix::pbr::GpuClusterOffsetsAndCounts);
    }
    static void write_into(const ::epix::pbr::GpuClusterOffsetsAndCountsStorage& value,
                           std::span<std::uint8_t> destination) {
        const std::size_t size = size_for(value);
        if (destination.size() < size) throw std::out_of_range("Shader destination is too small.");
        if (size != 0) std::memcpy(destination.data(), value.data.data(), size);
    }
};
}  // namespace epix::render::render_resource

namespace epix::pbr {

/** @brief The clustered objects of the whole scene, in whichever buffer form the
 *  device supports (Bevy `GpuClusterableObjects`, `bevy_pbr/src/cluster.rs:78-81`
 *  and its `impl` at `:171-232`).
 *
 *  It sits after the `ShaderTypeInfo` specialisations above, because
 *  `UniformBuffer`/`StorageBuffer` require their element types to be shader
 *  types, and those specialisations are what makes them so.
 *
 *  `set` clamps to `MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS` in the uniform case,
 *  because a uniform buffer cannot grow, and replaces the list in the storage
 *  case. */
EPIX_EXPORT struct GpuClusterableObjects {
    using Uniform = render::render_resource::UniformBuffer<GpuClusterableObjectsUniform>;
    using Storage = render::render_resource::StorageBuffer<GpuClusterableObjectsStorage>;

    std::variant<Uniform, Storage> buffer;

    /** @brief The form the device supports (Bevy `GpuClusterableObjects::new`). */
    static GpuClusterableObjects from_binding_type(wgpu::BufferBindingType binding_type) {
        if (binding_type == wgpu::BufferBindingType::eUniform) return GpuClusterableObjects{.buffer = Uniform{}};
        return GpuClusterableObjects{.buffer = Storage{}};
    }

    /** @brief The uniform form, used when the device lacks three storage
     *  bindings. */
    static GpuClusterableObjects uniform() { return GpuClusterableObjects{.buffer = Uniform{}}; }
    /** @brief The storage form. */
    static GpuClusterableObjects storage() { return GpuClusterableObjects{.buffer = Storage{}}; }

    /** @brief Store the frame's clusterable objects (Bevy
     *  `GpuClusterableObjects::set`, `cluster.rs:187-202`). */
    void set(std::vector<GpuClusterableObject> clusterable_objects) {
        if (auto* uniform = std::get_if<Uniform>(&buffer)) {
            const std::size_t count = std::min(clusterable_objects.size(), MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS);
            auto& destination       = uniform->get_mut().data;
            for (std::size_t index = 0; index < count; ++index) destination[index] = clusterable_objects[index];
            return;
        }
        std::get<Storage>(buffer).get_mut().data = std::move(clusterable_objects);
    }

    /** @brief Upload the stored objects, in whichever form is held. */
    void write_buffer(const wgpu::Device& device, const wgpu::Queue& queue) {
        if (auto* uniform = std::get_if<Uniform>(&buffer)) {
            uniform->write_buffer(device, queue);
            return;
        }
        std::get<Storage>(buffer).write_buffer(device, queue);
    }

    /** @brief The buffer to bind, once written (Bevy
     *  `GpuClusterableObjects::binding`). */
    std::optional<std::reference_wrapper<const wgpu::Buffer>> binding() const {
        if (const auto* uniform = std::get_if<Uniform>(&buffer)) return uniform->binding();
        return std::get<Storage>(buffer).binding();
    }
};

/** @brief The scene-wide clustered objects and the entity-to-index map that
 *  `prepare_lights` fills (Bevy `GlobalClusterableObjectMeta`,
 *  `cluster.rs:72-76,162-169`). */
EPIX_EXPORT struct GlobalClusterableObjectMeta {
    GpuClusterableObjects gpu_clusterable_objects;
    std::unordered_map<ecs::Entity, std::size_t> entity_to_index;

    GlobalClusterableObjectMeta() = default;
    explicit GlobalClusterableObjectMeta(wgpu::BufferBindingType binding_type)
        : gpu_clusterable_objects(GpuClusterableObjects::from_binding_type(binding_type)) {}
};

/** @brief A view's cluster-binding limits (Bevy `ViewClusterBindings`,
 *  `bevy_pbr/src/cluster.rs:339-341`).
 *
 *  `MAX_OFFSETS` and `MAX_INDICES` come from the 16 KiB uniform buffer binding
 *  limit: 16384 bytes of `uvec4`s is 4096 offsets, and 16384 `u32` indices fit
 *  the index list. `MAX_UNIFORM_ITEMS` is the number of `uvec4`s those offsets
 *  occupy, four offsets to a `uvec4`. */
EPIX_EXPORT inline constexpr std::size_t VIEW_CLUSTER_MAX_OFFSETS = 16384 / 4;
EPIX_EXPORT inline constexpr std::size_t VIEW_CLUSTER_MAX_UNIFORM_ITEMS = VIEW_CLUSTER_MAX_OFFSETS / 4;
EPIX_EXPORT inline constexpr std::size_t VIEW_CLUSTER_MAX_INDICES = 16384;

/** @brief Where a cluster's packed offset and counts go in the uniform form of
 *  the offset list (Bevy `ViewClusterBindings::push_offset_and_counts`,
 *  `cluster.rs:378-383`): four packed words per `uvec4`, so the entry is word
 *  `n_offsets >> 2` of the array and component `n_offsets & 3` within it. */
EPIX_EXPORT inline constexpr std::size_t uniform_offsets_array_index(std::size_t n_offsets) noexcept {
    return n_offsets >> 2;
}
EPIX_EXPORT inline constexpr std::size_t uniform_offsets_component(std::size_t n_offsets) noexcept {
    return n_offsets & 3;
}

/** @brief The two `uvec4`s a cluster occupies in the storage form of the offset
 *  list (Bevy `ViewClusterBindings::push_offset_and_counts`,
 *  `cluster.rs:393-401`), in Bevy's field order: offset, point lights, spot
 *  lights and reflection probes in the first, irradiance volumes, decals and two
 *  zeroes in the second.
 *
 *  The uniform form cannot carry the probe counts at all, which is why it packs
 *  only the point and spot counts - see `pack_offset_and_counts`. */
EPIX_EXPORT inline GpuClusterOffsetsAndCounts pack_cluster_offsets_and_counts_storage(
    std::size_t offset, const ClusterableObjectCounts& counts) {
    return GpuClusterOffsetsAndCounts{
        glm::uvec4(static_cast<std::uint32_t>(offset), counts.point_lights, counts.spot_lights,
                   counts.reflection_probes),
        glm::uvec4(counts.irradiance_volumes, counts.decals, 0u, 0u),
    };
}

/** @brief Where an index lands in the uniform form of the index list (Bevy
 *  `ViewClusterBindings::push_index`, `bevy_pbr/src/cluster.rs:418-424`).
 *
 *  A `uvec4` holds sixteen indices - four per word - so the entry is word
 *  `n_indices >> 4` of the array, component `(n_indices >> 2) & 3` within it,
 *  and byte `n_indices & 3` of that word. Each index occupies one byte, which is
 *  why the uniform form cannot hold more than 255 clusterable objects per view. */
EPIX_EXPORT inline constexpr std::size_t uniform_index_lists_array_index(std::size_t n_indices) noexcept {
    return n_indices >> 4;
}
EPIX_EXPORT inline constexpr std::size_t uniform_index_lists_component(std::size_t n_indices) noexcept {
    return (n_indices >> 2) & 3;
}
EPIX_EXPORT inline constexpr std::size_t uniform_index_lists_sub_index(std::size_t n_indices) noexcept {
    return n_indices & 3;
}

/** @brief Or one index into the word it belongs to, which is Bevy's
 *  `|= index << (8 * sub_index)` (`cluster.rs:423-424`); the caller has already
 *  cleared the word if the slot must be empty. */
EPIX_EXPORT inline std::uint32_t pack_index_into_uniform_word(std::uint32_t word, std::size_t n_indices,
                                                              std::uint32_t index) {
    return word | (index << (8 * uniform_index_lists_sub_index(n_indices)));
}

/** @brief The uniform form of a view's cluster index list (Bevy
 *  `GpuClusterableObjectIndexListsUniform`, `cluster.rs:557-560`). Its elements
 *  are `uvec4`s because every array in a std140 layout is 16-byte aligned, which
 *  is what the index packing above assumes. */
EPIX_EXPORT struct GpuClusterableObjectIndexListsUniform {
    std::array<glm::uvec4, VIEW_CLUSTER_MAX_UNIFORM_ITEMS> data{};
};

// Bevy's compile-time guard (`cluster.rs:562-564`): the index list is exactly at
// the binding's limit, so it is the assertion that keeps it there.
static_assert(sizeof(GpuClusterableObjectIndexListsUniform) <= 16384,
              "the cluster index list must fit the 16 KiB WebGL2 uniform binding");

/** @brief The uniform form of a view's cluster offsets and counts (Bevy
 *  `GpuClusterOffsetsAndCountsUniform`, `cluster.rs:113-116`), with the same
 *  `uvec4` element rule. */
EPIX_EXPORT struct GpuClusterOffsetsAndCountsUniform {
    std::array<glm::uvec4, VIEW_CLUSTER_MAX_UNIFORM_ITEMS> data{};
};

}  // namespace epix::pbr

namespace epix::render::render_resource {
template <>
struct ShaderTypeInfo<::epix::pbr::GpuClusterableObjectIndexListsUniform>
    : RawShaderType<::epix::pbr::GpuClusterableObjectIndexListsUniform> {};
template <>
struct ShaderTypeInfo<::epix::pbr::GpuClusterOffsetsAndCountsUniform>
    : RawShaderType<::epix::pbr::GpuClusterOffsetsAndCountsUniform> {};
}  // namespace epix::render::render_resource

namespace epix::pbr {

/** @brief A view's cluster buffers in the uniform form (Bevy
 *  `ViewClusterBuffers::Uniform`, `cluster.rs:512-517`). */
EPIX_EXPORT struct ViewClusterBuffersUniform {
    render::render_resource::UniformBuffer<GpuClusterableObjectIndexListsUniform> clusterable_object_index_lists;
    render::render_resource::UniformBuffer<GpuClusterOffsetsAndCountsUniform> cluster_offsets_and_counts;
};

/** @brief A view's cluster buffers in the storage form (Bevy
 *  `ViewClusterBuffers::Storage`, `cluster.rs:519-524`). */
EPIX_EXPORT struct ViewClusterBuffersStorage {
    render::render_resource::StorageBuffer<GpuClusterableObjectIndexListsStorage> clusterable_object_index_lists;
    render::render_resource::StorageBuffer<GpuClusterOffsetsAndCountsStorage> cluster_offsets_and_counts;
};

/** @brief A view's cluster buffers, in whichever form the device supports (Bevy
 *  `ViewClusterBuffers`, `cluster.rs:504-525`). */
EPIX_EXPORT struct ViewClusterBuffers {
    std::variant<ViewClusterBuffersUniform, ViewClusterBuffersStorage> buffers;

    /** @brief The form the device supports. */
    static ViewClusterBuffers from_binding_type(wgpu::BufferBindingType binding_type) {
        if (binding_type == wgpu::BufferBindingType::eUniform) return uniform();
        return storage();
    }
    static ViewClusterBuffers uniform() { return ViewClusterBuffers{.buffers = ViewClusterBuffersUniform{}}; }
    static ViewClusterBuffers storage() { return ViewClusterBuffers{.buffers = ViewClusterBuffersStorage{}}; }
};

/** @brief One view's cluster bindings: the two cluster buffers plus the counts of
 *  what has been written into them (Bevy `ViewClusterBindings`,
 *  `cluster.rs:147-151` and its `impl` at `:338-502`).
 *
 *  `push_offset_and_counts` and `push_index` are where the two forms diverge: the
 *  uniform form packs into preallocated `uvec4` arrays and drops an offset that
 *  would overrun the array (Bevy warns and returns, leaving the count untouched),
 *  while the storage form appends to its vectors and has no bound. Both are
 *  CPU-side, so they are verifiable without a device. */
EPIX_EXPORT struct ViewClusterBindings {
    std::size_t n_indices = 0;
    std::size_t n_offsets = 0;
    ViewClusterBuffers buffers;

    ViewClusterBindings() = default;
    explicit ViewClusterBindings(wgpu::BufferBindingType binding_type)
        : buffers(ViewClusterBuffers::from_binding_type(binding_type)) {}

    /** @brief Zero the buffers, keeping their allocation (Bevy
     *  `ViewClusterBindings::clear`, `cluster.rs:351-370`). */
    void clear() {
        if (auto* uniform = std::get_if<ViewClusterBuffersUniform>(&buffers.buffers)) {
            uniform->clusterable_object_index_lists.get_mut().data = {};
            uniform->cluster_offsets_and_counts.get_mut().data    = {};
            return;
        }
        auto& storage = std::get<ViewClusterBuffersStorage>(buffers.buffers);
        storage.clusterable_object_index_lists.get_mut().data.clear();
        storage.cluster_offsets_and_counts.get_mut().data.clear();
    }

    /** @brief Record one cluster's offset and counts (Bevy
     *  `ViewClusterBindings::push_offset_and_counts`, `cluster.rs:372-406`). */
    void push_offset_and_counts(std::size_t offset, const ClusterableObjectCounts& counts) {
        if (auto* uniform = std::get_if<ViewClusterBuffersUniform>(&buffers.buffers)) {
            const std::size_t array_index = uniform_offsets_array_index(n_offsets);
            if (array_index >= VIEW_CLUSTER_MAX_UNIFORM_ITEMS) {
                // Bevy warns and returns without counting the offset (cluster.rs:380-383).
                spdlog::warn("[pbr] cluster offset and count out of bounds!");
                return;
            }
            const std::size_t component = uniform_offsets_component(n_offsets);
            uniform->cluster_offsets_and_counts.get_mut().data[array_index][component] =
                pack_offset_and_counts(offset, counts.point_lights, counts.spot_lights);
        } else {
            auto& storage = std::get<ViewClusterBuffersStorage>(buffers.buffers);
            storage.cluster_offsets_and_counts.get_mut().data.push_back(
                pack_cluster_offsets_and_counts_storage(offset, counts));
        }
        ++n_offsets;
    }

    /** @brief Record one clusterable object index (Bevy
     *  `ViewClusterBindings::push_index`, `cluster.rs:412-438`). Bevy does not
     *  bound this one, so neither does this. */
    void push_index(std::size_t index) {
        if (auto* uniform = std::get_if<ViewClusterBuffersUniform>(&buffers.buffers)) {
            const std::size_t array_index = uniform_index_lists_array_index(n_indices);
            const std::size_t component   = uniform_index_lists_component(n_indices);
            auto& word                    = uniform->clusterable_object_index_lists.get_mut().data[array_index][component];
            word = pack_index_into_uniform_word(word, n_indices, static_cast<std::uint32_t>(index));
        } else {
            std::get<ViewClusterBuffersStorage>(buffers.buffers)
                .clusterable_object_index_lists.get_mut()
                .data.push_back(static_cast<std::uint32_t>(index));
        }
        ++n_indices;
    }

    /** @brief The number of indices pushed (Bevy `n_indices`). */
    std::size_t indices_count() const noexcept { return n_indices; }
    /** @brief The number of offsets pushed (Bevy `n_offsets`). */
    std::size_t offsets_count() const noexcept { return n_offsets; }

    /** @brief Upload both cluster buffers (Bevy
     *  `ViewClusterBindings::write_buffers`, `cluster.rs:440-457`). */
    void write_buffers(const wgpu::Device& device, const wgpu::Queue& queue) {
        if (auto* uniform = std::get_if<ViewClusterBuffersUniform>(&buffers.buffers)) {
            uniform->clusterable_object_index_lists.write_buffer(device, queue);
            uniform->cluster_offsets_and_counts.write_buffer(device, queue);
            return;
        }
        auto& storage = std::get<ViewClusterBuffersStorage>(buffers.buffers);
        storage.clusterable_object_index_lists.write_buffer(device, queue);
        storage.cluster_offsets_and_counts.write_buffer(device, queue);
    }

    /** @brief The index list buffer, once written (Bevy
     *  `clusterable_object_index_lists_binding`, `cluster.rs:459-470`). */
    std::optional<std::reference_wrapper<const wgpu::Buffer>> clusterable_object_index_lists_binding() const {
        if (const auto* uniform = std::get_if<ViewClusterBuffersUniform>(&buffers.buffers))
            return uniform->clusterable_object_index_lists.binding();
        return std::get<ViewClusterBuffersStorage>(buffers.buffers).clusterable_object_index_lists.binding();
    }

    /** @brief The offsets and counts buffer, once written (Bevy
     *  `offsets_and_counts_binding`, `cluster.rs:472-483`). */
    std::optional<std::reference_wrapper<const wgpu::Buffer>> offsets_and_counts_binding() const {
        if (const auto* uniform = std::get_if<ViewClusterBuffersUniform>(&buffers.buffers))
            return uniform->cluster_offsets_and_counts.binding();
        return std::get<ViewClusterBuffersStorage>(buffers.buffers).cluster_offsets_and_counts.binding();
    }
};

/** @brief A view's cluster geometry, extracted from the main world (Bevy
 *  `ExtractedClusterConfig`, `bevy_pbr/src/cluster.rs:95-101`): the special near
 *  value the first slice uses, the far plane, and the number of clusters in X, Y
 *  and Z. */
EPIX_EXPORT struct ExtractedClusterConfig {
    /** @brief Special near value for cluster calculations. Spelled
     *  `near_plane`/`far_plane` rather than Bevy's `near`/`far`, because those
     *  two are macros in the Windows SDK and a macro expands a whole token. */
    float near_plane = 0.0f;
    float far_plane  = 0.0f;
    /** @brief The number of clusters in X, Y and Z in the view frustum. */
    glm::uvec3 dimensions{1, 1, 1};
};

/** @brief One entry of a view's extracted clusterable objects (Bevy
 *  `ExtractedClusterableObjectElement`, `cluster.rs:103-106`): either a cluster's
 *  header, which carries the counts for the cluster that follows, or one
 *  clusterable object's entity.
 *
 *  The data is a flat list in cluster order, so the header-then-entities
 *  sequence is what lets `prepare_lights` fill the index lists without an
 *  intermediate structure. */
EPIX_EXPORT struct ExtractedClusterableObjectElement {
    enum class Kind {
        /** @brief The counts for the cluster whose objects follow. */
        ClusterHeader,
        /** @brief One clusterable object. */
        ClusterableObjectEntity,
    } kind = Kind::ClusterHeader;

    /** @brief The counts, when `kind` is `ClusterHeader`. */
    ClusterableObjectCounts counts{};
    /** @brief The object, when `kind` is `ClusterableObjectEntity`. */
    ecs::Entity entity{};
};

/** @brief The clusterable objects a view extracted, as the flat list above (Bevy
 *  `ExtractedClusterableObjects`, `cluster.rs:108-111`). */
EPIX_EXPORT struct ExtractedClusterableObjects {
    std::vector<ExtractedClusterableObjectElement> data;
};

/** @brief Walk a view's extracted records and fill its cluster bindings (Bevy
 *  `prepare_clusters`, `bevy_pbr/src/cluster.rs:302-330`).
 *
 *  Bevy inlines this walk in the system, which then uploads the buffers and
 *  inserts the bindings; it is a function here so the walk itself - which is
 *  where the behaviour lives - can be verified without a device.
 *
 *  Three rules come from the walk: a cluster's offset is the *current* index
 *  count, so it points at the first index the cluster's objects will add; an
 *  object with no entry in the global index map is skipped; and in the uniform
 *  form, where the index list cannot grow past `MAX_INDICES`, hitting that limit
 *  stops the walk for the whole view, keeping what was already written. The
 *  storage form has no such limit. */
EPIX_EXPORT inline ViewClusterBindings prepare_view_cluster_bindings(
    const std::vector<ExtractedClusterableObjectElement>& data,
    const std::unordered_map<ecs::Entity, std::size_t>& entity_to_index, wgpu::BufferBindingType binding_type,
    bool supports_storage_buffers) {
    ViewClusterBindings bindings{binding_type};
    bindings.clear();

    for (const auto& record : data) {
        if (record.kind == ExtractedClusterableObjectElement::Kind::ClusterHeader) {
            bindings.push_offset_and_counts(bindings.indices_count(), record.counts);
            continue;
        }
        const auto found = entity_to_index.find(record.entity);
        if (found == entity_to_index.end()) continue;
        if (bindings.indices_count() >= VIEW_CLUSTER_MAX_INDICES && !supports_storage_buffers) {
            // Bevy warns here, once per view that overflows (cluster.rs:317-325).
            spdlog::warn(
                "[pbr] Clusterable object index lists are full! The clusterable objects in the view are present in too "
                "many clusters.");
            break;
        }
        bindings.push_index(found->second);
    }
    return bindings;
}

/** @brief What one view's extraction produces (Bevy `extract_clusters`,
 *  `bevy_pbr/src/cluster.rs:278-285`): the flat records plus the view's cluster
 *  geometry. */
EPIX_EXPORT struct ExtractedViewClusters {
    ExtractedClusterableObjects objects;
    ExtractedClusterConfig config;
};

/** @brief Build a view's extracted cluster records from its `Clusters`
 *  component (Bevy `extract_clusters`, `bevy_pbr/src/cluster.rs:259-285`).
 *
 *  The mapper is called with each clusterable object's main-world entity and
 *  fills the render-world entity to record; returning false means that entity
 *  was not synced, and Bevy skips it rather than failing - the same
 *  `mapper.get(...).is_ok()` shape. Bevy also reserves
 *  `clusters.len() + total_entities` up front, which the reserve below mirrors.
 *
 *  The system half - the `Extract` query with `RenderEntity`, the removal from
 *  inactive cameras, and the component insertion - lands with the plugin that
 *  schedules it; this is the part with the behaviour. */
template <typename Mapper>
    requires std::invocable<Mapper&, ecs::Entity, ecs::Entity&>
EPIX_EXPORT ExtractedViewClusters extract_view_clusters(const Clusters& clusters, Mapper&& mapper) {
    std::size_t entity_count = 0;
    for (const auto& cluster : clusters.clusterable_objects) entity_count += cluster.entities.size();

    ExtractedViewClusters extracted;
    extracted.objects.data.reserve(clusters.clusterable_objects.size() + entity_count);
    for (const auto& cluster : clusters.clusterable_objects) {
        extracted.objects.data.push_back(ExtractedClusterableObjectElement{
            .kind = ExtractedClusterableObjectElement::Kind::ClusterHeader, .counts = cluster.counts});
        for (const ecs::Entity clusterable : cluster.entities) {
            ecs::Entity render_entity{};
            if (!mapper(clusterable, render_entity)) continue;  // not synced: Bevy skips it
            extracted.objects.data.push_back(ExtractedClusterableObjectElement{
                .kind = ExtractedClusterableObjectElement::Kind::ClusterableObjectEntity,
                .entity = render_entity});
        }
    }

    extracted.config = ExtractedClusterConfig{
        .near_plane = clusters.near_plane, .far_plane = clusters.far_plane, .dimensions = clusters.dimensions};
    return extracted;
}

/** @brief Extract each view's cluster records into the render world (Bevy
 *  `extract_clusters`, `bevy_pbr/src/cluster.rs:245-287`).
 *
 *  Every view is a main-world entity carrying `RenderEntity`, `Clusters` and
 *  `Camera`. A view whose camera is not active has both extracted components
 *  *removed* - that is how a paused camera stops feeding `prepare_clusters` -
 *  and an active one has them inserted on its render entity, which is the
 *  `RenderEntity` the query already required.
 *
 *  The mapper query is Bevy's: it resolves each clusterable object's main-world
 *  entity to its render entity inside `extract_view_clusters`, and an object
 *  that has no `RenderEntity` is skipped rather than failing. Bevy reads the
 *  render entity through `Commands::get_entity(..).expect(..)`; Epix's
 *  `Commands::entity` does not validate, so the expectation has no counterpart
 *  here. */
EPIX_EXPORT void extract_clusters(
    ecs::Commands commands,
    app::Extract<ecs::Query<ecs::Item<const ::epix::render::sync_world::RenderEntity&, const Clusters&,
                                       const ::epix::camera::Camera&>>>
        views,
    app::Extract<ecs::Query<ecs::Item<const ::epix::render::sync_world::RenderEntity&>>> mapper);

}  // namespace epix::pbr

