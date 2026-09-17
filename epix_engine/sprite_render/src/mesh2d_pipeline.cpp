#include <epix/core_graph.hpp>
#include <epix/sprite_render/mesh2d_pipeline.hpp>

namespace epix::sprite_render {

wgpu::BindGroupLayout create_mesh2d_view_layout(const wgpu::Device& device) {
    using render::render_resource::BindGroupLayoutEntries;
    using render::render_resource::binding_types::uniform_buffer;
    // Bevy `init_mesh_2d_pipeline` (mesh.rs:297-309): a dynamic view uniform, the
    // globals uniform, then the two tonemapping LUT entries at fragment
    // visibility, all at group 0. The LUT builders are ref-qualified on their
    // `visibility` member, so the array must stay mutable.
    auto lut           = core_graph::get_lut_bind_group_layout_entries();
    const auto entries = BindGroupLayoutEntries<>::sequential(
        wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment,
        uniform_buffer(true, sizeof(render::view::ViewUniform)),
        uniform_buffer(false, sizeof(render::GlobalsUniform)),
        lut[0].visibility(wgpu::ShaderStage::eFragment), lut[1].visibility(wgpu::ShaderStage::eFragment));
    return device.createBindGroupLayout(
        wgpu::BindGroupLayoutDescriptor().setLabel("mesh2d_view_layout").setEntries(entries.entries()));
}

wgpu::BindGroupLayout create_mesh2d_mesh_layout(const wgpu::Device& device) {
    using render::render_resource::GpuArrayBuffer;
    // Bevy `init_mesh_2d_pipeline` (mesh.rs:311-317): one vertex/fragment binding
    // holding the mesh2d uniform array, as a storage buffer where the adapter has
    // them and as a dynamic-offset uniform array otherwise.
    wgpu::Limits limits{};
    device.getLimits(&limits);
    const auto buffer_binding = GpuArrayBuffer<Mesh2dUniform>::binding_layout(limits);
    const std::array<wgpu::BindGroupLayoutEntry, 1> entries{
        wgpu::BindGroupLayoutEntry()
            .setBinding(0)
            .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
            .setBuffer(buffer_binding),
    };
    const std::span<const wgpu::BindGroupLayoutEntry> entry_span{entries};
    return device.createBindGroupLayout(
        wgpu::BindGroupLayoutDescriptor().setLabel("mesh2d_layout").setEntries(entry_span));
}

std::optional<std::uint32_t> mesh2d_per_object_buffer_batch_size(const wgpu::Limits& limits) {
    using render::render_resource::BatchedUniformBuffer;
    // Bevy's `GpuArrayBuffer::<T>::batch_size` only produces a batch size for the
    // uniform fallback; the storage-buffer path has none.
    if (limits.maxStorageBuffersPerShaderStage == 0) {
        return static_cast<std::uint32_t>(BatchedUniformBuffer<Mesh2dUniform>::batch_size(limits));
    }
    return std::nullopt;
}

}  // namespace epix::sprite_render
