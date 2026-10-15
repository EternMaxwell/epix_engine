#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/camera/visibility.hpp>
#include <epix/common.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/lightmap.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/morph.hpp>
#include <epix/pbr/skin.hpp>
#include <epix/pbr/skin.hpp>
#include <epix/render/batching.hpp>
#include <epix/render/binned_phase.hpp>
#include <epix/render/globals.hpp>
#include <epix/render/mesh_allocator.hpp>
#include <glm/glm.hpp>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <epix/pbr/lightmap.hpp>
#include <epix/pbr/material_bindings.hpp>
#include <epix/pbr/fog.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/light_probe.hpp>
#include <epix/pbr/mesh_pipeline.hpp>
#include <epix/pbr/mesh_view_bindings.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/morph.hpp>
#include <epix/pbr/skin.hpp>
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
        NONE = 0,
        /** @brief The mesh casts shadows. */
        SHADOW_CASTER = 1u << 0,
        /** @brief The mesh may participate in automatic batching. */
        AUTOMATIC_BATCHING = 1u << 1,
        /** @brief The mesh had a transform last frame. */
        HAS_PREVIOUS_TRANSFORM = 1u << 2,
        /** @brief The mesh had a skin last frame. */
        HAS_PREVIOUS_SKIN = 1u << 3,
        /** @brief The mesh had morph targets last frame. */
        HAS_PREVIOUS_MORPH = 1u << 4,
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
/** @brief The set of entities to re-extract next frame (Bevy
 *  ``MeshesToReextractNextFrame``, `render/mesh.rs`).
 *
 *  Declared rather than included: it lives in ``mesh_extraction.hpp``, which
 *  includes this header, and the builder only needs it by reference. */
