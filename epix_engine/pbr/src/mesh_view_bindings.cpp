/** @brief Build each view's mesh view bind group (Bevy `prepare_mesh_view_bind_groups`,
 *  `render/mesh_view_bindings.rs:563-869`).
 *
 *  Prepass and transmission bindings follow the view's textures, and all three bind groups
 *  are created even when no material samples the probe fallback textures. Probe gathering,
 *  SSAO, OIT, decals and atmosphere rendering remain separate unported subsystems; the
 *  no-probe/no-SSAO branches here bind Bevy's white fallbacks. */
#include <epix/pbr/mesh_pipeline_resource.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/core_graph/tonemapping.hpp>
#include <epix/pbr/cluster.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/light_probe.hpp>
#include <epix/pbr/shadow_samplers.hpp>
#include <epix/pbr/ssr.hpp>
#include <epix/render/fallback_image.hpp>
#include <epix/render/globals.hpp>
#include <epix/render/visibility_range.hpp>
#include <limits>
#include <vector>
#endif

namespace epix::pbr {

namespace {
/** @brief A buffer entry with an explicit size, which is what a uniform binding needs. */
wgpu::BindGroupEntry buffer_entry(std::uint32_t binding, std::uint64_t size, const wgpu::Buffer& buffer) {
    return wgpu::BindGroupEntry{}.setBinding(binding).setBuffer(buffer).setOffset(0).setSize(size);
}

/** @brief A buffer entry covering the whole buffer, as `bind_group_entry::entry` spells it. */
wgpu::BindGroupEntry buffer_entry(std::uint32_t binding, const wgpu::Buffer& buffer) {
    return wgpu::BindGroupEntry{}.setBinding(binding).setBuffer(buffer).setOffset(0).setSize(
        std::numeric_limits<std::uint64_t>::max());
}

wgpu::BindGroupEntry texture_entry(std::uint32_t binding, const wgpu::TextureView& view) {
    return wgpu::BindGroupEntry{}.setBinding(binding).setTextureView(view);
}

wgpu::BindGroupEntry sampler_entry(std::uint32_t binding, const wgpu::Sampler& sampler) {
    return wgpu::BindGroupEntry{}.setBinding(binding).setSampler(sampler);
}
}  // namespace

void prepare_mesh_view_bind_groups(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Res<MeshPipeline> mesh_pipeline,
    ecs::Res<ShadowSamplers> shadow_samplers,
    ecs::Res<LightMeta> light_meta,
    ecs::Res<GlobalClusterableObjectMeta> global_light_meta,
    ecs::Res<FogMeta> fog_meta,
    ecs::Res<render::view::ViewUniforms> view_uniforms,
    ecs::Res<render::GlobalsBuffer> globals_buffer,
    ecs::Res<LightProbesBuffer> light_probes_buffer,
    ecs::Res<ScreenSpaceReflectionsBuffer> ssr_buffer,
    ecs::Res<EnvironmentMapUniformBuffer> environment_map_uniform,
    ecs::Res<render::view::RenderVisibilityRanges> visibility_ranges,
    ecs::Res<render::texture::FallbackImage> fallback_image,
    ecs::Res<render::texture::FallbackImageZero> fallback_image_zero,
    ecs::Res<render::RenderAssets<render::texture::GpuImage>> images,
    ecs::Res<core_graph::TonemappingLuts> tonemapping_luts,
    ecs::Query<ecs::Item<ecs::Entity,
                         const ViewShadowBindings&,
                         const ViewClusterBindings&,
                         const core_graph::Tonemapping&,
                         const render::view::Msaa&,
                         ecs::Opt<const core_graph::core_3d::ViewPrepassTextures&>,
                         ecs::Opt<const core_graph::core_3d::ViewTransmissionTexture&>>> views) {
    const auto view_uniform_binding        = view_uniforms->uniforms.binding();
    const auto light_binding               = light_meta->view_gpu_lights.binding();
    const auto clusterable_binding         = global_light_meta->gpu_clusterable_objects.binding();
    const auto globals_binding             = globals_buffer->buffer.binding();
    const auto fog_binding                 = fog_meta->gpu_fogs.binding();
    const auto probes_binding              = light_probes_buffer->uniforms.binding();
    const auto ssr_binding                 = ssr_buffer->uniforms.binding();
    const auto environment_binding         = environment_map_uniform->uniforms.binding();
    const wgpu::Buffer* visibility_binding = visibility_ranges->buffer.buffer();
    if (!view_uniform_binding || !light_binding || !clusterable_binding || !globals_binding || !fog_binding ||
        !probes_binding || !ssr_binding || !environment_binding || visibility_binding == nullptr) {
        return;
    }

    for (auto&& [entity, shadow_bindings, cluster_bindings, tonemapping, msaa, prepass, transmission] : views.iter()) {
        const MeshPipelineViewLayoutKey layout_key =
            MeshPipelineViewLayoutKey::from_msaa(msaa) |
            MeshPipelineViewLayoutKey::from_prepass_textures(prepass ? &prepass->get() : nullptr);
        const MeshPipelineViewLayout& layout = mesh_pipeline->get_view_layout(layout_key);

        const auto cluster_index_lists    = cluster_bindings.clusterable_object_index_lists_binding();
        const auto cluster_offsets_counts = cluster_bindings.offsets_and_counts_binding();
        if (!cluster_index_lists || !cluster_offsets_counts) {
            continue;
        }
        const auto [lut_texture, lut_sampler] =
            core_graph::get_lut_bindings(*images, *tonemapping_luts, tonemapping, *fallback_image);

        std::vector<wgpu::BindGroupEntry> entries;
        entries.reserve(20);
        entries.push_back(buffer_entry(0u, sizeof(render::view::ViewUniform), view_uniform_binding->get()));
        entries.push_back(buffer_entry(1u, sizeof(GpuLights), light_binding->get()));
        entries.push_back(texture_entry(2u, shadow_bindings.point_light_depth_texture_view));
        entries.push_back(sampler_entry(3u, shadow_samplers->point_light_comparison_sampler));
        entries.push_back(texture_entry(5u, shadow_bindings.directional_light_depth_texture_view));
        entries.push_back(sampler_entry(6u, shadow_samplers->directional_light_comparison_sampler));
        entries.push_back(buffer_entry(8u, clusterable_binding->get()));
        entries.push_back(buffer_entry(9u, cluster_index_lists->get()));
        entries.push_back(buffer_entry(10u, cluster_offsets_counts->get()));
        entries.push_back(buffer_entry(11u, globals_binding->get()));
        entries.push_back(buffer_entry(12u, sizeof(GpuFog), fog_binding->get()));
        entries.push_back(buffer_entry(13u, sizeof(LightProbesUniform), probes_binding->get()));
        entries.push_back(buffer_entry(14u, *visibility_binding));
        entries.push_back(buffer_entry(15u, sizeof(ScreenSpaceReflectionsUniform), ssr_binding->get()));
        entries.push_back(texture_entry(16u, fallback_image->d2.texture_view));
        entries.push_back(buffer_entry(17u, sizeof(EnvironmentMapUniform), environment_binding->get()));
        entries.push_back(texture_entry(18u, lut_texture));
        entries.push_back(sampler_entry(19u, lut_sampler));
        const auto prepass_views = prepass_bindings(prepass);
        for (std::uint32_t index = 0; index < prepass_views.size(); ++index) {
            if (prepass_views[index]) entries.push_back(texture_entry(20u + index, *prepass_views[index]));
        }
        entries.push_back(
            texture_entry(24u, transmission ? transmission->get().view : fallback_image_zero->image.texture_view));
        entries.push_back(
            sampler_entry(25u, transmission ? transmission->get().sampler : fallback_image_zero->image.sampler));

        const wgpu::BindGroup main =
            render_device->createBindGroup(wgpu::BindGroupDescriptor()
                                               .setLabel("mesh_view_bind_group")
                                               .setLayout(pipeline_server->get_bind_group_layout(layout.main_layout))
                                               .setEntries(entries));
        // Bevy fills every array slot with the appropriate dimensional fallback
        // when this view has no gathered probes. Arrays are a device capability,
        // independent of whether this frame happens to use an environment map.
        std::vector<wgpu::BindGroupEntry> probe_entries;
        std::array<wgpu::BindGroupEntryExtras, 3> array_extras;
        const auto add_probe_texture = [&](std::uint32_t binding, const wgpu::TextureView& texture, std::size_t slot) {
            auto entry = texture_entry(binding, texture);
            if (mesh_pipeline->binding_arrays_are_usable) {
                array_extras[slot].setTextureViews(std::vector<wgpu::TextureView>(MAX_VIEW_LIGHT_PROBES, texture));
                entry.setNextInChain(array_extras[slot]);
            }
            probe_entries.push_back(std::move(entry));
        };
        add_probe_texture(0u, fallback_image->cube.texture_view, 0);
        add_probe_texture(1u, fallback_image->cube.texture_view, 1);
        probe_entries.push_back(sampler_entry(2u, fallback_image->cube.sampler));
        if (IRRADIANCE_VOLUMES_ARE_USABLE) {
            add_probe_texture(3u, fallback_image->d3.texture_view, 2);
            probe_entries.push_back(sampler_entry(4u, fallback_image->d3.sampler));
        }
        const auto binding_array = render_device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("mesh_view_bind_group_binding_array")
                .setLayout(pipeline_server->get_bind_group_layout(layout.binding_array_layout))
                .setEntries(probe_entries));
        const auto empty =
            render_device->createBindGroup(wgpu::BindGroupDescriptor()
                                               .setLabel("mesh_view_bind_group_empty")
                                               .setLayout(pipeline_server->get_bind_group_layout(layout.empty_layout))
                                               .setEntries(std::array<wgpu::BindGroupEntry, 0>{}));
        commands.entity(entity).insert(MeshViewBindGroup{.main = main, .binding_array = binding_array, .empty = empty});
    }
}

}  // namespace epix::pbr
