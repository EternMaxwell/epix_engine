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

}  // namespace epix::pbr
