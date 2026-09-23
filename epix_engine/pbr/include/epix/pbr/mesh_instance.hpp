#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#include <epix/assets.hpp>
#include <epix/camera/visibility.hpp>
#include <epix/mesh.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <epix/common.hpp>
#endif
#include <epix/pbr/material_bindings.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/prepass.hpp>

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

    /** @brief The shared record for the GPU-building path, which learns the mesh id
     *  later, from `RenderMeshInstanceGpuBuilder::update` (Bevy
     *  `RenderMeshInstanceShared::for_gpu_building`, `render/mesh.rs:837-859`). */
    static RenderMeshInstanceShared for_gpu_building(const PreviousGlobalTransform* previous_transform,
                                                     const mesh::Mesh3d& mesh,
                                                     const mesh::MeshTag* tag,
                                                     bool not_shadow_caster,
                                                     bool no_automatic_batching,
                                                     const ::epix::camera::RenderLayers* render_layers,
                                                     const ::epix::camera::Aabb* aabb) {
        return for_cpu_building(previous_transform, mesh, tag, MaterialBindingId{}, not_shadow_caster,
                                no_automatic_batching, render_layers, aabb);
    }

    /** @brief The shared record for either path (Bevy
     *  `RenderMeshInstanceShared::for_cpu_building`, `render/mesh.rs:861-892`).
     *
     *  The flag body is load-bearing: the shadow-caster and automatic-batching bits
     *  are the *negations* of components that opt out, and
     *  `HAS_PREVIOUS_TRANSFORM` is set from the presence of the previous transform
     *  alone - which is what makes the motion-vector prepass cheap. A missing tag is
     *  0, a missing AABB leaves the centre at the origin, and the lightmap slab is
     *  absent (the GPU path fills it in later). */
    static RenderMeshInstanceShared for_cpu_building(const PreviousGlobalTransform* previous_transform,
                                                     const mesh::Mesh3d& mesh,
                                                     const mesh::MeshTag* tag,
                                                     MaterialBindingId material_bindings_index,
                                                     bool not_shadow_caster,
                                                     bool no_automatic_batching,
                                                     const ::epix::camera::RenderLayers* render_layers,
                                                     const ::epix::camera::Aabb* aabb) {
        RenderMeshInstanceFlags instance_flags{};
        if (!not_shadow_caster) instance_flags.insert(RenderMeshInstanceFlags::SHADOW_CASTER);
        if (!no_automatic_batching) instance_flags.insert(RenderMeshInstanceFlags::AUTOMATIC_BATCHING);
        if (previous_transform) instance_flags.insert(RenderMeshInstanceFlags::HAS_PREVIOUS_TRANSFORM);

        // The record has no default constructor (the mesh id is what it is about),
        // so every field is spelled out; the layer component is added after, since
        // its absence and an empty layer set are different things.
        RenderMeshInstanceShared shared{
            .mesh_asset_id           = mesh.handle.id(),
            .material_bindings_index = material_bindings_index,
            .flags                   = instance_flags,
            .lightmap_slab_index     = std::nullopt,
            .tag                     = tag ? tag->value : 0u,
            .render_layers           = std::nullopt,
            .center                  = aabb ? aabb->center : glm::vec3(0.0f),
        };
        if (render_layers) shared.render_layers = *render_layers;
        return shared;
    }
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
EPIX_EXPORT struct MeshCullingDataBuffer;

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

    /** @brief Flush this instance's culling data into the buffer at a slot, growing
     *  it with zeroed entries as needed (Bevy `MeshCullingData::update`,
     *  `render/mesh.rs:1298-1309`, which Bevy calls through
     *  `RenderMeshInstanceGpuBuilder::update`). */
    void update(MeshCullingDataBuffer& mesh_culling_data_buffer, std::size_t instance_data_index) const;
};

static_assert(sizeof(MeshCullingData) == 32, "two vec4s, which is what the shader reads");
static_assert(offsetof(MeshCullingData, aabb_half_extents) == 16);

}  // namespace epix::pbr

namespace epix::render::render_resource {

/** @brief `MeshCullingData` is uploaded through a `RawBufferVec`, and it is a pair
 *  of `vec4`s with every byte initialised, so the concept's escape hatch applies -
 *  the same shape `batching.hpp` uses for its work items. */
template <>
struct RawBufferElementInfo<::epix::pbr::MeshCullingData> {
    static constexpr bool has_no_uninit = true;
};

}  // namespace epix::render::render_resource

