#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/assets/asset_changed.hpp>
#include <epix/assets/store.hpp>
#include <epix/camera/visibility.hpp>
#include <epix/common.hpp>
#include <epix/mesh/skinning.hpp>
#include <epix/render/mesh/offset_allocator.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/sync_world.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::pbr {

/** @brief The total number of joints a skinned mesh can have (Bevy `MAX_JOINTS`,
 *  `bevy_pbr/src/render/skin.rs:31`).
 *
 *  The rest of `skin.rs` - `SkinUniforms`, `SkinIndex`, `prepare_skins`,
 *  `extract_skins` - lands with the skinning port; this is what the mesh bind
 *  group layouts need from it. */
EPIX_EXPORT inline constexpr std::size_t MAX_JOINTS = 256;

/** @brief The total number of joints supported across the scene (Bevy
 *  `MAX_TOTAL_JOINTS`, `render/skin.rs:38`): the capacity the joint buffer is
 *  sized for. */
EPIX_EXPORT inline constexpr std::uint32_t MAX_TOTAL_JOINTS = 1024u * 1024u * 1024u;

/** @brief How many joints one allocation unit holds (Bevy
 *  `JOINTS_PER_ALLOCATION_UNIT`, `render/skin.rs:43`).
 *
 *  Some hardware requires uniform allocations on 256-byte boundaries, so a unit
 *  is four 64-byte matrices. */
EPIX_EXPORT inline constexpr std::uint32_t JOINTS_PER_ALLOCATION_UNIT =
    static_cast<std::uint32_t>(256 / sizeof(glm::mat4));

/** @brief The total number of joints a skinned mesh can have (Bevy `MAX_JOINTS`,
 *  `bevy_pbr/src/render/skin.rs:31`).
 *
 *  The rest of `skin.rs` - `SkinUniforms`, `SkinIndex`, `prepare_skins`,
 *  `extract_skins` - lands with the skinning port; this is what the mesh bind
 *  group layouts need from it. */
EPIX_EXPORT inline constexpr std::size_t MAX_JOINTS_LEGACY = 256;

/** @brief Whether skinning has to use uniform buffers with dynamic offsets
 *  (Bevy `skins_use_uniform_buffers`, `render/skin.rs:194`).
 *
 *  Storage buffers are the fast path; a device without them falls back to
 *  uniforms, which is why the mesh layouts' joint entry and the bind group's
 *  buffer size both consult this. Bevy memoises the answer in a `OnceLock`, so
 *  the first device decides for the process. */
EPIX_EXPORT EPIX_API bool skins_use_uniform_buffers(const wgpu::Limits& limits);

/** @brief The location of the first joint matrix in the skin uniform buffer
 *  (Bevy `SkinByteOffset`, `render/skin.rs:55-76`). */
EPIX_EXPORT struct SkinByteOffset {
    std::uint32_t byte_offset = 0;

    /** @brief The address-space location of the joint matrix at `index` (Bevy
     *  `SkinByteOffset::from_index`, `skin.rs:64-68`). */
    static SkinByteOffset from_index(std::size_t index) {
        return SkinByteOffset{.byte_offset = static_cast<std::uint32_t>(index * sizeof(glm::mat4))};
    }

    /** @brief This offset in elements, each a 4x4 matrix (Bevy
     *  `SkinByteOffset::index`, `skin.rs:73-75`). */
    std::uint32_t index() const { return byte_offset / static_cast<std::uint32_t>(sizeof(glm::mat4)); }
};

/** @brief Allocation information about one skin (Bevy's private
 *  `SkinUniformInfo`, `render/skin.rs:177-190`). A struct's members are public
 *  in the port. */
EPIX_EXPORT struct SkinUniformInfo {
    /** @brief The joints' allocation within the current buffer. */
    render::mesh::offset_allocator::Allocation allocation;
    /** @brief The entities that make up the joints. */
    std::vector<render::sync_world::MainEntity> joints;

    /** @brief The offset in joints within the current staging buffer (Bevy
     *  `SkinUniformInfo::offset`, `skin.rs:185-189`). */
    std::uint32_t offset() const { return allocation.offset * JOINTS_PER_ALLOCATION_UNIT; }
};

