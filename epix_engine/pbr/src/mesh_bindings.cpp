#include <epix/pbr/mesh_bindings.hpp>

#include <limits>
#include <optional>
#include <vector>

namespace epix::pbr {

/** @brief The individual entries the mesh bind group layouts are built from
 *  (Bevy `render/mesh_bindings.rs:25-78`).
 *
 *  Each returns a deferred entry: the binding number and the collection's
 *  default visibility are applied when it is placed in a layout, except where the
 *  entry sets its own visibility, which is what Bevy's `.visibility(..)` does. */
namespace mesh_layout_entry {

/** @brief The mesh model uniform (Bevy `layout_entry::model`). */
inline render::render_resource::BindGroupLayoutEntryBuilder model(const wgpu::Limits& limits) {
    return render::render_resource::BindGroupLayoutEntryBuilder{
        wgpu::BindGroupLayoutEntry().setBuffer(
            render::render_resource::GpuArrayBuffer<MeshUniform>::binding_layout(limits))}
        .visibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment);
}

/** @brief The joint matrices, as a storage buffer where the device has them and
 *  as a uniform buffer otherwise (Bevy `layout_entry::skinning`). */
inline render::render_resource::BindGroupLayoutEntryBuilder skinning(const wgpu::Limits& limits) {
    using render::render_resource::binding_types::storage_buffer_read_only;
    using render::render_resource::binding_types::uniform_buffer;
    const auto size = static_cast<std::uint64_t>(JOINT_BUFFER_SIZE);
    return skins_use_uniform_buffers(limits) ? uniform_buffer(true, size) : storage_buffer_read_only(false, size);
}

/** @brief The morph weights (Bevy `layout_entry::weights`). */
inline render::render_resource::BindGroupLayoutEntryBuilder weights() {
    return render::render_resource::binding_types::uniform_buffer(true, MORPH_BUFFER_SIZE);
}

/** @brief The morph target textures (Bevy `layout_entry::targets`). */
inline render::render_resource::BindGroupLayoutEntryBuilder targets() {
    return render::render_resource::binding_types::texture_3d(wgpu::TextureSampleType::eUnfilterableFloat);
}

/** @brief One view's lightmap texture (Bevy
 *  `layout_entry::lightmaps_texture_view`). */
inline render::render_resource::BindGroupLayoutEntryBuilder lightmaps_texture_view() {
    return render::render_resource::binding_types::texture_2d(wgpu::TextureSampleType::eFloat)
        .visibility(wgpu::ShaderStage::eFragment);
}

/** @brief The lightmap sampler (Bevy `layout_entry::lightmaps_sampler`). */
inline render::render_resource::BindGroupLayoutEntryBuilder lightmaps_sampler() {
    return render::render_resource::binding_types::sampler(wgpu::SamplerBindingType::eFiltering)
        .visibility(wgpu::ShaderStage::eFragment);
}

/** @brief The slab's lightmap textures as a binding array (Bevy
 *  `layout_entry::lightmaps_texture_view_array`).
 *
 *  Bevy writes `texture_2d(..).visibility(..).count(N)`: the array-ness is the
 *  entry's binding-array size, not a `2d-array` view dimension. */
inline render::render_resource::BindGroupLayoutEntryBuilder lightmaps_texture_view_array() {
    return render::render_resource::binding_types::texture_2d(wgpu::TextureSampleType::eFloat)
        .visibility(wgpu::ShaderStage::eFragment)
        .count(static_cast<std::uint32_t>(LIGHTMAPS_PER_SLAB));
}

/** @brief The slab's lightmap samplers as a binding array (Bevy
 *  `layout_entry::lightmaps_sampler_array`). */
inline render::render_resource::BindGroupLayoutEntryBuilder lightmaps_sampler_array() {
    return render::render_resource::binding_types::sampler(wgpu::SamplerBindingType::eFiltering)
        .visibility(wgpu::ShaderStage::eFragment)
        .count(static_cast<std::uint32_t>(LIGHTMAPS_PER_SLAB));
}

}  // namespace mesh_layout_entry

