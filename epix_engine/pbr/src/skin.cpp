#include <epix/pbr/skin.hpp>

#include <cstdint>
#include <optional>
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

}  // namespace epix::pbr