struct MeshesToReextractNextFrame;

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
            return MeshCullingData{
                .aabb_center       = glm::vec4(0.0f),
                .aabb_half_extents = glm::vec4(glm::vec3(std::numeric_limits<float>::infinity()), 0.0f)};
        }
        return MeshCullingData{.aabb_center       = glm::vec4(aabb->center, 0.0f),
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

/** @brief `MeshInputUniform` is the data buffer's element, and every byte of it is
 *  initialised when it is built, so the concept's escape hatch applies here too.
 *
 *  This is what lets `InstanceInputUniformBuffer<MeshInputUniform>` be
 *  instantiated at all - the buffer the GPU instance builder adds to and
 *  `remove_mesh_input_uniform` frees a slot in. */
template <>
struct RawBufferElementInfo<::epix::pbr::MeshInputUniform> {
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
EPIX_EXPORT struct EPIX_API RenderMeshInstanceGpuBuilder {
    RenderMeshInstanceShared shared;
    glm::mat4 world_from_local{1.0f};
    glm::uvec2 lightmap_uv_rect{0u, 0u};
    std::optional<std::uint32_t> previous_input_index;
    MeshFlags mesh_flags{};

    /** @brief Flush this instance to the `RenderMeshInstanceGpu` and
     *  `MeshInputUniform` tables, replacing what was there (Bevy
     *  `RenderMeshInstanceGpuBuilder::update`, `render/mesh.rs:1128-1262`).
     *
     *  The instance's vertex and index slices, skin offset, material binding and
     *  lightmap are all resolved here, because this is the one point where the
     *  render world knows the mesh is being drawn. A material that has not been
     *  prepared is not guessed at: the entity is queued for re-extraction next
     *  frame and nothing is written, which is Bevy's bail-out.
     *
     *  An entity that was already present keeps its uniform slot and hands the
     *  previous uniform to `previous_input_buffer`, which is what the
     *  preprocessing shader reads for motion vectors; a new one takes a slot. The
     *  returned index is the slot, so callers can key their own buffers by it. */
    std::optional<std::uint32_t> update(
        render::sync_world::MainEntity entity,
        render::sync_world::MainEntityHashMap<RenderMeshInstanceGpu>& render_mesh_instances,
        render::batching::InstanceInputUniformBuffer<MeshInputUniform>& current_input_buffer,
        render::batching::InstanceInputUniformBuffer<MeshInputUniform>& previous_input_buffer,
        const render::mesh::MeshAllocator& mesh_allocator, const RenderMaterialInstances& mesh_material_ids,
        const RenderMaterialBindings& render_material_bindings, const RenderLightmaps& render_lightmaps,
        const SkinUniforms& skin_uniforms, render::FrameCount timestamp,
        MeshesToReextractNextFrame& meshes_to_reextract_next_frame);
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
            const glm::vec3 center = glm::mat3(instance.transforms.world_from_local) * instance.shared.center +
                                     glm::vec3(instance.transforms.world_from_local[3]);
            return RenderMeshQueueData{.shared                = &instance.shared,
                                       .center                = center,
                                       .current_uniform_index = render::phase::InputUniformIndex{}};
        }
        const auto found = gpu_instances.find(entity);
        if (found == gpu_instances.end()) return std::nullopt;
        const RenderMeshInstanceGpu& instance = found->second;
        return RenderMeshQueueData{
            .shared                = &instance.shared,
            .center                = instance.center,
            .current_uniform_index = render::phase::InputUniformIndex{instance.current_uniform_index}};
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

/** @brief Drop a mesh's input uniform and forget its instance (Bevy
 *  `remove_mesh_input_uniform`, `render/mesh.rs:1267-1278`).
 *
 *  The three steps are one operation: the instance record is removed, the slot it
 *  occupied is released back to the input buffer, and the slot index is handed
 *  back so the caller can do the same for the buffers keyed by it. An entity with
 *  no instance is a no-op returning nothing, which is Bevy's `?` on the removal.
 *
 *  The buffer's `remove` only records the slot as free - the data stays until
 *  something reuses it - so this needs no device and can be checked directly. */
EPIX_EXPORT EPIX_API std::optional<std::uint32_t> remove_mesh_input_uniform(
    render::sync_world::MainEntity entity,
    render::sync_world::MainEntityHashMap<RenderMeshInstanceGpu>& render_mesh_instances,
    render::batching::InstanceInputUniformBuffer<MeshInputUniform>& current_input_buffer);

/** @brief Set the motion-vector flags on the meshes that had a skin or morph targets
 *  last frame (Bevy `set_mesh_motion_vector_flags`, `render/mesh.rs:1721-1734`).
 *
 *  This is the only writer of `HAS_PREVIOUS_SKIN`/`HAS_PREVIOUS_MORPH`. The flags
 *  cannot be set during extraction because the skin and morph tables are filled
 *  later, and reading those tables in the queuing loop would be too slow - which is
 *  why Bevy defers it to a `Render`-schedule system, as here. */
EPIX_EXPORT EPIX_API void set_mesh_motion_vector_flags(ecs::ResMut<RenderMeshInstances> render_mesh_instances,
                                                       ecs::Res<SkinUniforms> skin_uniforms,
                                                       ecs::Res<MorphIndices> morph_indices);

/** @brief Compile each visible mesh's material pipeline for each view that draws it, and
 *  cache it per view and entity (Bevy `specialize_material_meshes`, `material.rs:996-1148`).
 *
 *  It lives here rather than in `material.hpp` because it reads `RenderMeshInstances`, which
 *  this header owns, and `material.hpp` cannot include this one without closing the cycle
 *  through `MeshPipeline` - the same reason `DrawMesh` is declared here.
 *
 *  Bevy registers it in `RenderSystems::PrepareMeshes`, after the mesh assets are prepared,
 *  the GPU-built meshes are collected and the motion-vector flags are set, because it reads
 *  all three of those results. The key it builds is Bevy's: the view's mesh key, the mesh's
 *  own bits, the material's precalculated bits with its alpha mode resolved against the view's
 *  sample count, and the lightmap, visibility-range and previous-frame skin and morph bits -
 *  plus the material's *erased bind-group data*, which is what keeps two materials of one type
 *  with different data from sharing a compiled pipeline. */
EPIX_EXPORT EPIX_API void specialize_material_meshes(
    ecs::Res<render::RenderAssets<render::mesh::RenderMesh>> render_meshes,
    ecs::Res<render::erased_render_asset::ErasedRenderAssets<PreparedMaterial>> render_materials,
    ecs::Res<RenderMeshInstances> render_mesh_instances,
    ecs::Res<RenderMaterialInstances> render_material_instances,
    ecs::Res<RenderLightmaps> render_lightmaps,
    ecs::Res<render::view::RenderVisibilityRanges> render_visibility_ranges,
    ecs::Res<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::Opaque3d>> opaque_render_phases,
    ecs::Res<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::AlphaMask3d>> alpha_mask_render_phases,
    ecs::Res<render::phase::ViewSortedRenderPhases<core_graph::core_3d::Transmissive3d>> transmissive_render_phases,
    ecs::Res<render::phase::ViewSortedRenderPhases<core_graph::core_3d::Transparent3d>> transparent_render_phases,
    ecs::Query<ecs::Item<const render::sync_world::MainEntity&, const render::view::ExtractedView&,
                         const render::view::RenderVisibleEntities&>>
        views,
    ecs::Res<ViewKeyCache> view_key_cache, ecs::Res<EntitySpecializationTicks> entity_specialization_ticks,
    ecs::Res<ViewSpecializationTicks> view_specialization_ticks,
    ecs::ResMut<SpecializedMaterialPipelineCache> specialized_material_pipeline_cache,
    ecs::ResMut<render::SpecializedMeshPipelines<MaterialPipelineSpecializer>> pipelines,
    ecs::Res<MaterialPipeline> pipeline, ecs::Res<render::PipelineServer> pipeline_server,
    ecs::SystemChangeTick ticks);

/** @brief Queue each visible mesh into the phase its material belongs to (Bevy
 *  `queue_material_meshes`, `material.rs:1152-1350`).
 *
 *  Bevy registers it in `RenderSystems::QueueMeshes`, after `specialize_material_meshes` has
 *  filled the per-view pipeline cache it reads. The four arms are the four phase kinds: the
 *  sorted ones take a view-space distance plus the material's depth bias, the binned ones a
 *  batch-set key holding the pipeline, the draw function, the material's bind group and the
 *  mesh's slabs. The opaque arm additionally honours the deferred renderer by *updating the
 *  cache without queuing*, so the entity is not re-examined every frame while it is drawn by
 *  the deferred pass. */
EPIX_EXPORT EPIX_API void queue_material_meshes(
    ecs::Res<render::erased_render_asset::ErasedRenderAssets<PreparedMaterial>> render_materials,
    ecs::Res<RenderMeshInstances> render_mesh_instances,
    ecs::Res<RenderMaterialInstances> render_material_instances, ecs::Res<render::mesh::MeshAllocator> mesh_allocator,
    ecs::Res<render::batching::GpuPreprocessingSupport> gpu_preprocessing_support,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::Opaque3d>> opaque_render_phases,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::AlphaMask3d>> alpha_mask_render_phases,
    ecs::ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_3d::Transmissive3d>> transmissive_render_phases,
    ecs::ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_3d::Transparent3d>> transparent_render_phases,
    ecs::Query<ecs::Item<const render::sync_world::MainEntity&, const render::view::ExtractedView&,
                         const render::view::RenderVisibleEntities&>>
        views,
    ecs::Res<SpecializedMaterialPipelineCache> specialized_material_pipeline_cache);
/** @brief Bind one mesh's bind group at slot `I`. The tag Bevy spells
 *  `SetMeshBindGroup<const I: usize>` (`render/mesh.rs:3080`); the command is the
 *  `RenderCommand` specialisation below, in `epix::render::phase` where the trait lives. */
EPIX_EXPORT template <std::size_t I>
struct SetMeshBindGroup {};

/** @brief Bind the view's mesh bind group at slot `I` (Bevy `SetMeshViewBindGroup`,
 *  `render/mesh.rs:2991`), with the offsets its dynamic bindings need. */
EPIX_EXPORT template <std::size_t I>
struct SetMeshViewBindGroup {};

/** @brief Bind the view's *binding-array* mesh group at slot `I` (Bevy
 *  `SetMeshViewBindingArrayBindGroup`, `render/mesh.rs:3040`), which takes no offsets: the
 *  phase chooses this command instead of working out dynamic offsets. */
EPIX_EXPORT template <std::size_t I>
struct SetMeshViewBindingArrayBindGroup {};

/** @brief Bind the view's *empty* mesh group at slot `I` (Bevy `SetMeshViewEmptyBindGroup`,
 *  `render/mesh.rs:3060`), for phases that need a group bound but have no mesh view state. */
EPIX_EXPORT template <std::size_t I>
struct SetMeshViewEmptyBindGroup {};

/** @brief Bind the prepass pass's view group at slot `I` (Bevy `SetPrepassViewBindGroup`,
 *  `prepass/mod.rs:1215`), which is the motion-vector variant when the view writes them. */
EPIX_EXPORT template <std::size_t I>
struct SetPrepassViewBindGroup {};

/** @brief Bind the prepass pass's empty group at slot `I` (Bevy
 *  `SetPrepassViewEmptyBindGroup`, `prepass/mod.rs:1262`), which stands in for the mesh view
 *  group the forward pass binds in that slot. */
EPIX_EXPORT template <std::size_t I>
struct SetPrepassViewEmptyBindGroup {};

/** @brief Bevy's `DrawMaterial` command list (`material.rs:512-519`): the draw sequence every
 *  3D phase of every material registers - `SetItemPipeline`, the view's mesh and binding-array
 *  groups, the mesh's group, the material's own group and `DrawMesh`.
 *
 *  Bevy's version is a plain tuple and so needs no phase parameter; Epix's
 *  `RenderCommandSequence` carries its phase, so the phase is the argument here. The sequence
 *  is *not* parameterised by material type, because none of its commands is: the material's
 *  group is reached through the untyped allocator registry. */
template <typename P>
using DrawMaterial = render::phase::RenderCommandSequence<P,
                                                         render::phase::SetItemPipeline,
                                                         SetMeshViewBindGroup<0>,
                                                         SetMeshViewBindingArrayBindGroup<1>,
                                                         SetMeshBindGroup<2>,
                                                         SetMaterialBindGroup<MATERIAL_BIND_GROUP_INDEX>,
                                                         DrawMesh>;

/** @brief Bevy's `DrawPrepass` command list (`prepass/mod.rs:1282-1289`): the prepass pass's
 *  own view group, the empty group where the forward pass binds the mesh view group, the
 *  mesh's group, the material's own group and the mesh.
 *
 *  Bevy's version is a plain tuple; Epix's `RenderCommandSequence` carries its phase, so the
 *  phase is the argument, as for `DrawMaterial`. */
template <typename P>
using DrawPrepass = render::phase::RenderCommandSequence<P,
                                                         render::phase::SetItemPipeline,
                                                         SetPrepassViewBindGroup<0>,
                                                         SetPrepassViewEmptyBindGroup<1>,
                                                         SetMeshBindGroup<2>,
                                                         SetMaterialBindGroup<MATERIAL_BIND_GROUP_INDEX>,
                                                         DrawMesh>;

}  // namespace epix::pbr

namespace epix::render::phase {

/** @brief Draw one mesh (Bevy `DrawMesh`, `render/mesh.rs:3204-3410`).
 *
 *  It lives here rather than beside the `DrawMesh` tag because it reads
 *  `RenderMeshInstances`, which this header owns; `mesh_pipeline.hpp` cannot include this one
 *  without closing an include cycle through `material.hpp`'s `MeshPipeline`.
 *
 *  What is ported is the direct half, which is the same shape as the 2D command: the vertex
 *  buffer, then an indexed or non-indexed draw over the item's batch range. **The indirect
 *  half is not portable and is deliberately absent.** Bevy draws GPU-preprocessed meshes with
 *  `multi_draw_indexed_indirect[_count]`, and the wgpu revision this engine builds against -
 *  the fetched package's `include/webgpu/webgpu.h` under the build directory - declares no
 *  multi-draw entry point at all: a search for `multiDraw` in it finds nothing. So an item
 *  whose extra index asks for an indirect draw is skipped with an explanation rather than
 *  drawn wrongly. Bevy's preprocess guard is absent for a related reason: it asks whether
 *  `PreprocessPipelines` have loaded, and this port has no such pipelines, so the condition it
 *  tests cannot arise yet. */
EPIX_EXPORT template <PhaseItem P>
struct RenderCommand<::epix::pbr::DrawMesh, P> {
    using Param = std::tuple<epix::ecs::Res<::epix::render::RenderAssets<::epix::render::mesh::RenderMesh>>,
                             epix::ecs::Res<::epix::pbr::RenderMeshInstances>,
                             epix::ecs::Res<::epix::render::mesh::MeshAllocator>>;
    using ViewQuery = std::tuple<>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P& item,
                                                   std::tuple<>,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param params,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        auto&& [render_meshes, instances, mesh_allocator] = params;

        const auto mesh_asset_id = instances->mesh_asset_id(item.main_entity());
        if (!mesh_asset_id.has_value()) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Skip,
                .message = std::format("[mesh] Main entity {:#x} has no RenderMeshInstance during draw.",
                                       item.main_entity().id().index),
            });
        }
        auto* render_mesh = render_meshes->try_get(*mesh_asset_id);
        if (render_mesh == nullptr) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Skip,
                .message = std::format("[mesh] GPU mesh {} is missing from RenderAssets<Mesh> at draw time.",
                                       mesh_asset_id->to_string_short()),
            });
        }
        const auto vertex_slice = mesh_allocator->mesh_vertex_slice(*mesh_asset_id);
        if (!vertex_slice.has_value()) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Skip,
                .message = std::format("[mesh] Mesh {} has no MeshAllocator vertex slice at draw time.",
                                       mesh_asset_id->to_string_short()),
            });
        }

        const auto& layout      = render_mesh->layout.value->layout();
        const auto stride       = static_cast<std::uint64_t>(layout.array_stride);
        const auto vertex_first = static_cast<std::uint64_t>(vertex_slice->range.first);
        const auto vertex_count = static_cast<std::uint64_t>(vertex_slice->range.second - vertex_first);
        encoder.setVertexBuffer(0, *vertex_slice->buffer, vertex_first * stride, vertex_count * stride);

        const auto& batch_range = item.batch_range();
        const auto batch_size   = batch_range_len(batch_range);

        // The indirect half: see this command's comment. Skipping is the honest answer; a
        // direct draw here would draw the wrong instances.
        if (item.extra_index().type == PhaseItemExtraIndex::Type::IndirectParametersIndex) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Skip,
                .message = std::format("[mesh] Mesh {} needs an indirect draw, and this engine's wgpu has no "
                                       "multi-draw entry point.",
                                       mesh_asset_id->to_string_short()),
            });
        }

        if (render_mesh->indexed()) {
            const auto index_slice = mesh_allocator->mesh_index_slice(*mesh_asset_id);
            if (!index_slice.has_value()) {
                return std::unexpected(RenderCommandError{
                    .type    = RenderCommandError::Type::Skip,
                    .message = std::format("[mesh] Indexed mesh {} has no MeshAllocator index slice.",
                                           mesh_asset_id->to_string_short()),
                });
            }
            const auto* info                 = render_mesh->buffer_info.indexed_info();
            const wgpu::IndexFormat format   = info != nullptr ? info->index_format : wgpu::IndexFormat::eUint16;
            const std::uint64_t element_size = format == wgpu::IndexFormat::eUint16 ? 2 : 4;
            const auto index_first           = static_cast<std::uint64_t>(index_slice->range.first);
            const auto index_count           = static_cast<std::uint64_t>(index_slice->range.second - index_first);
            encoder.setIndexBuffer(*index_slice->buffer, format, index_first * element_size,
                                   index_count * element_size);
            encoder.drawIndexed(static_cast<std::uint32_t>(index_count), batch_size, 0, 0, batch_range.first);
        } else {
            encoder.draw(static_cast<std::uint32_t>(vertex_count), batch_size, 0, batch_range.first);
        }
        return {};
    }
};

