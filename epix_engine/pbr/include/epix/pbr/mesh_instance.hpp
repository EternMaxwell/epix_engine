#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <limits>
#include <epix/assets.hpp>
#include <epix/camera/visibility.hpp>
#include <epix/mesh.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <unordered_map>
#include <epix/common.hpp>
#endif
#include <epix/pbr/material_bindings.hpp>
#include <epix/pbr/mesh_uniform.hpp>

namespace epix::pbr {

/** @brief Flag bits carried by a mesh instance (Bevy `RenderMeshInstanceFlags`,
 *  `bevy_pbr/src/render/mesh.rs:683-699`).
 *
 *  The bits are Bevy's, and their *values* matter rather than only their
 *  distinctness: they travel in `MeshUniform::flags` to the shader, which tests
 *  them by position.
 *
 *  Three of them exist for motion vectors: whether the instance had a transform,
 *  a skin or morph targets last frame. Without them the shader cannot tell "this
 *  instance has not moved" from "this instance had no previous data", and the
 *  difference is a wrong velocity rather than a zero one. */
EPIX_EXPORT struct RenderMeshInstanceFlags {
    enum Bits : std::uint8_t {
        NONE                   = 0,
        /** @brief The mesh casts shadows. */
        SHADOW_CASTER          = 1u << 0,
        /** @brief The mesh may participate in automatic batching. */
        AUTOMATIC_BATCHING     = 1u << 1,
        /** @brief The mesh had a transform last frame. */
        HAS_PREVIOUS_TRANSFORM = 1u << 2,
        /** @brief The mesh had a skin last frame. */
        HAS_PREVIOUS_SKIN      = 1u << 3,
        /** @brief The mesh had morph targets last frame. */
        HAS_PREVIOUS_MORPH     = 1u << 4,
    };

    std::uint8_t bits = NONE;

    constexpr bool contains(Bits flag) const noexcept { return (bits & static_cast<std::uint8_t>(flag)) != 0; }

    constexpr void insert(Bits flag) noexcept { bits |= static_cast<std::uint8_t>(flag); }
    constexpr void remove(Bits flag) noexcept { bits &= static_cast<std::uint8_t>(~static_cast<std::uint8_t>(flag)); }

    /** @brief Whether this instance may be batched automatically (Bevy
     *  `RenderMeshInstanceShared::should_batch`, `render/mesh.rs:894-900`). */
    constexpr bool should_batch() const noexcept { return contains(AUTOMATIC_BATCHING); }

    bool operator==(const RenderMeshInstanceFlags&) const = default;
};

/** @brief The index of a lightmap slab (Bevy `LightmapSlabIndex`,
 *  `bevy_pbr/src/lightmap/mod.rs:177`).
 *
 *  Bevy wraps a `NonMaxU32` - a `u32` that is never `u32::MAX` - so a slab index
 *  can never collide with a sentinel. The invariant is documented rather than
 *  enforced here, since the port has no non-max integer type. */
EPIX_EXPORT struct LightmapSlabIndex {
    std::uint32_t index = 0;
    auto operator<=>(const LightmapSlabIndex&) const = default;
};

/** @brief CPU data the render world keeps for each entity holding a mesh, shared
 *  by both uniform-building paths (Bevy `RenderMeshInstanceShared`,
 *  `bevy_pbr/src/render/mesh.rs:735-755`).
 *
 *  There is no default constructor, and that matches Bevy: the mesh id is what the
 *  record is *about*, and Epix's `AssetId` deletes its own default constructor for
 *  the same reason, offering `invalid()` as an explicit sentinel instead.
 *
 *  `center` is a representative local-space position taken from the mesh's
 *  AABB, used as a spatial proxy for view-dependent sorting rather than a true
 *  centre; `render_layers` is absent when the instance carries no layer component,
 *  which is not the same as belonging to none. */
EPIX_EXPORT struct RenderMeshInstanceShared {
    assets::AssetId<mesh::Mesh> mesh_asset_id;
    MaterialBindingId material_bindings_index{};
    RenderMeshInstanceFlags flags{};
    std::optional<LightmapSlabIndex> lightmap_slab_index;
    std::uint32_t tag = 0;
    std::optional<::epix::camera::RenderLayers> render_layers;
    glm::vec3 center{0.0f};

    /** @brief Whether this instance may be batched automatically (Bevy
     *  `RenderMeshInstanceShared::should_batch`, `render/mesh.rs:894-900`). */
    bool should_batch() const noexcept { return flags.should_batch(); }
};

/** @brief The CPU data the render world keeps for an instance when *not* using GPU
 *  uniform building (Bevy `RenderMeshInstanceCpu`, `render/mesh.rs:702-714`).
 *
 *  It pairs the shared record with the transform, which is what the CPU path turns
 *  into a `MeshUniform` itself rather than handing the transform to the GPU. */
EPIX_EXPORT struct RenderMeshInstanceCpu {
    RenderMeshInstanceShared shared;
    MeshTransforms transforms;
};

/** @brief The CPU data the render world keeps for an instance when using GPU
 *  uniform building (Bevy `RenderMeshInstanceGpu`, `render/mesh.rs:716-731`).
 *
 *  The transform is absent because the GPU builds the uniform from a
 *  `MeshInputUniform`; what stays here is a world-space centre for view-dependent
 *  sorting and the index of this instance's input uniform, which is Bevy's
 *  `NonMaxU32` - a `u32` that is never `u32::MAX` - and is documented here since the
 *  port has no non-max integer type. */
EPIX_EXPORT struct RenderMeshInstanceGpu {
    RenderMeshInstanceShared shared;
    glm::vec3 center{0.0f};
    std::uint32_t current_uniform_index = 0;
};

/** @brief What a queueing system needs in order to place a mesh entity in the
 *  right batch (Bevy `RenderMeshQueueData`, `render/mesh.rs:1319-1334`).
 *
 *  Bevy derefs it to the shared record, so it is the shared data plus a world-space
 *  centre and the instance's uniform index; the pointer carries that delegation here.
 *
 *  The centre is what the two sides differ on. The GPU path stores it and hands it
 *  back; the CPU path derives it on the spot, by taking the shared local-space
 *  centre through the instance's transform - which is why the CPU record keeps the
 *  transform and the GPU record does not need it. The CPU path has no uniform, so it
 *  reports a default index rather than inventing one. */
EPIX_EXPORT struct RenderMeshQueueData {
    const RenderMeshInstanceShared* shared = nullptr;
    glm::vec3 center{0.0f};
    render::phase::InputUniformIndex current_uniform_index{};

    const RenderMeshInstanceShared& record() const noexcept { return *shared; }
};

/** @brief The mesh instance records, one side of which is in use (Bevy
 *  `RenderMeshInstances`, `render/mesh.rs:903-924`).
 *
 *  Which side is populated depends on whether the uniform builder runs on the CPU or
 *  the GPU, so Bevy models it as an enum rather than merging the two shapes. The
 *  accessors dispatch; the two sides are otherwise separate, and only one of them is
 *  ever used in a given frame. */
EPIX_EXPORT struct RenderMeshInstances {
    using CpuMap = std::unordered_map<render::sync_world::MainEntity, RenderMeshInstanceCpu>;
    using GpuMap = std::unordered_map<render::sync_world::MainEntity, RenderMeshInstanceGpu>;

    enum class Kind { CpuBuilding, GpuBuilding };

    Kind kind = Kind::CpuBuilding;
    CpuMap cpu_instances;
    GpuMap gpu_instances;

    /** @brief The record set for the side in use (Bevy `RenderMeshInstances::new`,
     *  `render/mesh.rs:926-934`). */
    static RenderMeshInstances make(bool use_gpu_instance_buffer_builder) {
        RenderMeshInstances instances;
        instances.kind = use_gpu_instance_buffer_builder ? Kind::GpuBuilding : Kind::CpuBuilding;
        return instances;
    }

    bool is_cpu_building() const noexcept { return kind == Kind::CpuBuilding; }

    /** @brief The mesh asset attached to an entity, if any (Bevy
     *  `RenderMeshInstances::mesh_asset_id`, `render/mesh.rs:936-942`). */
    std::optional<assets::AssetId<mesh::Mesh>> mesh_asset_id(render::sync_world::MainEntity entity) const {
        if (is_cpu_building()) {
            const auto found = cpu_instances.find(entity);
            if (found == cpu_instances.end()) return std::nullopt;
            return found->second.shared.mesh_asset_id;
        }
        const auto found = gpu_instances.find(entity);
        if (found == gpu_instances.end()) return std::nullopt;
        return found->second.shared.mesh_asset_id;
    }

    /** @brief What a queueing system needs for an entity, if it holds a mesh (Bevy
     *  `RenderMeshInstances::render_mesh_queue_data`, `render/mesh.rs:944-954`). */
    std::optional<RenderMeshQueueData> render_mesh_queue_data(render::sync_world::MainEntity entity) const {
        if (is_cpu_building()) {
            const auto found = cpu_instances.find(entity);
            if (found == cpu_instances.end()) return std::nullopt;
            const RenderMeshInstanceCpu& instance = found->second;
            // The local-space centre through the instance's transform; the third
            // column carries the translation, as for any affine matrix.
            const glm::vec3 center =
                glm::mat3(instance.transforms.world_from_local) * instance.shared.center +
                glm::vec3(instance.transforms.world_from_local[3]);
            return RenderMeshQueueData{.shared = &instance.shared,
                                       .center = center,
                                       .current_uniform_index = render::phase::InputUniformIndex{}};
        }
        const auto found = gpu_instances.find(entity);
        if (found == gpu_instances.end()) return std::nullopt;
        const RenderMeshInstanceGpu& instance = found->second;
        return RenderMeshQueueData{.shared = &instance.shared,
                                   .center = instance.center,
                                   .current_uniform_index = render::phase::InputUniformIndex{
                                       instance.current_uniform_index}};
    }

    /** @brief Insert flags into the instance on whichever side is in use (Bevy
     *  `RenderMeshInstances::insert_mesh_instance_flags`, `render/mesh.rs:957-968`).
     *
     *  Silently does nothing for an entity with no record, as Bevy does. */
    void insert_mesh_instance_flags(render::sync_world::MainEntity entity, RenderMeshInstanceFlags flags) {
        if (is_cpu_building()) {
            const auto found = cpu_instances.find(entity);
            if (found == cpu_instances.end()) return;
            found->second.shared.flags.insert(static_cast<RenderMeshInstanceFlags::Bits>(flags.bits));
            return;
        }
        const auto found = gpu_instances.find(entity);
        if (found == gpu_instances.end()) return;
        found->second.shared.flags.insert(static_cast<RenderMeshInstanceFlags::Bits>(flags.bits));
    }
};

/** @brief What the GPU needs in order to cull a mesh instance (Bevy
 *  `MeshCullingData`, `bevy_pbr/src/render/mesh.rs:562-574,1280-1296`).
 *
 *  A model-space AABB: its centre and its half extents, each padded with an unused
 *  fourth float because the shader reads them as `vec4`s.
 *
 *  A mesh with no AABB gets an **infinitely large** one rather than a zero-sized
 *  one, and the choice matters: culling is a test for being outside, so an infinite
 *  box means the instance is never culled. A zero box would make it always culled -
 *  an invisible mesh instead of a visible one. */
EPIX_EXPORT struct MeshCullingData {
    glm::vec4 aabb_center{0.0f};
    glm::vec4 aabb_half_extents{0.0f};

    /** @brief The culling data for an AABB, or the conservative infinite box when
     *  there is none (Bevy `MeshCullingData::new`, `render/mesh.rs:1285-1296`). */
    static MeshCullingData make(const std::optional<::epix::camera::Aabb>& aabb) {
        if (!aabb.has_value()) {
            return MeshCullingData{.aabb_center = glm::vec4(0.0f),
                                   .aabb_half_extents = glm::vec4(glm::vec3(std::numeric_limits<float>::infinity()),
                                                                  0.0f)};
        }
        return MeshCullingData{.aabb_center = glm::vec4(aabb->center, 0.0f),
                               .aabb_half_extents = glm::vec4(aabb->half_extents, 0.0f)};
    }
};

static_assert(sizeof(MeshCullingData) == 32, "two vec4s, which is what the shader reads");
static_assert(offsetof(MeshCullingData, aabb_half_extents) == 16);

}  // namespace epix::pbr
