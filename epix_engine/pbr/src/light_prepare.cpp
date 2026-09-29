#include <algorithm>

#include <epix/core_graph/core3d.hpp>
#include <epix/ecs.hpp>
#include <epix/pbr/directional_light.hpp>
#include <epix/pbr/gpu_lights.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/render/render_resource.hpp>

namespace epix::pbr {

/** @brief Create the shadow textures and views the light views render into (Bevy
 *  `prepare_lights`' head, `bevy_pbr/src/render/light.rs:817-1113`).
 *
 *  **This is the first half of a function Bevy writes in one piece, and it is
 *  deliberately unwired.** What exists here is the point-light count and its depth
 *  texture and `CubeArray` view. What does not exist yet: the directional pair and its
 *  three counts, the `DepthAttachment`s, and everything that consumes them - the
 *  per-view light-view spawning, `LightViewEntities` reuse and despawn, and the phase
 *  preparation, which are `PBR-778`. Registering this now would add a system that runs
 *  and does nothing with what it builds, which is the failure the cluster probe caught
 *  three times over; it is registered once the body can run without lying about itself.
 *
 *  Two substitutions, both recorded elsewhere and both forced by this port's shape: the
 *  device limit Bevy reads as `max_texture_cubes` arrives as a parameter, as every other
 *  device limit here does, and the `wgpu` types are built through the wrapper's chained
 *  setters rather than a Rust struct literal. The webgl and simulator fallbacks Bevy
 *  carries for the view dimension do not apply to this port's targets. */
void prepare_lights(const wgpu::Device& device, ::epix::render::render_resource::TextureCache& texture_cache,
                    const PointLightShadowMap& point_light_shadow_map,
                    std::size_t max_texture_cubes, std::size_t max_texture_array_layers,
                    std::size_t max_texture_dimension_2d,
                    const DirectionalLightShadowMap& directional_light_shadow_map,
                    ecs::Query<ecs::Item<const ExtractedPointLight&>> point_lights,
                    ecs::Query<ecs::Item<const ExtractedDirectionalLight&>> directional_lights) {
    // Bevy counts the shadow-casting point lights - those without spot angles, since a
    // spot light is clustered as a directional one - and clamps to the device's cubemap
    // limit.
    std::size_t point_light_shadow_maps_count = 0;
    for (auto&& [light] : point_lights.iter()) {
        if (light.shadows_enabled && !light.spot_light_angles.has_value()) {
            point_light_shadow_maps_count += 1;
        }
    }
    point_light_shadow_maps_count = std::min(point_light_shadow_maps_count, max_texture_cubes);

    // The remaining counts, in Bevy's order (`light.rs:826-853`). Directional lights are
    // counted only up to `MAX_DIRECTIONAL_LIGHTS` and only those casting shadows, and each
    // shadow-casting one reserves `MAX_CASCADES_PER_LIGHT` array layers; the three spot
    // counts take what is left of the array, which is why they all subtract the same
    // directional reservation.
    std::size_t directional_shadow_enabled_count = 0;
    std::size_t taken_directional_lights         = 0;
    for (auto&& [light] : directional_lights.iter()) {
        if (taken_directional_lights >= MAX_DIRECTIONAL_LIGHTS) {
            break;
        }
        taken_directional_lights += 1;
        if (light.shadows_enabled) {
            directional_shadow_enabled_count += 1;
        }
    }
    directional_shadow_enabled_count = std::min(directional_shadow_enabled_count,
                                                max_texture_array_layers / MAX_CASCADES_PER_LIGHT);

    const std::size_t layers_left_for_spots =
        max_texture_array_layers - directional_shadow_enabled_count * MAX_CASCADES_PER_LIGHT;

    std::size_t spot_light_count                    = 0;
    std::size_t spot_light_volumetric_enabled_count = 0;
    std::size_t spot_light_shadow_maps_count        = 0;
    for (auto&& [light] : point_lights.iter()) {
        if (!light.spot_light_angles.has_value()) {
            continue;
        }
        spot_light_count += 1;
        if (light.volumetric) {
            spot_light_volumetric_enabled_count += 1;
        }
        if (light.shadows_enabled) {
            spot_light_shadow_maps_count += 1;
        }
    }
    spot_light_count = std::min(spot_light_count, layers_left_for_spots);
    spot_light_volumetric_enabled_count =
        std::min(spot_light_volumetric_enabled_count, layers_left_for_spots);
    spot_light_shadow_maps_count = std::min(spot_light_shadow_maps_count, layers_left_for_spots);

    // Iterate the views once to find the most cascade shadow maps any one view needs
    // (Bevy `light.rs:979-1010`). Bevy walks the sorted cameras and looks each view up;
    // this port reads the per-view cascades the light already carries instead. Same
    // computation: that map is keyed by the views the cascade builder produced for this
    // light, so it has already applied the render-layers filter Bevy applies inline, and a
    // maximum does not depend on iteration order.
    std::size_t num_directional_cascades_enabled = 0;
    for (auto&& [light] : directional_lights.iter()) {
        if (!light.shadows_enabled) {
            continue;
        }
        for (const auto& [view_entity, cascades] : light.cascades) {
            (void)view_entity;
            num_directional_cascades_enabled =
                std::max(num_directional_cascades_enabled,
                         std::min(cascades.size(), MAX_CASCADES_PER_LIGHT));
        }
    }
    num_directional_cascades_enabled =
        std::min(num_directional_cascades_enabled, max_texture_array_layers);

    // Six faces per cubemap, and at least one layer so the texture is never zero-sized.
    wgpu::TextureDescriptor point_light_depth_texture_descriptor{};
    point_light_depth_texture_descriptor.setLabel("point_light_shadow_map_texture")
        .setSize({static_cast<std::uint32_t>(point_light_shadow_map.size),
                  static_cast<std::uint32_t>(point_light_shadow_map.size),
                  static_cast<std::uint32_t>(point_light_shadow_maps_count) * 6u})
        .setFormat(::epix::core_graph::core_3d::CORE_3D_DEPTH_FORMAT)
        .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding)
        .setDimension(wgpu::TextureDimension::e2D);
    // Bevy leaves the layer count at its default of one only when nothing casts; the
    // `.setSize` above already carries `max(count, 1) * 6`, so the default here is what
    // Bevy's `.max(1)` computes.
    if (point_light_shadow_maps_count == 0) {
        point_light_depth_texture_descriptor.setSize(
            {static_cast<std::uint32_t>(point_light_shadow_map.size),
             static_cast<std::uint32_t>(point_light_shadow_map.size), 6u});
    }
    point_light_depth_texture_descriptor.setMipLevelCount(1).setSampleCount(1);