/** @brief Bind one mesh's bind group at slot `I` (Bevy `SetMeshBindGroup<I>`,
 *  `render/mesh.rs:3080-3165`).
 *
 *  Three of Bevy's four outcomes are not failures. A mesh that is not in
 *  `RenderMeshInstances`, and a `MeshBindGroups` set with nothing for this phase, both report
 *  *success*: they are ordinary - a shadow phase with no shadows has nothing to bind. Only a
 *  phase that is present in `MeshBindGroups` but has no group for this mesh fails, because
 *  that means the preparation did not run for a phase that needs it.
 *
 *  Which set to read follows the same split as the resource itself: one set for the whole
 *  scene on the CPU path, one per phase on the GPU path, keyed by the phase's type. The
 *  `Has<MotionVectorPrepass>` view query yields the `bool` that picks the motion-vector
 *  variant, which is Bevy's own `render` parameter of that name. */
EPIX_EXPORT template <std::size_t I, PhaseItem P>
struct RenderCommand<::epix::pbr::SetMeshBindGroup<I>, P> {
    using Param = std::tuple<epix::ecs::Res<::epix::pbr::MeshBindGroups>,
                             epix::ecs::Res<::epix::pbr::RenderMeshInstances>,
                             epix::ecs::Res<::epix::pbr::SkinUniforms>,
                             epix::ecs::Res<::epix::pbr::MorphIndices>,
                             epix::ecs::Res<::epix::pbr::RenderLightmaps>>;
    using ViewQuery = epix::ecs::Has<::epix::core_graph::core_3d::MotionVectorPrepass>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P& item,
                                                   bool has_motion_vector_prepass,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param params,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        auto&& [bind_groups, instances, skin_uniforms, morph_indices, lightmaps] = params;