namespace bind_group_entry {

// Bevy's `mod bind_group_entry` is `pub(super)`: these construct the entries the
// mesh bind groups are created from and nothing outside this file uses them, so
// they are file-local rather than header-inline.

/** @brief A buffer entry covering the whole buffer when `size` is empty (Bevy
 *  `bind_group_entry::entry`). */
wgpu::BindGroupEntry entry(std::uint32_t binding, std::optional<std::uint64_t> size, const wgpu::Buffer& buffer) {
    return wgpu::BindGroupEntry()
        .setBinding(binding)
        .setBuffer(buffer)
        .setOffset(0)
        .setSize(size ? *size : std::numeric_limits<std::uint64_t>::max());
}

/** @brief The mesh model buffer (Bevy `bind_group_entry::model`). */
wgpu::BindGroupEntry model(std::uint32_t binding, const wgpu::Buffer& buffer) {
    return entry(binding, std::nullopt, buffer);
}

/** @brief The joint matrices, bound as a fixed-size slice when they are uniforms
 *  (Bevy `bind_group_entry::skinning`; Bevy reads the device's limits, this takes
 *  the fact). */
wgpu::BindGroupEntry skinning(bool skins_use_uniform_buffers, std::uint32_t binding, const wgpu::Buffer& buffer) {
    const std::optional<std::uint64_t> size =
        skins_use_uniform_buffers ? std::optional<std::uint64_t>(JOINT_BUFFER_SIZE) : std::nullopt;
    return entry(binding, size, buffer);
}

/** @brief The morph weights, whose layout declares a fixed size (Bevy
 *  `bind_group_entry::weights`). */
wgpu::BindGroupEntry weights(std::uint32_t binding, const wgpu::Buffer& buffer) {
    return entry(binding, MORPH_BUFFER_SIZE, buffer);
}

/** @brief The morph target texture (Bevy `bind_group_entry::targets`). */
wgpu::BindGroupEntry targets(std::uint32_t binding, const wgpu::TextureView& texture) {
    return wgpu::BindGroupEntry().setBinding(binding).setTextureView(texture);
}

}  // namespace

MeshLayouts MeshLayouts::make(const wgpu::Limits& limits, bool binding_arrays_are_usable) {
    return MeshLayouts{
        .model_only             = model_only_layout(limits),
        .lightmapped            = lightmapped_layout(limits, binding_arrays_are_usable),
        .skinned                = skinned_layout(limits),
        .skinned_motion         = skinned_motion_layout(limits),
        .morphed                = morphed_layout(limits),
        .morphed_motion         = morphed_motion_layout(limits),
        .morphed_skinned        = morphed_skinned_layout(limits),
        .morphed_skinned_motion = morphed_skinned_motion_layout(limits),
    };
}

render::BindGroupLayoutDescriptor MeshLayouts::model_only_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "mesh_layout", BindGroupLayoutEntries<>::single(wgpu::ShaderStage::eNone, mesh_layout_entry::model(limits)));
}

render::BindGroupLayoutDescriptor MeshLayouts::skinned_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "skinned_mesh_layout", BindGroupLayoutEntries<>::with_indices(
                                   wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
                                   // The current frame's joint matrix buffer.
                                   std::pair{1u, mesh_layout_entry::skinning(limits)})
                                   .entries());
}

render::BindGroupLayoutDescriptor MeshLayouts::skinned_motion_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "skinned_motion_mesh_layout", BindGroupLayoutEntries<>::with_indices(
                                          wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
                                          // The current frame's joint matrix buffer.
                                          std::pair{1u, mesh_layout_entry::skinning(limits)},
                                          // The previous frame's joint matrix buffer.
                                          std::pair{6u, mesh_layout_entry::skinning(limits)})
                                          .entries());
}

render::BindGroupLayoutDescriptor MeshLayouts::morphed_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "morphed_mesh_layout", BindGroupLayoutEntries<>::with_indices(wgpu::ShaderStage::eVertex,
                                                                      std::pair{0u, mesh_layout_entry::model(limits)},
                                                                      // The current frame's morph weight buffer.
                                                                      std::pair{2u, mesh_layout_entry::weights()},
                                                                      std::pair{3u, mesh_layout_entry::targets()})
                                   .entries());
}