namespace epix::pbr {

/** @brief The buffer that holds each mesh's culling data (Bevy
 *  `MeshCullingDataBuffer`, `bevy_pbr/src/render/mesh.rs:576-583`).
 *
 *  Bevy's `#[derive(Resource, Deref, DerefMut)]` newtype over
 *  `RawBufferVec<MeshCullingData>`; C++ has no derive, so the buffer is reached
 *  through `operator*`/`operator->`, the same shape the ECS wrappers use for their
 *  `Deref`. To avoid wasting CPU time in the CPU culling case the buffer stays
 *  empty when GPU culling is not in use, exactly as Bevy documents. */
EPIX_EXPORT struct MeshCullingDataBuffer {
    /** @brief Bevy's `Default`: a `RawBufferVec` with storage usage (Bevy
     *  `impl Default`, `render/mesh.rs:1312-1317`), which is what the plugin's
     *  `init_resource` builds. */
    render::render_resource::RawBufferVec<MeshCullingData> buffer{wgpu::BufferUsage::eStorage};

    render::render_resource::RawBufferVec<MeshCullingData>& operator*() noexcept { return buffer; }
    const render::render_resource::RawBufferVec<MeshCullingData>& operator*() const noexcept { return buffer; }
    render::render_resource::RawBufferVec<MeshCullingData>* operator->() noexcept { return std::addressof(buffer); }
    const render::render_resource::RawBufferVec<MeshCullingData>* operator->() const noexcept {
        return std::addressof(buffer);
    }
};

inline void MeshCullingData::update(MeshCullingDataBuffer& mesh_culling_data_buffer,
                                    std::size_t instance_data_index) const {
    while (mesh_culling_data_buffer->len() < instance_data_index + 1) {
        mesh_culling_data_buffer->push(MeshCullingData{});
    }
    mesh_culling_data_buffer->set(static_cast<std::uint32_t>(instance_data_index), *this);
}

/** @brief Information gathered during the parallel part of mesh extraction when the
 *  GPU builds the uniforms (Bevy `RenderMeshInstanceGpuBuilder`,
 *  `bevy_pbr/src/render/mesh.rs:757-783`).
 *
 *  The mesh id is deliberately absent: the builder does not know it until the
 *  material bindings have been resolved, which is what
 *  `RenderMeshInstanceGpuBuilder::update` does. Bevy stores an `Affine3` and an
 *  `Option<NonMaxU32>`; Epix's convention is the full `glm::mat4` and a plain
 *  optional, with the non-max invariant documented as elsewhere. */
EPIX_EXPORT struct RenderMeshInstanceGpuBuilder {
    RenderMeshInstanceShared shared;
    glm::mat4 world_from_local{1.0f};
    glm::uvec2 lightmap_uv_rect{0u, 0u};
    std::optional<std::uint32_t> previous_input_index;
    MeshFlags mesh_flags{};
};

/** @brief The per-thread queue of GPU-built mesh instances filled during extraction
 *  (Bevy `RenderMeshInstanceGpuQueue`, `render/mesh.rs:785-820`).
 *
 *  Bevy models it as an enum with a `None` default and one variant per culling kind,
 *  because the CPU-culling shape omits the per-instance `MeshCullingData` and so
 *  wastes no space when GPU culling is off; `std::variant` is the C++ sum type, and
 *  `std::monostate` stands in for the unit `None` variant. */
EPIX_EXPORT struct RenderMeshInstanceGpuQueue {
    /** @brief The CPU-culling shape: what changed, and what became invisible. */
    struct CpuCulling {
        std::vector<std::pair<render::sync_world::MainEntity, RenderMeshInstanceGpuBuilder>> changed;
        std::vector<render::sync_world::MainEntity> removed;
    };
    /** @brief The GPU-culling shape, which also carries each instance's culling
     *  data. */
    struct GpuCulling {
        std::vector<std::tuple<render::sync_world::MainEntity, RenderMeshInstanceGpuBuilder, MeshCullingData>> changed;
        std::vector<render::sync_world::MainEntity> removed;
    };

    std::variant<std::monostate, CpuCulling, GpuCulling> queue;

    /** @brief Clear the queue for the coming frame, switching shape if the culling
     *  kind changed (Bevy `RenderMeshInstanceGpuQueue::init`, `:1029-1057`). The
     *  matching shape is cleared in place so its allocations survive; the other is
     *  replaced outright. */
    void init(bool any_gpu_culling) {
        if (any_gpu_culling) {
            if (auto* gpu = std::get_if<GpuCulling>(&queue)) {
                gpu->changed.clear();
                gpu->removed.clear();
                return;
            }
            queue = GpuCulling{};
            return;
        }
        if (auto* cpu = std::get_if<CpuCulling>(&queue)) {
            cpu->changed.clear();
            cpu->removed.clear();
            return;
        }
        queue = CpuCulling{};
    }

    /** @brief Queue a mesh instance (Bevy `RenderMeshInstanceGpuQueue::push`,
     *  `:1059-1098`). The culling data decides which shape the entry needs; if the
     *  queue is in the other shape - or in neither - it is replaced by a fresh queue
     *  holding this entry, which is what discards any removals queued before it, as
     *  in Bevy. */
    void push(render::sync_world::MainEntity entity,
              RenderMeshInstanceGpuBuilder builder,
              std::optional<MeshCullingData> culling_data) {
        if (!culling_data) {
            if (auto* cpu = std::get_if<CpuCulling>(&queue)) {
                cpu->changed.emplace_back(entity, std::move(builder));
                return;
            }
            CpuCulling promoted;
            promoted.changed.emplace_back(entity, std::move(builder));
            queue = std::move(promoted);
            return;
        }
        if (auto* gpu = std::get_if<GpuCulling>(&queue)) {
            gpu->changed.emplace_back(entity, std::move(builder), *culling_data);
            return;
        }
        GpuCulling promoted;
        promoted.changed.emplace_back(entity, std::move(builder), *culling_data);
        queue = std::move(promoted);
    }

    /** @brief Queue an entity for removal (Bevy `RenderMeshInstanceGpuQueue::remove`,
     *  `:1100-1122`). An empty queue takes the shape the culling flag asks for; a
     *  queue that already has one keeps it, whatever the flag says. */
    void remove(render::sync_world::MainEntity entity, bool gpu_culling) {
        if (auto* cpu = std::get_if<CpuCulling>(&queue)) {
            cpu->removed.push_back(entity);
            return;
        }
        if (auto* gpu = std::get_if<GpuCulling>(&queue)) {
            gpu->removed.push_back(entity);
            return;
        }
        if (gpu_culling) {
            GpuCulling promoted;
            promoted.removed.push_back(entity);
            queue = std::move(promoted);
            return;
        }
        CpuCulling promoted;
        promoted.removed.push_back(entity);
        queue = std::move(promoted);
    }
};

/** @brief The queues mesh instances are collected into during extraction (Bevy
 *  `RenderMeshInstanceGpuQueues`, `bevy_pbr/src/render/mesh.rs:822-828`).
 *
 *  Bevy wraps a `Parallel<RenderMeshInstanceGpuQueue>` - one queue per thread, so
 *  its parallel extraction pushes without locking and drains them afterwards. Epix's
 *  extraction system iterates sequentially, so the resource holds a single queue:
 *  one slot instead of one per thread, with the entries in iteration order, which is
 *  what Bevy's per-thread concatenation gives it anyway. Bevy's `Deref`/`DerefMut`
 *  are the two operators here. */
EPIX_EXPORT struct RenderMeshInstanceGpuQueues {
    RenderMeshInstanceGpuQueue queue;

    RenderMeshInstanceGpuQueue& operator*() noexcept { return queue; }
    const RenderMeshInstanceGpuQueue& operator*() const noexcept { return queue; }
    RenderMeshInstanceGpuQueue* operator->() noexcept { return std::addressof(queue); }
    const RenderMeshInstanceGpuQueue* operator->() const noexcept { return std::addressof(queue); }
};

/** @brief Meshes that could not be extracted this frame because their materials
 *  were not prepared yet, to be retried next frame (Bevy
 *  `MeshesToReextractNextFrame`, `bevy_pbr/src/render/mesh.rs:830-835`).
 *
 *  Bevy derives `Resource`, `Default`, `Deref` and `DerefMut` over the set; here the
 *  set is reached through `operator*`/`operator->`, the shape the other resource
 *  wrappers in this module use. Without it a mesh whose material is not ready yet
 *  is dropped instead of retried. */
EPIX_EXPORT struct MeshesToReextractNextFrame {
    render::sync_world::MainEntityHashSet entities;

    render::sync_world::MainEntityHashSet& operator*() noexcept { return entities; }
    const render::sync_world::MainEntityHashSet& operator*() const noexcept { return entities; }
    render::sync_world::MainEntityHashSet* operator->() noexcept { return std::addressof(entities); }
    const render::sync_world::MainEntityHashSet* operator->() const noexcept { return std::addressof(entities); }
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

}  // namespace epix::pbr
