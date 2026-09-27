#pragma once

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/core_graph/oit.hpp>
#include <epix/core_graph/tonemapping.hpp>
#include <epix/render/bind_group_layout.hpp>
#include <epix/render/bind_group_layout_entries.hpp>
#include <epix/render/globals.hpp>
#include <epix/render/view.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/pbr/atmosphere.hpp>
#include <epix/pbr/cluster.hpp>
#include <epix/pbr/clustered_decal.hpp>
#include <epix/pbr/fog.hpp>
#include <epix/pbr/gpu_lights.hpp>
#include <epix/pbr/light_probe.hpp>
#include <epix/pbr/mesh_pipeline.hpp>
#include <epix/pbr/mesh_pipeline_key.hpp>
#include <epix/pbr/prepass_bindings.hpp>
#include <epix/pbr/ssr.hpp>

namespace epix::pbr {

/** @brief The entry for a buffer whose form the device decides (Bevy
 *  `buffer_layout`, `render/mesh_view_bindings.rs:204-219`).
 *
 *  `min_binding_size` is Epix's stand-in for Bevy's `Option<NonZero<u64>>`,
 *  where zero means "no minimum". */
EPIX_EXPORT inline render::render_resource::BindGroupLayoutEntryBuilder buffer_layout(
    wgpu::BufferBindingType buffer_binding_type, bool has_dynamic_offset, std::uint64_t min_binding_size) {
    using namespace render::render_resource::binding_types;
    if (buffer_binding_type == wgpu::BufferBindingType::eUniform)
        return uniform_buffer(has_dynamic_offset, min_binding_size);
    if (buffer_binding_type == wgpu::BufferBindingType::eReadOnlyStorage)
        return storage_buffer_read_only(has_dynamic_offset, min_binding_size);
    return storage_buffer(has_dynamic_offset, min_binding_size);
}

/** @brief The two entry lists of a mesh view layout: the main group and the
 *  binding-array group (Bevy `layout_entries`,
 *  `render/mesh_view_bindings.rs:221-442`).
 *
 *  The order, the binding numbers and the visibility of every entry are Bevy's:
 *  bindings 0 to 17 of the main group are always present, 18 and 19 are the
 *  tonemapping LUTs, 20 to 23 the prepass textures the key asks for, 24 and 25
 *  the view transmission texture, 26 to 28 the OIT buffers when the key asks
 *  for them and the platform supports them, and 29 to 31 the atmosphere LUTs and
 *  data buffer when the key asks for them. The binding-array group always has
 *  the environment maps at 0 to 2, the irradiance volumes at 3 and 4 when they
 *  are usable, and the clustered decals at 5 to 7 when they are.
 *
 *  Bevy reads the device and adapter here; the facts they imply are parameters,
 *  the substitution the rest of the port records. Two gates have no counterpart:
 *  Bevy's webgl-vs-native prepass condition is always taken on the platforms Epix
 *  builds for, and `IRRADIANCE_VOLUMES_ARE_USABLE` is a compile-time constant. */
EPIX_EXPORT inline std::array<std::vector<wgpu::BindGroupLayoutEntry>, 2> mesh_view_layout_entries(
    wgpu::BufferBindingType clustered_forward_buffer_binding_type,
    wgpu::BufferBindingType visibility_ranges_buffer_binding_type,
    const MeshPipelineViewLayoutKey& layout_key,
    bool binding_arrays_are_usable,
    bool decals_feature_enabled,
    bool partially_bound_binding_array_supported,
    bool oit_supported) {
    using render::render_resource::DynamicBindGroupLayoutEntries;
    using namespace render::render_resource::binding_types;

    // EnvironmentMapLight
    const auto environment_map_entries = environment_map_bind_group_layout_entries(binding_arrays_are_usable);

    auto entries = DynamicBindGroupLayoutEntries::new_with_indices(
        wgpu::ShaderStage::eFragment,
        // View
        std::pair{0u, uniform_buffer(true, sizeof(render::view::ViewUniform))
                          .visibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)},
        // Lights
        std::pair{1u, uniform_buffer(true, sizeof(GpuLights))},
        // Point Shadow Texture Cube Array
        std::pair{2u, texture_cube_array(wgpu::TextureSampleType::eDepth)},
        // Point Shadow Texture Array Comparison Sampler
        std::pair{3u, sampler(wgpu::SamplerBindingType::eComparison)},
        // Directional Shadow Texture Array
        std::pair{5u, texture_2d_array(wgpu::TextureSampleType::eDepth)},
        // Directional Shadow Texture Array Comparison Sampler
        std::pair{6u, sampler(wgpu::SamplerBindingType::eComparison)},
        // PointLights
        std::pair{8u, buffer_layout(clustered_forward_buffer_binding_type, false,
                                    GpuClusterableObjects::min_size(clustered_forward_buffer_binding_type))},
        // ClusteredLightIndexLists
        std::pair{9u, buffer_layout(clustered_forward_buffer_binding_type, false,
                                    ViewClusterBindings::min_size_clusterable_object_index_lists(
                                        clustered_forward_buffer_binding_type))},
        // ClusterOffsetsAndCounts
        std::pair{10u, buffer_layout(clustered_forward_buffer_binding_type, false,
                                     ViewClusterBindings::min_size_cluster_offsets_and_counts(
                                         clustered_forward_buffer_binding_type))},
        // Globals
        std::pair{11u, uniform_buffer(false, sizeof(render::GlobalsUniform))
                           .visibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)},
        // Fog
        std::pair{12u, uniform_buffer(true, sizeof(GpuFog))},
        // Light probes
        std::pair{13u, uniform_buffer(true, sizeof(LightProbesUniform))},
        // Visibility ranges
        std::pair{14u, buffer_layout(visibility_ranges_buffer_binding_type, false, sizeof(glm::vec4))
                           .visibility(wgpu::ShaderStage::eVertex)},
        // Screen space reflection settings
        std::pair{15u, uniform_buffer(true, sizeof(ScreenSpaceReflectionsUniform))},
        // Screen space ambient occlusion texture
        std::pair{16u, texture_2d(wgpu::TextureSampleType::eUnfilterableFloat)},
        std::pair{17u, environment_map_entries[3]});

    // Tonemapping
    const auto tonemapping_lut_entries = core_graph::get_lut_bind_group_layout_entries();
    entries.extend_with_indices(std::pair{18u, tonemapping_lut_entries[0]}, std::pair{19u, tonemapping_lut_entries[1]});

    // Prepass: the four entries the key asks for, at bindings 20 to 23.
    const auto prepass_entries = prepass_bind_group_layout_entries(layout_key);
    constexpr std::array<std::uint32_t, 4> prepass_bindings{20u, 21u, 22u, 23u};
    for (std::size_t index = 0; index < prepass_entries.size(); ++index) {
        if (prepass_entries[index]) {
            entries.extend_with_indices(std::pair{prepass_bindings[index], *prepass_entries[index]});
        }
    }

    // View Transmission Texture
    entries.extend_with_indices(std::pair{24u, texture_2d(wgpu::TextureSampleType::eFloat)},
                                std::pair{25u, sampler(wgpu::SamplerBindingType::eFiltering)});

    // OIT: the two layer buffers and the settings uniform.
    if (layout_key.contains(MeshPipelineViewLayoutKey::OIT_ENABLED) && oit_supported) {
        entries.extend_with_indices(
            // oit_layers
            std::pair{26u, storage_buffer(false, 0)},
            // oit_layer_ids
            std::pair{27u, storage_buffer(false, 0)},
            // oit_layer_count
            std::pair{28u, uniform_buffer(true, sizeof(core_graph::OrderIndependentTransparencySettings))});
    }

    // Atmosphere
    if (layout_key.contains(MeshPipelineViewLayoutKey::ATMOSPHERE)) {
        entries.extend_with_indices(
            // transmittance LUT
            std::pair{29u, texture_2d(wgpu::TextureSampleType::eFloat)},
            std::pair{30u, sampler(wgpu::SamplerBindingType::eFiltering)},
            // atmosphere data buffer
            std::pair{31u, storage_buffer_read_only(false, sizeof(AtmosphereData))});
    }

    auto binding_array_entries = DynamicBindGroupLayoutEntries::new_with_indices(
        wgpu::ShaderStage::eFragment, std::pair{0u, environment_map_entries[0]},
        std::pair{1u, environment_map_entries[1]}, std::pair{2u, environment_map_entries[2]});

    // Irradiance volumes
    if (IRRADIANCE_VOLUMES_ARE_USABLE) {
        const auto irradiance_volume_entries = irradiance_volume_bind_group_layout_entries(binding_arrays_are_usable);
        binding_array_entries.extend_with_indices(std::pair{3u, irradiance_volume_entries[0]},
                                                  std::pair{4u, irradiance_volume_entries[1]});
    }

    // Clustered decals
    if (const auto clustered_decal_entries = clustered_decal_bind_group_layout_entries(
            binding_arrays_are_usable, decals_feature_enabled, partially_bound_binding_array_supported)) {
        binding_array_entries.extend_with_indices(std::pair{5u, (*clustered_decal_entries)[0]},
                                                  std::pair{6u, (*clustered_decal_entries)[1]},
                                                  std::pair{7u, (*clustered_decal_entries)[2]});
    }

    return {std::vector<wgpu::BindGroupLayoutEntry>(entries.begin(), entries.end()),
            std::vector<wgpu::BindGroupLayoutEntry>(binding_array_entries.begin(), binding_array_entries.end())};
}