render::BindGroupLayoutDescriptor MeshLayouts::morphed_motion_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    // Bevy labels this one `morphed_mesh_layout` as well, the same string as
    // `morphed_layout`; kept as is.
    return render::BindGroupLayoutDescriptor::create(
        "morphed_mesh_layout", BindGroupLayoutEntries<>::with_indices(wgpu::ShaderStage::eVertex,
                                                                      std::pair{0u, mesh_layout_entry::model(limits)},
                                                                      // The current frame's morph weight buffer.
                                                                      std::pair{2u, mesh_layout_entry::weights()},
                                                                      std::pair{3u, mesh_layout_entry::targets()},
                                                                      // The previous frame's morph weight buffer.
                                                                      std::pair{7u, mesh_layout_entry::weights()})
                                   .entries());
}

render::BindGroupLayoutDescriptor MeshLayouts::morphed_skinned_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "morphed_skinned_mesh_layout",
        BindGroupLayoutEntries<>::with_indices(
            wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
            // The current frame's joint matrix buffer.
            std::pair{1u, mesh_layout_entry::skinning(limits)},
            // The current frame's morph weight buffer.
            std::pair{2u, mesh_layout_entry::weights()}, std::pair{3u, mesh_layout_entry::targets()})
            .entries());
}

render::BindGroupLayoutDescriptor MeshLayouts::morphed_skinned_motion_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "morphed_skinned_motion_mesh_layout",
        BindGroupLayoutEntries<>::with_indices(
            wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
            // The current frame's joint matrix buffer.
            std::pair{1u, mesh_layout_entry::skinning(limits)},
            // The current frame's morph weight buffer.
            std::pair{2u, mesh_layout_entry::weights()}, std::pair{3u, mesh_layout_entry::targets()},
            // The previous frame's joint matrix buffer.
            std::pair{6u, mesh_layout_entry::skinning(limits)},
            // The previous frame's morph weight buffer.
            std::pair{7u, mesh_layout_entry::weights()})
            .entries());
}

render::BindGroupLayoutDescriptor MeshLayouts::lightmapped_layout(const wgpu::Limits& limits,
                                                                         bool binding_arrays_are_usable) {
    using render::render_resource::BindGroupLayoutEntries;
    if (binding_arrays_are_usable) {
        return render::BindGroupLayoutDescriptor::create(
            "lightmapped_mesh_layout", BindGroupLayoutEntries<>::with_indices(
                                           wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
                                           std::pair{4u, mesh_layout_entry::lightmaps_texture_view_array()},
                                           std::pair{5u, mesh_layout_entry::lightmaps_sampler_array()})
                                           .entries());
    }
    return render::BindGroupLayoutDescriptor::create(
        "lightmapped_mesh_layout", BindGroupLayoutEntries<>::with_indices(
                                       wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
                                       std::pair{4u, mesh_layout_entry::lightmaps_texture_view()},
                                       std::pair{5u, mesh_layout_entry::lightmaps_sampler()})
                                       .entries());
}


wgpu::BindGroup MeshLayouts::create_model_only(const wgpu::Device& device,
                                               const render::PipelineServer& pipeline_server,
                                               const wgpu::Buffer& model) const {
    return device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("model_only_mesh_bind_group")
            .setLayout(pipeline_server.get_bind_group_layout(model_only))
            .setEntries(std::vector<wgpu::BindGroupEntry>{bind_group_entry::model(0u, model)}));
}

wgpu::BindGroup MeshLayouts::create_skinned(const wgpu::Device& device,
                                            const render::PipelineServer& pipeline_server,
                                            bool skins_use_uniform_buffers, const wgpu::Buffer& model,
                                            const wgpu::Buffer& current_skin) const {
    return device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("skinned_mesh_bind_group")
            .setLayout(pipeline_server.get_bind_group_layout(skinned))
            .setEntries(std::vector<wgpu::BindGroupEntry>{bind_group_entry::model(0u, model),
                                                          bind_group_entry::skinning(skins_use_uniform_buffers, 1u, current_skin)}));
}