    // The cache is keyed by hand here because this wrapper's descriptor exposes setters
    // only, so the key restates the fields the descriptor carries.
    const ::epix::render::render_resource::TextureCacheKey point_light_depth_texture_key{
        .format                = ::epix::core_graph::core_3d::CORE_3D_DEPTH_FORMAT,
        .dimension             = wgpu::TextureDimension::e2D,
        .width                 = static_cast<std::uint32_t>(point_light_shadow_map.size),
        .height                = static_cast<std::uint32_t>(point_light_shadow_map.size),
        .depth_or_array_layers = static_cast<std::uint32_t>(point_light_shadow_maps_count) * 6u,
        .mip_level_count       = 1,
        .sample_count          = 1,
        .usage = wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding,
        .label = "point_light_shadow_map_texture",
        .view_formats = {},
    };

    [[maybe_unused]] const ::epix::render::render_resource::CachedTexture point_light_depth_texture =
        texture_cache.get(device, point_light_depth_texture_key,
                          point_light_depth_texture_descriptor);

    // The directional array, whose size and layer count both come from helpers this port
    // already carries (`light_meta.hpp:113-127`), which is also where the reason the two
    // light kinds share one array is written down: a spot light's shadow map index is
    // offset by the cascade count rather than starting at zero.
    const std::uint32_t directional_shadow_texture_size =
        directional_light_shadow_texture_size(directional_light_shadow_map.size, max_texture_dimension_2d);
    const std::uint32_t directional_shadow_texture_layers =
        directional_light_shadow_texture_layers(num_directional_cascades_enabled, spot_light_shadow_maps_count);

    wgpu::TextureDescriptor directional_light_depth_texture_descriptor{};
    directional_light_depth_texture_descriptor.setLabel("directional_light_shadow_map_texture")
        .setSize({directional_shadow_texture_size, directional_shadow_texture_size,
                  directional_shadow_texture_layers})
        .setFormat(::epix::core_graph::core_3d::CORE_3D_DEPTH_FORMAT)
        .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding)
        .setDimension(wgpu::TextureDimension::e2D);
    directional_light_depth_texture_descriptor.setMipLevelCount(1).setSampleCount(1);

    const ::epix::render::render_resource::TextureCacheKey directional_light_depth_texture_key{
        .format                = ::epix::core_graph::core_3d::CORE_3D_DEPTH_FORMAT,
        .dimension             = wgpu::TextureDimension::e2D,
        .width                 = directional_shadow_texture_size,
        .height                = directional_shadow_texture_size,
        .depth_or_array_layers = directional_shadow_texture_layers,
        .mip_level_count       = 1,
        .sample_count          = 1,
        .usage = wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding,
        .label = "directional_light_shadow_map_texture",
        .view_formats = {},
    };

    [[maybe_unused]] const ::epix::render::render_resource::CachedTexture directional_light_depth_texture =
        texture_cache.get(device, directional_light_depth_texture_key,
                          directional_light_depth_texture_descriptor);

    [[maybe_unused]] const wgpu::TextureView directional_light_depth_texture_view =
        directional_light_depth_texture.texture.createView(
            wgpu::TextureViewDescriptor{}
                .setLabel("directional_light_shadow_map_array_texture_view")
                .setDimension(wgpu::TextureViewDimension::e2DArray)
                .setAspect(wgpu::TextureAspect::eDepthOnly)
                .setBaseMipLevel(0)
                .setBaseArrayLayer(0));

    [[maybe_unused]] const wgpu::TextureView point_light_depth_texture_view =
        point_light_depth_texture.texture.createView(
            wgpu::TextureViewDescriptor{}
                .setLabel("point_light_shadow_map_array_texture_view")
                .setDimension(wgpu::TextureViewDimension::eCubeArray)
                .setAspect(wgpu::TextureAspect::eDepthOnly)
                .setBaseMipLevel(0)
                .setBaseArrayLayer(0));

    // Owed, in Bevy's order: the directional-light count, the directional depth texture
    // and its `D2Array` view, the `DepthAttachment`s, and then the per-view spawning of
    // `PBR-778`. The locals above are unused until those land, which is why this function
    // is not registered anywhere yet.
}

}  // namespace epix::pbr