/** @brief The three layouts one view key needs (Bevy `MeshPipelineViewLayout`,
 *  `render/mesh_view_bindings.rs:61-69`).
 *
 *  Bevy's debug-only `texture_count` is kept under `NDEBUG`, which is what
 *  `assert` and `debug_assert` use here: it counts the entries whose texture
 *  layout names a view dimension, and every entry in a view layout comes from a
 *  default-initialised descriptor, so a non-texture entry leaves it undefined. */
EPIX_EXPORT struct MeshPipelineViewLayout {
    render::BindGroupLayoutDescriptor main_layout;
    render::BindGroupLayoutDescriptor binding_array_layout;
    render::BindGroupLayoutDescriptor empty_layout;
#ifndef NDEBUG
    std::size_t texture_count = 0;
#endif
};

/** @brief Every layout the mesh pipeline can ask for, one per combination of
 *  view-layout key flags (Bevy `generate_view_layouts`,
 *  `render/mesh_view_bindings.rs:516-554`).
 *
 *  The layouts are descriptors, not device objects, so this needs no device: the
 *  facts Bevy reads off the device and adapter are parameters. */
EPIX_EXPORT inline std::array<MeshPipelineViewLayout, MeshPipelineViewLayoutKey::COUNT> generate_view_layouts(
    wgpu::BufferBindingType clustered_forward_buffer_binding_type,
    wgpu::BufferBindingType visibility_ranges_buffer_binding_type,
    bool binding_arrays_are_usable,
    bool decals_feature_enabled,
    bool partially_bound_binding_array_supported,
    bool oit_supported) {
    std::array<MeshPipelineViewLayout, MeshPipelineViewLayoutKey::COUNT> layouts{};
    for (std::uint32_t bits = 0; bits < MeshPipelineViewLayoutKey::COUNT; ++bits) {
        const auto key     = MeshPipelineViewLayoutKey::from_bits_retain(bits);
        const auto entries = mesh_view_layout_entries(
            clustered_forward_buffer_binding_type, visibility_ranges_buffer_binding_type, key,
            binding_arrays_are_usable, decals_feature_enabled, partially_bound_binding_array_supported, oit_supported);
#ifndef NDEBUG
        std::size_t texture_count = 0;
        for (const auto& list : entries) {
            for (const auto& entry : list) {
                if (entry.texture.viewDimension != wgpu::TextureViewDimension::eUndefined) ++texture_count;
            }
        }
#endif
        layouts[bits] = MeshPipelineViewLayout{
            .main_layout = render::BindGroupLayoutDescriptor::create(key.label(), entries[0]),
            .binding_array_layout =
                render::BindGroupLayoutDescriptor::create(key.label() + "_binding_array", entries[1]),
            .empty_layout = render::BindGroupLayoutDescriptor::create(key.label() + "_empty",
                                                                      std::vector<wgpu::BindGroupLayoutEntry>{}),
#ifndef NDEBUG
            .texture_count = texture_count,
#endif
        };
    }
    return layouts;
}