/** @brief The GPU buffers holding joint matrices for all skinned meshes (Bevy
 *  `SkinUniforms`, `render/skin.rs:78-117,156-175`).
 *
 *  Double-buffered: each mesh's previous-frame joints are kept alongside the
 *  current frame's for motion vectors, and the buffers are swapped every frame.
 *  Bevy's `FromWorld` is `make`, which takes the device facts as parameters; the
 *  systems that fill it - `prepare_skins`, `extract_skins` and their helpers -
 *  are not ported yet, so this is the allocation bookkeeping and the accessors
 *  the batching adapter and the mesh bind groups read. Bevy's `SmallVec` of the
 *  one-skin-common case is a plain vector here. */
EPIX_EXPORT struct EPIX_API SkinUniforms {
    /** @brief The joint matrices for this frame, on the CPU. */
    std::vector<glm::mat4> current_staging_buffer;
    /** @brief The joint matrices for this frame, on the GPU. */
    wgpu::Buffer current_buffer;
    /** @brief The joint matrices for the previous frame, on the GPU. */
    wgpu::Buffer prev_buffer;
    /** @brief Places joints within `current_buffer`. */
    render::mesh::offset_allocator::Allocator allocator;
    /** @brief What is known about each skin. Bevy's field is private there. */
    render::sync_world::MainEntityHashMap<SkinUniformInfo> skin_uniform_info;
    /** @brief Each joint entity mapped to the skins it belongs to; conceptually
     *  a map of sets, used with change detection to update only what moved.
     *  Bevy's field is private there. */
    render::sync_world::MainEntityHashMap<std::vector<render::sync_world::MainEntity>> joint_to_skins;
    /** @brief The total number of joints in the scene, part of the heuristic
     *  that decides whether fine-grained change detection is worth it. */
    std::size_t total_joints = 0;

    /** @brief Build the joint buffers for a device (Bevy `impl FromWorld for
     *  SkinUniforms`, `render/skin.rs:119-154`), which Bevy also uses to decide
     *  the buffers' usage from `skins_use_uniform_buffers`. */
    static SkinUniforms make(const wgpu::Device& render_device, const wgpu::Limits& limits);

    /** @brief The current offset in joints of a skin (Bevy
     *  `SkinUniforms::skin_index`, `skin.rs:158-162`). */
    std::optional<std::uint32_t> skin_index(render::sync_world::MainEntity skin) const;

    /** @brief The current offset in bytes of a skin (Bevy
     *  `SkinUniforms::skin_byte_offset`, `skin.rs:165-169`). */
    std::optional<SkinByteOffset> skin_byte_offset(render::sync_world::MainEntity skin) const;

    /** @brief Every skin in the scene (Bevy `SkinUniforms::all_skins`,
     *  `skin.rs:172-174`). */
    std::vector<render::sync_world::MainEntity> all_skins() const;
};

/** @brief Upload this frame's joint matrices to the GPU (Bevy `prepare_skins`,
 *  `render/skin.rs:199-268`).
 *
 *  Three things happen in order, and each matters: the current and previous
 *  buffers are swapped, so what the motion-vector prepass reads as "previous"
 *  really is last frame's; the buffers grow by a factor of 1.5 (rounded to a
 *  multiple of four) when the staging data no longer fits, with `MAX_JOINTS` of
 *  extra room because a full uniform buffer's worth must stay bindable where
 *  skins use uniform buffers; and when they grow, the fresh previous buffer is
 *  filled from the *current* staging data, which is Bevy's documented one-frame
 *  approximation - a new previous buffer has no history to hold. */
EPIX_EXPORT EPIX_API void prepare_skins(ecs::Res<wgpu::Device> render_device, ecs::Res<wgpu::Queue> render_queue,
                                        ecs::ResMut<SkinUniforms> uniform);

/** @brief Release a skin's allocation and forget it (Bevy `remove_skin`,
 *  `render/skin.rs:583-607`).
 *
 *  Bevy keeps this private to `skin.rs`; it is a function here for the same
 *  reason `prepare_view_cluster_bindings` is - the behaviour can then be checked
 *  without a device, and the systems that call it are thin. Three things happen
 *  together and each has to: the allocation is freed, the reverse mapping from
 *  each of the skin's joints is pruned (and the entry dropped when it empties,
 *  which is what keeps `total_joints`'s fine-grained heuristic honest), and
 *  `total_joints` falls by the skin's joint count. A skin that was never
 *  registered is a no-op. */
EPIX_EXPORT EPIX_API void remove_skin(SkinUniforms& skin_uniforms, render::sync_world::MainEntity skinned_mesh_entity);

