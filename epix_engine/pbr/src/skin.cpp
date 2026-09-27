#include <epix/pbr/skin.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <spdlog/spdlog.h>
#include <utility>
#include <vector>

namespace epix::pbr {

bool skins_use_uniform_buffers(const wgpu::Limits& limits) {
    // Bevy memoises this in a OnceLock, so the first device decides for the
    // process. Here the answer is memoised once per binary: in a header it would
    // be once per translation unit, which would let two of them disagree.
    static const bool use_uniform_buffers = limits.maxStorageBuffersPerShaderStage == 0;
    return use_uniform_buffers;
}

SkinUniforms SkinUniforms::make(const wgpu::Device& render_device, const wgpu::Limits& limits) {
    const wgpu::BufferUsage usage =
        (::epix::pbr::skins_use_uniform_buffers(limits) ? wgpu::BufferUsage::eUniform : wgpu::BufferUsage::eStorage) |
        wgpu::BufferUsage::eCopyDst;
    const std::uint64_t size = static_cast<std::uint64_t>(MAX_JOINTS) * sizeof(glm::mat4);
    // Aggregate initialization: the allocator has no default constructor, so
    // every member is named.
    return SkinUniforms{
        .current_staging_buffer = {},
        .current_buffer         = render_device.createBuffer(wgpu::BufferDescriptor()
                                                         .setLabel("skin uniform buffer")
                                                         .setSize(size)
                                                         .setUsage(usage)),
        .prev_buffer            = render_device.createBuffer(wgpu::BufferDescriptor()
                                                      .setLabel("skin uniform buffer")
                                                      .setSize(size)
                                                      .setUsage(usage)),
        .allocator              = render::mesh::offset_allocator::Allocator(MAX_TOTAL_JOINTS),
        .skin_uniform_info      = {},
        .joint_to_skins         = {},
        .total_joints           = 0,
    };
}

std::optional<std::uint32_t> SkinUniforms::skin_index(render::sync_world::MainEntity skin) const {
    const auto it = skin_uniform_info.find(skin);
    if (it == skin_uniform_info.end()) return std::nullopt;
    return it->second.offset();
}

std::optional<SkinByteOffset> SkinUniforms::skin_byte_offset(render::sync_world::MainEntity skin) const {
    const auto index = skin_index(skin);
    if (!index) return std::nullopt;
    return SkinByteOffset::from_index(*index);
}

std::vector<render::sync_world::MainEntity> SkinUniforms::all_skins() const {
    std::vector<render::sync_world::MainEntity> skins;
    skins.reserve(skin_uniform_info.size());
    for (const auto& [entity, info] : skin_uniform_info) skins.push_back(entity);
    return skins;
}

void prepare_skins(ecs::Res<wgpu::Device> render_device, ecs::Res<wgpu::Queue> render_queue,
                   ecs::ResMut<SkinUniforms> uniform) {
    SkinUniforms& uniforms = *uniform;
    if (uniforms.current_staging_buffer.empty()) return;

    // Swap current and previous buffers: what was current last frame is this
    // frame's previous, which is what the motion-vector prepass reads.
    std::swap(uniforms.current_buffer, uniforms.prev_buffer);

    // Resize if necessary, including extra space equal to `MAX_JOINTS` because a
    // full uniform buffer's worth has to be bindable when skins use uniform
    // buffers on this platform.
    const std::uint64_t needed_size =
        static_cast<std::uint64_t>(uniforms.current_staging_buffer.size() + MAX_JOINTS) * sizeof(glm::mat4);
    if (uniforms.current_buffer.getSize() < needed_size) {
        std::uint64_t new_size = uniforms.current_buffer.getSize();
        while (new_size < needed_size) {
            // 1.5x growth factor, rounded to a multiple of four.
            new_size = ((new_size + new_size / 2) + 3) & ~std::uint64_t{3};
        }

        wgpu::Limits limits{};
        const bool uniform_buffers =
            render_device->getLimits(&limits) == wgpu::Status::eSuccess && ::epix::pbr::skins_use_uniform_buffers(limits);
        const wgpu::BufferUsage usage =
            (uniform_buffers ? wgpu::BufferUsage::eUniform : wgpu::BufferUsage::eStorage) | wgpu::BufferUsage::eCopyDst;
        uniforms.current_buffer = render_device->createBuffer(
            wgpu::BufferDescriptor().setLabel("skin uniform buffer").setSize(new_size).setUsage(usage));
        uniforms.prev_buffer = render_device->createBuffer(
            wgpu::BufferDescriptor().setLabel("skin uniform buffer").setSize(new_size).setUsage(usage));

        // The new previous buffer has no previous joint data of its own, so it is
        // filled with the current frame's - Bevy's documented one-frame
        // approximation for motion vectors.
        render_queue->writeBuffer(uniforms.prev_buffer, 0, uniforms.current_staging_buffer.data(),
                                  uniforms.current_staging_buffer.size() * sizeof(glm::mat4));
    }

    render_queue->writeBuffer(uniforms.current_buffer, 0, uniforms.current_staging_buffer.data(),
                              uniforms.current_staging_buffer.size() * sizeof(glm::mat4));

    // The previous buffer is deliberately not written: it already holds last
    // frame's joints and that data is still on the GPU.
}

void remove_skin(SkinUniforms& skin_uniforms, render::sync_world::MainEntity skinned_mesh_entity) {
    const auto found = skin_uniforms.skin_uniform_info.find(skinned_mesh_entity);
    if (found == skin_uniforms.skin_uniform_info.end()) return;
    const SkinUniformInfo old_skin_uniform_info = found->second;
    skin_uniforms.skin_uniform_info.erase(found);

    // Free the allocation.
    skin_uniforms.allocator.free(old_skin_uniform_info.allocation);

    // Remove the inverse mapping from each joint back to the skin, dropping the
    // joint's entry once no skin refers to it: the fine-grained extraction reads
    // this map, and a stale entry would make it re-extract skins that are gone.
    for (const render::sync_world::MainEntity joint : old_skin_uniform_info.joints) {
        const auto joint_entry = skin_uniforms.joint_to_skins.find(joint);
        if (joint_entry == skin_uniforms.joint_to_skins.end()) continue;
        std::vector<render::sync_world::MainEntity>& skins = joint_entry->second;
        std::erase(skins, skinned_mesh_entity);
        if (skins.empty()) skin_uniforms.joint_to_skins.erase(joint_entry);
    }

    // Update the total number of joints.
    skin_uniforms.total_joints -= old_skin_uniform_info.joints.size();
}

void add_skin(render::sync_world::MainEntity skinned_mesh_entity, const mesh::SkinnedMesh& skinned_mesh,
              SkinUniforms& skin_uniforms,
              const assets::Assets<mesh::SkinnedMeshInverseBindposes>& inverse_bindposes,
              app::Extract<JointTransformQuery>& joints) {
    // Space is allocated in whole allocation units.
    const std::size_t units =
        (skinned_mesh.joints.size() + JOINTS_PER_ALLOCATION_UNIT - 1) / JOINTS_PER_ALLOCATION_UNIT;
    const auto allocation = skin_uniforms.allocator.allocate(static_cast<std::uint32_t>(units));
    if (!allocation) {
        spdlog::error("[pbr] Out of space for skin. Tried to allocate space for {} joints.", skinned_mesh.joints.size());
        return;
    }

    SkinUniformInfo skin_uniform_info{.allocation = *allocation, .joints = {}};
    skin_uniform_info.joints.reserve(skinned_mesh.joints.size());
    for (const ecs::Entity joint : skinned_mesh.joints) {
        skin_uniform_info.joints.push_back(render::sync_world::MainEntity{joint});
    }

    // Epix's `Assets::get` hands back an optional reference rather than Bevy's
    // `Option<&T>`, so the pointer the loop needs is taken from it once.
    const auto bindposes_asset = inverse_bindposes.get(skinned_mesh.inverse_bindposes);
    const mesh::SkinnedMeshInverseBindposes* bindposes = bindposes_asset ? std::addressof(bindposes_asset->get()) : nullptr;

    for (std::size_t joint_index = 0; joint_index < skinned_mesh.joints.size(); ++joint_index) {
        // The initial matrix is the joint's transform times its inverse bind
        // pose, or identity when either is missing - Bevy's `match` fallthrough.
        glm::mat4 joint_matrix{1.0f};
        if (bindposes != nullptr && joint_index < bindposes->size()) {
            if (auto row = joints.get(skinned_mesh.joints[joint_index]); row.has_value()) {
                auto&& [joint_transform] = *row;
                joint_matrix             = joint_transform.matrix * (*bindposes)[joint_index];
            }
        }

        // Write it in, growing the staging buffer with identities where the
        // allocator placed this skin past the end.
        const std::size_t buffer_index = static_cast<std::size_t>(skin_uniform_info.offset()) + joint_index;
        if (skin_uniforms.current_staging_buffer.size() < buffer_index + 1) {
            skin_uniforms.current_staging_buffer.resize(buffer_index + 1, glm::mat4{1.0f});
        }
        skin_uniforms.current_staging_buffer[buffer_index] = joint_matrix;

        // Record the inverse mapping the fine-grained extraction reads.
        skin_uniforms.joint_to_skins[skin_uniform_info.joints[joint_index]].push_back(skinned_mesh_entity);
    }

    skin_uniforms.total_joints += skinned_mesh.joints.size();
    skin_uniforms.skin_uniform_info.insert_or_assign(skinned_mesh_entity, std::move(skin_uniform_info));
}

void extract_joints_for_skin(
    render::sync_world::MainEntity skin_entity, const mesh::SkinnedMesh& skin, SkinUniforms& skin_uniforms,
    app::Extract<ecs::Query<ChangedSkinnedMeshItem, SkinnedMeshChangedFilter>>& changed_skinned_meshes,
    const assets::Assets<mesh::SkinnedMeshInverseBindposes>& inverse_bindposes, app::Extract<JointTransformQuery>& joints) {
    // A skin initialized this frame already had every joint written.
    if (changed_skinned_meshes.get(skin_entity.id()).has_value()) return;

    const auto info = skin_uniforms.skin_uniform_info.find(skin_entity);
    if (info == skin_uniforms.skin_uniform_info.end()) return;
    const auto bindposes_asset = inverse_bindposes.get(skin.inverse_bindposes);
    if (!bindposes_asset) return;
    const mesh::SkinnedMeshInverseBindposes& bindposes = bindposes_asset->get();

    // The joints and the inverse bind poses are paired by index, so the shorter
    // of the two decides how many matrices are written.
    const std::size_t joint_count = std::min(skin.joints.size(), bindposes.size());
    for (std::size_t joint_index = 0; joint_index < joint_count; ++joint_index) {
        if (auto row = joints.get(skin.joints[joint_index]); row.has_value()) {
            auto&& [joint_transform]             = *row;
            const std::size_t buffer_index       = static_cast<std::size_t>(info->second.offset()) + joint_index;
            skin_uniforms.current_staging_buffer[buffer_index] = joint_transform.matrix * bindposes[joint_index];
        }
    }
}

void extract_joints(SkinUniforms& skin_uniforms, app::Extract<ecs::Query<SkinnedMeshItem>>& skinned_meshes,
                    app::Extract<ecs::Query<ChangedSkinnedMeshItem, SkinnedMeshChangedFilter>>& changed_skinned_meshes,
                    const assets::Assets<mesh::SkinnedMeshInverseBindposes>& inverse_bindposes,
                    app::Extract<ecs::Query<ChangedTransformItem, ChangedTransformFilter>>& changed_transforms,
                    app::Extract<JointTransformQuery>& joints) {
    // Once changed entities outnumber a quarter of the scene's joints,
    // fine-grained detection costs more than it saves: re-extract everything.
    const std::size_t threshold = static_cast<std::size_t>(
        std::floor(static_cast<double>(skin_uniforms.total_joints) * JOINT_EXTRACTION_THRESHOLD_FACTOR));

    std::size_t changed_count = 0;
    for (auto&& changed : changed_transforms.iter()) {
        if (changed_count > threshold) break;
        ++changed_count;
        (void)changed;
    }
    if (changed_count > threshold) {
        for (auto&& [skin_entity, skin] : skinned_meshes.iter()) {
            extract_joints_for_skin(render::sync_world::MainEntity{skin_entity}, skin, skin_uniforms,
                                    changed_skinned_meshes, inverse_bindposes, joints);
        }
        return;
    }

    // Otherwise only the skins the changed joints map to.
    std::vector<render::sync_world::MainEntity> dirty_skins;
    for (auto&& [joint_entity, joint_transform] : changed_transforms.iter()) {
        (void)joint_transform;
        const auto found = skin_uniforms.joint_to_skins.find(render::sync_world::MainEntity{joint_entity});
        if (found == skin_uniforms.joint_to_skins.end()) continue;
        for (const render::sync_world::MainEntity skin : found->second) {
            if (std::find(dirty_skins.begin(), dirty_skins.end(), skin) == dirty_skins.end()) {
                dirty_skins.push_back(skin);
            }
        }
    }

    for (const render::sync_world::MainEntity skin_entity : dirty_skins) {
        auto row = skinned_meshes.get(skin_entity.id());
        if (!row.has_value()) continue;
        auto&& [entity, skin] = *row;
        (void)entity;
        extract_joints_for_skin(skin_entity, skin, skin_uniforms, changed_skinned_meshes, inverse_bindposes, joints);
    }
}

void add_or_delete_skins(SkinUniforms& skin_uniforms,
                         app::Extract<ecs::Query<ChangedSkinnedMeshItem, SkinnedMeshChangedFilter>>& changed_skinned_meshes,
                         const assets::Assets<mesh::SkinnedMeshInverseBindposes>& inverse_bindposes,
                         app::Extract<JointTransformQuery>& joints) {
    for (auto&& [entity, view_visibility, skinned_mesh] : changed_skinned_meshes.iter()) {
        const render::sync_world::MainEntity skinned_mesh_entity{entity};

        // What was there last frame is released first, so a skin never keeps an
        // allocation that no longer matches it.
        remove_skin(skin_uniforms, skinned_mesh_entity);

        // An invisible skin is simply absent now.
        if (!view_visibility.get()) continue;

        add_skin(skinned_mesh_entity, skinned_mesh, skin_uniforms, inverse_bindposes, joints);
    }
}

void extract_skins(ecs::ResMut<SkinUniforms> skin_uniforms, app::Extract<ecs::Query<SkinnedMeshItem>> skinned_meshes,
                   app::Extract<ecs::Query<ChangedSkinnedMeshItem, SkinnedMeshChangedFilter>> changed_skinned_meshes,
                   app::Extract<ecs::Res<assets::Assets<mesh::SkinnedMeshInverseBindposes>>> inverse_bindposes,
                   app::Extract<ecs::Query<ChangedTransformItem, ChangedTransformFilter>> changed_transforms,
                   app::Extract<JointTransformQuery> joints,
                   app::Extract<ecs::RemovedComponents<mesh::SkinnedMesh>> removed_skinned_meshes) {
    // `Extract` forwards to what it wraps, so the queries pass straight through
    // and the resources are reached with a single dereference, as elsewhere.
    SkinUniforms& uniforms = *skin_uniforms;

    // 1. Skins that appeared, disappeared or changed are allocated or released.
    add_or_delete_skins(uniforms, changed_skinned_meshes, *inverse_bindposes, joints);

    // 2. The joints of the skins the changed transforms reach are re-extracted.
    extract_joints(uniforms, skinned_meshes, changed_skinned_meshes, *inverse_bindposes, changed_transforms, joints);

    // 3. Skins whose component was removed are released - but only if step 1 did
    // not already see them, since a component can be removed and re-added in the
    // same frame.
    for (const ecs::Entity skinned_mesh_entity : removed_skinned_meshes.read()) {
        if (changed_skinned_meshes.get(skinned_mesh_entity).has_value()) continue;
        remove_skin(uniforms, render::sync_world::MainEntity{skinned_mesh_entity});
    }
}

}  // namespace epix::pbr