wgpu::BindGroup MeshLayouts::create_skinned_motion(const wgpu::Device& device,
                                                   const render::PipelineServer& pipeline_server,
                                                   bool skins_use_uniform_buffers, const wgpu::Buffer& model,
                                                   const wgpu::Buffer& current_skin,
                                                   const wgpu::Buffer& prev_skin) const {
    return device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("skinned_motion_mesh_bind_group")
            .setLayout(pipeline_server.get_bind_group_layout(skinned_motion))
            .setEntries(std::vector<wgpu::BindGroupEntry>{
                bind_group_entry::model(0u, model), bind_group_entry::skinning(skins_use_uniform_buffers, 1u, current_skin),
                bind_group_entry::skinning(skins_use_uniform_buffers, 6u, prev_skin)}));
}

wgpu::BindGroup MeshLayouts::create_morphed(const wgpu::Device& device,
                                            const render::PipelineServer& pipeline_server, const wgpu::Buffer& model,
                                            const wgpu::Buffer& current_weights,
                                            const wgpu::TextureView& morph_targets) const {
    return device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("morphed_mesh_bind_group")
            .setLayout(pipeline_server.get_bind_group_layout(morphed))
            .setEntries(std::vector<wgpu::BindGroupEntry>{bind_group_entry::model(0u, model), bind_group_entry::weights(2u, current_weights),
                                                          bind_group_entry::targets(3u, morph_targets)}));
}

wgpu::BindGroup MeshLayouts::create_morphed_motion(const wgpu::Device& device,
                                                   const render::PipelineServer& pipeline_server,
                                                   const wgpu::Buffer& model, const wgpu::Buffer& current_weights,
                                                   const wgpu::TextureView& morph_targets,
                                                   const wgpu::Buffer& prev_weights) const {
    return device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("morphed_motion_mesh_bind_group")
            .setLayout(pipeline_server.get_bind_group_layout(morphed_motion))
            .setEntries(std::vector<wgpu::BindGroupEntry>{bind_group_entry::model(0u, model), bind_group_entry::weights(2u, current_weights),
                                                          bind_group_entry::targets(3u, morph_targets), bind_group_entry::weights(7u, prev_weights)}));
}

wgpu::BindGroup MeshLayouts::create_morphed_skinned(const wgpu::Device& device,
                                                    const render::PipelineServer& pipeline_server,
                                                    bool skins_use_uniform_buffers, const wgpu::Buffer& model,
                                                    const wgpu::Buffer& current_skin,
                                                    const wgpu::Buffer& current_weights,
                                                    const wgpu::TextureView& morph_targets) const {
    return device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("morphed_skinned_mesh_bind_group")
            .setLayout(pipeline_server.get_bind_group_layout(morphed_skinned))
            .setEntries(std::vector<wgpu::BindGroupEntry>{
                bind_group_entry::model(0u, model), bind_group_entry::skinning(skins_use_uniform_buffers, 1u, current_skin),
                bind_group_entry::weights(2u, current_weights), bind_group_entry::targets(3u, morph_targets)}));
}

wgpu::BindGroup MeshLayouts::create_morphed_skinned_motion(
    const wgpu::Device& device, const render::PipelineServer& pipeline_server, bool skins_use_uniform_buffers,
    const wgpu::Buffer& model, const wgpu::Buffer& current_skin, const wgpu::Buffer& current_weights,
    const wgpu::TextureView& morph_targets, const wgpu::Buffer& prev_skin, const wgpu::Buffer& prev_weights) const {
    return device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("morphed_skinned_motion_mesh_bind_group")
            .setLayout(pipeline_server.get_bind_group_layout(morphed_skinned_motion))
            .setEntries(std::vector<wgpu::BindGroupEntry>{
                bind_group_entry::model(0u, model), bind_group_entry::skinning(skins_use_uniform_buffers, 1u, current_skin),
                bind_group_entry::weights(2u, current_weights), bind_group_entry::targets(3u, morph_targets),
                bind_group_entry::skinning(skins_use_uniform_buffers, 6u, prev_skin), bind_group_entry::weights(7u, prev_weights)}));
}

}  // namespace epix::pbr