/** @brief The term that selects the skins whose data changed this frame (Bevy
 *  `Or<(Changed<ViewVisibility>, Changed<SkinnedMesh>, AssetChanged<SkinnedMesh>)>`,
 *  `render/skin.rs:302-308`). */
using SkinnedMeshChangedFilter = ecs::Or<ecs::Modified<camera::ViewVisibility>,
                                         ecs::Modified<mesh::SkinnedMesh>,
                                         assets::AssetChanged<mesh::SkinnedMesh>>;

/** @brief The fraction of the scene's joints past which change detection is
 *  skipped and every joint is re-extracted (Bevy
 *  `JOINT_EXTRACTION_THRESHOLD_FACTOR`, `render/skin.rs:24`).
 *
 *  A quarter: past that, the bookkeeping for fine-grained detection costs more
 *  than re-extracting the transforms. The comparison is against the number of
 *  *changed entities*, not changed joints, which Bevy notes is a deliberate
 *  approximation. */
EPIX_EXPORT inline constexpr double JOINT_EXTRACTION_THRESHOLD_FACTOR = 0.25;

/** @brief One changed skin: its visibility decides whether it stays allocated
 *  (Bevy's `(Entity, &ViewVisibility, &SkinnedMesh)`). */
using ChangedSkinnedMeshItem = ecs::Item<ecs::Entity, const camera::ViewVisibility&, const mesh::SkinnedMesh&>;

/** @brief One skinned mesh and the entity it belongs to (Bevy's
 *  `(Entity, &SkinnedMesh)`). */
using SkinnedMeshItem = ecs::Item<ecs::Entity, const mesh::SkinnedMesh&>;

/** @brief The joint transforms every skin reads (Bevy's
 *  `Query<&GlobalTransform>`). */
using JointTransformQuery = ecs::Query<ecs::Item<const transform::GlobalTransform&>>;

/** @brief The entities whose transform moved this frame (Bevy's
 *  `Query<(Entity, &GlobalTransform), Changed<GlobalTransform>>`). */
using ChangedTransformItem = ecs::Item<ecs::Entity, const transform::GlobalTransform&>;

/** @brief The term that selects the moved transforms (Bevy's
 *  `Changed<GlobalTransform>`). */
using ChangedTransformFilter = ecs::Modified<transform::GlobalTransform>;

/** @brief Allocate space for a skin and fill in its joints (Bevy `add_skin`,
 *  `render/skin.rs:507-581`).
 *
 *  Space is allocated in units of `JOINTS_PER_ALLOCATION_UNIT`, the joints'
 *  matrices are written into the staging buffer (grown with identity matrices
 *  where the allocator placed them past the end), the reverse mapping from each
 *  joint to this skin is recorded for the fine-grained extraction, and
 *  `total_joints` rises by the skin's joint count. A skin whose joints cannot be
 *  allocated is reported and skipped, as Bevy does.
 *
 *  A missing inverse-bind-pose asset, or a joint with no transform, contributes
 *  an identity matrix rather than failing: Bevy's `match` falls through to
 *  `Mat4::IDENTITY` for exactly that case. */
EPIX_EXPORT EPIX_API void add_skin(render::sync_world::MainEntity skinned_mesh_entity,
                                   const mesh::SkinnedMesh& skinned_mesh, SkinUniforms& skin_uniforms,
                                   const assets::Assets<mesh::SkinnedMeshInverseBindposes>& inverse_bindposes,
                                   app::Extract<JointTransformQuery>& joints);

/** @brief Recompute one skin's joints into the staging buffer (Bevy
 *  `extract_joints_for_skin`, `render/skin.rs:457-505`).
 *
 *  A skin that was just added is skipped - `add_skin` already wrote all of its
 *  joints - and so is one with no allocation or no inverse bind poses. Each
 *  joint's matrix is its transform times its inverse bind pose, paired by index.
 *
 *  The queries are taken as the extraction wrapper because that is what the
 *  extract system holds: Epix's `Extract` forwards `get`/`iter` to the query it
 *  wraps but is not convertible to it, so a helper called from extraction takes
 *  the wrapper, where Bevy's takes `&Query`. */