/** @brief The view layouts, indexed by key (Bevy `MeshPipelineViewLayouts`,
 *  `render/mesh_view_bindings.rs:444-514`).
 *
 *  Bevy wraps the array in an `Arc` so it can be cloned into specializable
 *  pipelines; Epix keeps the array by value. Bevy's `FromWorld` reads the
 *  device and adapter, so the port's counterpart takes the same facts and
 *  delegates to `generate_view_layouts`. */
EPIX_EXPORT struct MeshPipelineViewLayouts {
    std::array<MeshPipelineViewLayout, MeshPipelineViewLayoutKey::COUNT> layouts;

    /** @brief Build every layout for a device with the given facts (Bevy
     *  `impl FromWorld for MeshPipelineViewLayouts`). */
    static MeshPipelineViewLayouts make(wgpu::BufferBindingType clustered_forward_buffer_binding_type,
                                        wgpu::BufferBindingType visibility_ranges_buffer_binding_type,
                                        bool binding_arrays_are_usable,
                                        bool decals_feature_enabled,
                                        bool partially_bound_binding_array_supported,
                                        bool oit_supported) {
        return MeshPipelineViewLayouts{generate_view_layouts(
            clustered_forward_buffer_binding_type, visibility_ranges_buffer_binding_type, binding_arrays_are_usable,
            decals_feature_enabled, partially_bound_binding_array_supported, oit_supported)};
    }

    /** @brief The layout for a key (Bevy `MeshPipelineViewLayouts::get_view_layout`),
     *  warning once in debug builds when a layout carries too many textures. */
    const MeshPipelineViewLayout& get_view_layout(const MeshPipelineViewLayoutKey& layout_key) const {
        const MeshPipelineViewLayout& layout = layouts.at(layout_key.bits());
#ifndef NDEBUG
        if (layout.texture_count > MESH_PIPELINE_VIEW_LAYOUT_SAFE_MAX_TEXTURES) {
            static const bool warned = [] {
                spdlog::warn(
                    "[pbr] Too many textures in mesh pipeline view layout, this might cause us to hit "
                    "`wgpu::Limits::max_sampled_textures_per_shader_stage` in some environments.");
                return true;
            }();
            (void)warned;
        }
#endif
        return layout;
    }
};

/** @brief The three view bind groups a view renders with (Bevy
 *  `MeshViewBindGroup`, `render/mesh_view_bindings.rs:556-561`).
 *
 *  Which one a draw uses depends on the pipeline's binding mode, which is why all
 *  three exist up front: `main` binds the view's own resources, `binding_array`
 *  binds the same ones as arrays where the platform allows them, and `empty`
 *  exists so a pass that needs no view bindings still has a group of the right
 *  layout to bind rather than a hole.
 *
 *  It holds created `BindGroup`s, so it is built where the device and the pipeline
 *  cache are - `prepare_mesh_view_bind_groups` (`PBR-302`) - and not before. */
EPIX_EXPORT struct MeshViewBindGroup {
    wgpu::BindGroup main;
    wgpu::BindGroup binding_array;
    wgpu::BindGroup empty;
};

}  // namespace epix::pbr