        const auto entity        = item.main_entity();
        const auto mesh_asset_id = instances->mesh_asset_id(entity);
        if (!mesh_asset_id.has_value()) return {};

        const bool is_skinned = skin_uniforms->skin_byte_offset(entity).has_value();
        const bool is_morphed = morph_indices->current.find(entity) != morph_indices->current.end();

        std::optional<::epix::pbr::LightmapSlabIndex> lightmap_slab;
        if (const auto found = lightmaps->render_lightmaps.find(entity);
            found != lightmaps->render_lightmaps.end()) {
            lightmap_slab = found->second.slab_index;
        }

        const ::epix::pbr::MeshPhaseBindGroups* phase_groups = nullptr;
        if (const auto* cpu = std::get_if<::epix::pbr::MeshPhaseBindGroups>(&*bind_groups)) {
            phase_groups = cpu;
        } else if (const auto* per_phase =
                       std::get_if<std::unordered_map<std::type_index, ::epix::pbr::MeshPhaseBindGroups>>(
                           &*bind_groups)) {
            if (const auto found = per_phase->find(std::type_index(typeid(P))); found != per_phase->end()) {
                phase_groups = &found->second;
            }
        }
        if (phase_groups == nullptr) return {};

        const wgpu::BindGroup* bind_group = phase_groups->get(*mesh_asset_id, lightmap_slab, is_skinned, is_morphed,
                                                             has_motion_vector_prepass);
        if (bind_group == nullptr) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Failure,
                .message = "The MeshBindGroups resource is present for this phase but holds no group for this "
                           "mesh, so mesh bind-group preparation did not run for it.",
            });
        }

        encoder.setBindGroup(I, *bind_group, std::span<const std::uint32_t>{});
        return {};
    }
};

