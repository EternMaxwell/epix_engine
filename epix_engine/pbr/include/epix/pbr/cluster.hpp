#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/render.hpp>
#include <glm/glm.hpp>
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
