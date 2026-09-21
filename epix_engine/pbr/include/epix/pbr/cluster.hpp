#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

}  // namespace epix::pbr