/** @brief Bind the view's mesh bind group at slot `I` (Bevy `SetMeshViewBindGroup<I>`,
 *  `render/mesh.rs:2991-3038`).
 *
 *  The offsets are the view's uniform offsets, in the order the group's dynamic bindings
 *  declare them. Bevy pushes six and optionally a seventh for order-independent transparency;
 *  this port pushes the four it has, because screen-space reflections and order-independent
 *  transparency are features it does not implement and its group's layout therefore does not
 *  declare those bindings. Pushing Bevy's six would bind offsets the layout never asked for. */
EPIX_EXPORT template <std::size_t I, PhaseItem P>
struct RenderCommand<::epix::pbr::SetMeshViewBindGroup<I>, P> {
    using Param     = std::tuple<>;
    using ViewQuery = epix::ecs::Item<const ::epix::render::view::ViewUniformOffset&,
                                      const ::epix::pbr::ViewLightsUniformOffset&,
                                      const ::epix::pbr::ViewFogUniformOffset&,
                                      const ::epix::pbr::ViewLightProbesUniformOffset&,
                                      const ::epix::pbr::ViewEnvironmentMapUniformOffset&,
                                      const ::epix::pbr::MeshViewBindGroup&>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P&,
                                                   ViewQuery view,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        const std::array<std::uint32_t, 4> offsets{
            std::get<0>(view).offset,
            std::get<1>(view).offset,
            std::get<2>(view).offset,
            std::get<3>(view).offset,
        };
        encoder.setBindGroup(I, std::get<5>(view).main, offsets);
        return {};
    }
};

