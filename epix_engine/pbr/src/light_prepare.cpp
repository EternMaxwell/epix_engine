#include <algorithm>

#include <epix/core_graph/core3d.hpp>
#include <epix/ecs.hpp>
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
                    std::size_t max_texture_cubes,
                    ecs::Query<ecs::Item<const ExtractedPointLight&>> point_lights) {
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