EPIX_EXPORT EPIX_API void
extract_joints_for_skin(render::sync_world::MainEntity skin_entity, const mesh::SkinnedMesh& skin,
                        SkinUniforms& skin_uniforms,
                        app::Extract<ecs::Query<ChangedSkinnedMeshItem, SkinnedMeshChangedFilter>>& changed_skinned_meshes,
                        const assets::Assets<mesh::SkinnedMeshInverseBindposes>& inverse_bindposes,
                        app::Extract<JointTransformQuery>& joints);

/** @brief Recompute the joints of every skin the changed transforms reach (Bevy
 *  `extract_joints`, `render/skin.rs:389-455`).
 *
 *  The heuristic is the point of the function: once the number of *entities* whose
 *  transform changed exceeds a quarter of the scene's joints, fine-grained
 *  detection is not worth its own cost, so every skin is re-extracted; otherwise
 *  only the skins that `joint_to_skins` maps the changed joints to. Bevy counts
 *  changed entities rather than joints and says so - the approximation is
 *  deliberate. */
EPIX_EXPORT EPIX_API void extract_joints(
    SkinUniforms& skin_uniforms, app::Extract<ecs::Query<SkinnedMeshItem>>& skinned_meshes,
    app::Extract<ecs::Query<ChangedSkinnedMeshItem, SkinnedMeshChangedFilter>>& changed_skinned_meshes,
    const assets::Assets<mesh::SkinnedMeshInverseBindposes>& inverse_bindposes,
    app::Extract<ecs::Query<ChangedTransformItem, ChangedTransformFilter>>& changed_transforms,
    app::Extract<JointTransformQuery>& joints);

/** @brief Re-allocate the skins whose visibility or data changed (Bevy
 *  `add_or_delete_skins`, `render/skin.rs:349-385`).
 *
 *  Every changed skin is removed first and re-added only if it is still visible,
 *  so a skin never keeps an allocation it no longer matches. */
EPIX_EXPORT EPIX_API void add_or_delete_skins(
    SkinUniforms& skin_uniforms,
    app::Extract<ecs::Query<ChangedSkinnedMeshItem, SkinnedMeshChangedFilter>>& changed_skinned_meshes,
    const assets::Assets<mesh::SkinnedMeshInverseBindposes>& inverse_bindposes, app::Extract<JointTransformQuery>& joints);

/** @brief Fill the joint buffers for every skinned mesh this frame (Bevy
 *  `extract_skins`, `render/skin.rs:296-345`).
 *
 *  The three steps are Bevy's and happen in this order: skins that appeared,
 *  disappeared or changed are allocated or released; the joints of the skins the
 *  changed transforms reach are re-extracted; and the skins whose component was
 *  removed are released - but only if `add_or_delete_skins` did not already see
 *  them, because a component can be removed and re-added in the same frame. */
EPIX_EXPORT EPIX_API void extract_skins(
    ecs::ResMut<SkinUniforms> skin_uniforms, app::Extract<ecs::Query<SkinnedMeshItem>> skinned_meshes,
    app::Extract<ecs::Query<ChangedSkinnedMeshItem, SkinnedMeshChangedFilter>> changed_skinned_meshes,
    app::Extract<ecs::Res<assets::Assets<mesh::SkinnedMeshInverseBindposes>>> inverse_bindposes,
    app::Extract<ecs::Query<ChangedTransformItem, ChangedTransformFilter>> changed_transforms,
    app::Extract<JointTransformQuery> joints,
    app::Extract<ecs::RemovedComponents<mesh::SkinnedMesh>> removed_skinned_meshes);

/** @brief Tag every skinned mesh as un-batchable where skins use uniform buffers
 *  (Bevy `no_automatic_skin_batching`, `render/skin.rs:611-622`).
 *
 *  A skinned mesh's joints are bound at a dynamic offset per entity, so on a
 *  platform without storage buffers for skins two such meshes cannot share a
 *  batch at all - the whole point of the system is to stop the batcher trying.
 *  Where storage buffers are available the question does not arise, which is why
 *  the system returns early.
 *
 *  Bevy uses `try_insert`, which does nothing if the entity is gone; Epix's
 *  `Commands::entity` does not validate, so `insert` is the same operation here. */
EPIX_EXPORT EPIX_API void no_automatic_skin_batching(
    ecs::Commands commands,
    ecs::Query<ecs::Item<ecs::Entity>,
               ecs::Filter<ecs::With<mesh::SkinnedMesh>, ecs::Without<render::phase::NoAutomaticBatching>>>
        query,
    ecs::Res<wgpu::Device> render_device);

}  // namespace epix::pbr