/** @brief Bind the view's binding-array mesh group at slot `I` (Bevy
 *  `SetMeshViewBindingArrayBindGroup<I>`, `render/mesh.rs:3040-3058`). */
EPIX_EXPORT template <std::size_t I, PhaseItem P>
struct RenderCommand<::epix::pbr::SetMeshViewBindingArrayBindGroup<I>, P> {
    using Param     = std::tuple<>;
    using ViewQuery = epix::ecs::Item<const ::epix::pbr::MeshViewBindGroup&>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P&,
                                                   ViewQuery view,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        encoder.setBindGroup(I, std::get<0>(view).binding_array, std::span<const std::uint32_t>{});
        return {};
    }
};

/** @brief Bind the view's empty mesh group at slot `I` (Bevy
 *  `SetMeshViewEmptyBindGroup<I>`, `render/mesh.rs:3060-3078`). */
EPIX_EXPORT template <std::size_t I, PhaseItem P>
struct RenderCommand<::epix::pbr::SetMeshViewEmptyBindGroup<I>, P> {
    using Param     = std::tuple<>;
    using ViewQuery = epix::ecs::Item<const ::epix::pbr::MeshViewBindGroup&>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P&,
                                                   ViewQuery view,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        encoder.setBindGroup(I, std::get<0>(view).empty, std::span<const std::uint32_t>{});
        return {};
    }
};

/** @brief Bind the prepass pass's view group at slot `I` (Bevy `SetPrepassViewBindGroup<I>`,
 *  `prepass/mod.rs:1216-1260`).
 *
 *  Bevy's view query is `(Read<ViewUniformOffset>, Has<MotionVectorPrepass>,
 *  Option<Read<PreviousViewUniformOffset>>)`, and that pair decides which of the resource's
 *  two groups is bound and with which offsets. The two `Failure` arms stand in for the
 *  `unwrap`s Bevy does there: a missing group means preparation did not run for a view that
 *  needs it, which is a defect rather than a phase with nothing to draw. */
EPIX_EXPORT template <std::size_t I, PhaseItem P>
struct RenderCommand<::epix::pbr::SetPrepassViewBindGroup<I>, P> {
    using Param     = epix::ecs::Res<::epix::pbr::PrepassViewBindGroup>;
    using ViewQuery = epix::ecs::Item<const ::epix::render::view::ViewUniformOffset&,
                                      epix::ecs::Has<::epix::core_graph::core_3d::MotionVectorPrepass>,
                                      epix::ecs::Opt<const ::epix::core_graph::PreviousViewUniformOffset&>>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P&,
                                                   ViewQuery view,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param prepass_view_bind_group,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        const auto& previous_view_uniform_offset = std::get<2>(view);
        if (std::get<1>(view) && previous_view_uniform_offset.has_value()) {
            if (!prepass_view_bind_group->motion_vectors.has_value()) {
                return std::unexpected(RenderCommandError{
                    .type    = RenderCommandError::Type::Failure,
                    .message = "The prepass view's motion-vector bind group is missing, so "
                               "prepare_prepass_view_bind_group did not bind the previous view uniforms.",
                });
            }
            const std::array<std::uint32_t, 2> offsets{std::get<0>(view).offset,
                                                       previous_view_uniform_offset->get().offset};
            encoder.setBindGroup(I, *prepass_view_bind_group->motion_vectors, offsets);
            return {};
        }
        if (!prepass_view_bind_group->no_motion_vectors.has_value()) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Failure,
                .message = "The prepass view's bind group is missing, so "
                           "prepare_prepass_view_bind_group did not run for this view.",
            });
        }
        const std::array<std::uint32_t, 1> offsets{std::get<0>(view).offset};
        encoder.setBindGroup(I, *prepass_view_bind_group->no_motion_vectors, offsets);
        return {};
    }
};

/** @brief Bind the prepass pass's empty group at slot `I` (Bevy
 *  `SetPrepassViewEmptyBindGroup<I>`, `prepass/mod.rs:1263-1280`), which takes no offsets. */
EPIX_EXPORT template <std::size_t I, PhaseItem P>
struct RenderCommand<::epix::pbr::SetPrepassViewEmptyBindGroup<I>, P> {
    using Param     = epix::ecs::Res<::epix::pbr::PrepassViewBindGroup>;
    using ViewQuery = epix::ecs::Item<>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P&,
                                                   epix::ecs::Item<>,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param prepass_view_bind_group,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        encoder.setBindGroup(I, prepass_view_bind_group->empty_bind_group, std::span<const std::uint32_t>{});
        return {};
    }
};

}  // namespace epix::render::phase
