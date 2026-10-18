#include <algorithm>

#include <epix/core_graph/core3d.hpp>
#include <epix/ecs.hpp>
#include <epix/pbr/cluster.hpp>
#include <epix/pbr/directional_light.hpp>
#include <epix/pbr/gpu_lights.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/render/render_resource.hpp>

namespace epix::pbr {

/** @brief Create the shadow textures and views the light views render into, and return them
 *  as one view's bindings (Bevy `prepare_lights`' head, `bevy_pbr/src/render/light.rs:817-1113`,
 *  and the per-view insertion at `:1633-1641`).
 *
 *  **This is the first half of a function Bevy writes in one piece.** What exists here is both
 *  shadow textures - the point lights' cubemap array and the directional lights' cascade array,
 *  each with its view - returned as a `ViewShadowBindings` for `prepare_lights_shadow_bindings`
 *  below to stamp onto every view. That is what `prepare_mesh_view_bind_groups` (PBR-83) reads
 *  and binds at 2 and 5, so the port needs these textures bound before it can build a mesh view
 *  bind group at all. Still owed, and `PBR-778`: the two `DepthAttachment` maps and the
 *  per-view light-view spawning, so nothing is cast into these textures yet - they are sized
 *  and bound, which is what the bind group needs and all it needs.
 *
 *  Two substitutions, both recorded elsewhere and both forced by this port's shape: the
 *  device limit Bevy reads as `max_texture_cubes` arrives as a parameter, as every other
 *  device limit here does, and the `wgpu` types are built through the wrapper's chained
 *  setters rather than a Rust struct literal. The webgl and simulator fallbacks Bevy
 *  carries for the view dimension do not apply to this port's targets. */
ViewShadowBindings prepare_lights(const wgpu::Device& device,
                                  ::epix::render::render_resource::TextureCache& texture_cache,
                                  const PointLightShadowMap& point_light_shadow_map,
                                  std::size_t max_texture_cubes, std::size_t max_texture_array_layers,
                                  std::size_t max_texture_dimension_2d,
                                  const DirectionalLightShadowMap& directional_light_shadow_map,
                                  ecs::Query<ecs::Item<const ExtractedPointLight&>> point_lights,
                                  ecs::Query<ecs::Item<const ExtractedDirectionalLight&>> directional_lights) {
    // The counts come from this port's own helper rather than a second copy of Bevy's
    // arithmetic. `count_lights` already ports `light.rs:806-853`, including the
    // shared-texture-array subtraction the spot counts need, and it counts more than this
    // function alone would - the volumetric and non-shadowing lights too. **Batch 223: this
    // replaced a hand-written copy of that arithmetic.** The lesson is the tracker's own
    // preamble read the other way round: check the port before porting from Bevy, not just
    // after.
    //
    // It takes vectors where Bevy iterates, so the queries are collected here. That is the
    // one shape difference, and it buys exactly one implementation of this arithmetic.
    std::vector<ExtractedPointLight> point_light_values;
    for (auto&& [light] : point_lights.iter()) {
        point_light_values.push_back(light);
    }
    std::vector<ExtractedDirectionalLight> directional_light_values;
    for (auto&& [light] : directional_lights.iter()) {
        directional_light_values.push_back(light);
    }

    const LightCounts counts = count_lights(point_light_values, directional_light_values,
                                            max_texture_array_layers, max_texture_cubes);
    const std::size_t point_light_shadow_maps_count = counts.point_light_shadow_maps;
    const std::size_t spot_light_shadow_maps_count  = counts.spot_light_shadow_maps;

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

    // Bevy hands each view these four handles inside `prepare_lights` (`light.rs:1633-1641`);
    // returning them is what lets the system below do the stamping. Still owed, in Bevy's
    // order: the two `DepthAttachment` maps (keyed by array layer, so they belong beside the
    // per-layer cache) and the per-view spawning of `PBR-778`, so nothing is cast into these
    // textures yet - they are sized and bound, which is what the mesh view bind group needs.
    return ViewShadowBindings{
        .point_light_depth_texture            = point_light_depth_texture.texture,
        .point_light_depth_texture_view       = point_light_depth_texture_view,
        .directional_light_depth_texture      = directional_light_depth_texture.texture,
        .directional_light_depth_texture_view = directional_light_depth_texture_view};
}

void prepare_lights_shadow_bindings(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    ecs::ResMut<::epix::render::render_resource::TextureCache> texture_cache,
    ecs::Res<PointLightShadowMap> point_light_shadow_map,
    ecs::Res<DirectionalLightShadowMap> directional_light_shadow_map,
    ecs::Query<ecs::Item<const ExtractedPointLight&>> point_lights,
    ecs::Query<ecs::Item<const ExtractedDirectionalLight&>> directional_lights,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::render::view::ExtractedView&>> views) {
    // Bevy creates the shadow textures and stamps every view with them inside one
    // `prepare_lights`; this port keeps the creation in the helper above and stamps here, so
    // each view gets the `ViewShadowBindings` that `prepare_mesh_view_bind_groups` (PBR-83)
    // reads and binds at 2 and 5.
    wgpu::Limits limits{};
    render_device->getLimits(&limits);
    const std::size_t max_texture_array_layers = static_cast<std::size_t>(limits.maxTextureArrayLayers);
    const std::size_t max_texture_cubes        = max_texture_array_layers / 6;
    const ViewShadowBindings bindings =
        prepare_lights(*render_device, *texture_cache, *point_light_shadow_map, max_texture_cubes,
                       max_texture_array_layers, static_cast<std::size_t>(limits.maxTextureDimension2D),
                       *directional_light_shadow_map, point_lights, directional_lights);
    for (auto&& [entity, view] : views.iter()) {
        (void)view;
        commands.entity(entity).insert(bindings);
    }
}

/** @brief Despawn a list of entities through commands (Bevy `despawn_entities`,
 *  `render/light.rs:1673-1682`).
 *
 *  Bevy queues one world closure for the whole list and returns early when the list is
 *  empty. This port commands each entity directly: its `Commands` applies at the next sync
 *  point, which is where Bevy's closure runs, and it has no queued world access to use
 *  instead. The empty-list guard is kept because it is Bevy's, and because it is what lets
 *  the three despawn sites in the spawning read as one line each. */
void despawn_entities(ecs::Commands commands, const std::vector<ecs::Entity>& entities) {
    if (entities.empty()) {
        return;
    }
    for (const ecs::Entity& entity : entities) {
        commands.entity(entity).despawn();
    }
}

/** @brief Re-key one view's visible meshes into the render world (Bevy
 *  `create_render_visible_mesh_entities`, `render/light.rs:600-614`).
 *
 *  An entity the sync did not map becomes the default entity, which is Bevy's
 *  `Entity::PLACEHOLDER`. */
namespace {
RenderVisibleMeshEntities create_render_visible_mesh_entities(
    app::Extract<ecs::Query<ecs::Item<const ::epix::render::sync_world::RenderEntity&>>>& mapper,
    const ::epix::camera::VisibleMeshEntities& visible_entities) {
    RenderVisibleMeshEntities rendered;
    rendered.entities.reserve(visible_entities.entities.size());
    for (const ecs::Entity visible : visible_entities.entities) {
        const auto row = mapper.get(visible);
        rendered.entities.emplace_back(row.has_value() ? std::get<0>(*row).id() : ecs::Entity{},
                                       ::epix::render::sync_world::MainEntity(visible));
    }
    return rendered;
}
}  // namespace

void extract_ambient_light_resource(ecs::Commands commands,
                                    app::Extract<ecs::Res<GlobalAmbientLight>> ambient_light) {
    // Bevy `extract_ambient_light_resource` (`render/light.rs:249-267`): the scene-wide ambient
    // light is a main-world resource the render world reads, and this is what puts it there.
    // Bevy guards the copy with `is_changed`; this port copies every frame, which yields the
    // same value each time.
    commands.insert_resource(*ambient_light);
}

void prepare_view_lights(ecs::Commands commands,
                         ecs::Res<wgpu::Device> render_device,
                         ecs::Res<wgpu::Queue> render_queue,
                         ecs::Res<GlobalAmbientLight> ambient_light,
                         ecs::ResMut<LightMeta> light_meta,
                         ecs::Query<ecs::Item<ecs::Entity, const ::epix::render::view::ExtractedView&,
                                              const ExtractedClusterConfig&,
                                              ecs::Opt<const ::epix::camera::RenderLayers&>,
                                              ecs::Opt<const AmbientLight&>>> views,
                         ecs::Query<ecs::Item<const ExtractedPointLight&>> point_lights,
                         ecs::Query<ecs::Item<const ExtractedDirectionalLight&>> directional_lights) {
    // Bevy's `prepare_lights` tail (`render/light.rs:1113-1250` and the write at `:1630-1650`):
    // one `GpuLights` per view, in view order, each view stamped with the offset
    // `SetMeshViewBindGroup<I>` reads at binding 1. Bevy writes this in one function with the
    // shadow-texture head this port keeps unwired in `light_prepare.cpp`, so the tail is
    // registered on its own until that head lands.
    std::vector<ExtractedPointLight> extracted_point_lights;
    for (auto&& [light] : point_lights.iter()) extracted_point_lights.push_back(light);
    std::vector<ExtractedDirectionalLight> extracted_directional_lights;
    for (auto&& [light] : directional_lights.iter()) extracted_directional_lights.push_back(light);

    wgpu::Limits limits{};
    render_device->getLimits(&limits);
    const auto max_texture_array_layers = static_cast<std::size_t>(limits.maxTextureArrayLayers);
    const auto max_texture_cubes        = max_texture_array_layers / 6;
    const LightCounts counts =
        count_lights(extracted_point_lights, extracted_directional_lights, max_texture_array_layers,
                     max_texture_cubes);

    auto& gpu_lights_buffer = light_meta->view_gpu_lights;
    gpu_lights_buffer.clear();
    for (auto&& [entity, view, clusters, view_layers, view_ambient] : views.iter()) {
        const ::epix::camera::RenderLayers& layers =
            view_layers.has_value() ? view_layers->get() : ::epix::camera::RenderLayers::layer(0);

        // The view's directional lights, in the order the extracted list carries them, with the
        // layer filter and the light cap Bevy applies (`light.rs:1163-1169`).
        std::array<GpuDirectionalLight, MAX_DIRECTIONAL_LIGHTS> gpu_directional_lights{};
        std::size_t num_directional_lights = 0;
        std::size_t num_cascades_enabled   = 0;
        for (const ExtractedDirectionalLight& light : extracted_directional_lights) {
            if (num_directional_lights >= MAX_DIRECTIONAL_LIGHTS) break;
            if (!layers.intersects(light.render_layers)) continue;
            const std::size_t index = num_directional_lights;
            ++num_directional_lights;

            const DirectionalLightFlagsAndCascades flags_and_cascades =
                directional_light_gpu_flags(light, index, counts.directional_volumetric_enabled,
                                            num_cascades_enabled, max_texture_array_layers);
            GpuDirectionalLight gpu{};
            gpu.color                       = pack_directional_color(light.color, light.illuminance);
            // Bevy negates the light's forward axis so the shader can use it for N.L directly
            // (`light.rs:1209`).
            gpu.dir_to_light                = -glm::vec3(light.transform[2]);
            gpu.flags                       = flags_and_cascades.flags;
            gpu.soft_shadow_size            = light.soft_shadow_size.value_or(0.0f);
            gpu.shadow_depth_bias           = light.shadow_depth_bias;
            gpu.shadow_normal_bias          = light.shadow_normal_bias;
            gpu.num_cascades                = flags_and_cascades.num_cascades;
            gpu.cascades_overlap_proportion = light.cascade_shadow_config.overlap_proportion;
            gpu.depth_texture_base_index    = static_cast<std::uint32_t>(num_cascades_enabled);
            gpu.decal_index                 = NO_CLUSTERED_DECAL;
            gpu.sun_disk_angular_size       = light.sun_disk_angular_size;
            gpu.sun_disk_intensity          = light.sun_disk_intensity;
            gpu_directional_lights[index]   = gpu;
            num_cascades_enabled += flags_and_cascades.num_cascades;
        }

        const AmbientLight resolved_ambient =
            resolve_ambient_light(view_ambient.has_value() ? &view_ambient->get() : nullptr, *ambient_light);

        // Bevy's cluster factors: xy scale the tile grid to the viewport, and zw are the
        // log-based pair `calculate_cluster_factors` returns (`light.rs:1233-1240`).
        const glm::vec2 cluster_factors_zw = calculate_cluster_factors(
            clusters.near_plane, clusters.far_plane, static_cast<float>(clusters.dimensions.z), false);

        GpuLights gpu_lights{};
        gpu_lights.directional_lights  = gpu_directional_lights;
        gpu_lights.ambient_color       = resolved_ambient.color.linear().vec4() * resolved_ambient.brightness;
        gpu_lights.cluster_dimensions  = glm::uvec4(
            clusters.dimensions.x, clusters.dimensions.y, clusters.dimensions.z,
            clusters.dimensions.x * clusters.dimensions.y * clusters.dimensions.z);
        gpu_lights.cluster_factors = glm::vec4(
            static_cast<float>(clusters.dimensions.x) / static_cast<float>(view.viewport.z),
            static_cast<float>(clusters.dimensions.y) / static_cast<float>(view.viewport.w),
            cluster_factors_zw.x, cluster_factors_zw.y);
        gpu_lights.n_directional_lights = static_cast<std::uint32_t>(num_directional_lights);
        gpu_lights.spot_light_shadowmap_offset =
            spot_light_shadowmap_offset(num_cascades_enabled, counts.point_lights);
        gpu_lights.ambient_light_affects_lightmapped_meshes =
            resolved_ambient.affects_lightmapped_meshes ? 1u : 0u;

        const std::size_t offset = gpu_lights_buffer.push(gpu_lights);
        commands.entity(entity).insert(ViewLightsUniformOffset{.offset = static_cast<std::uint32_t>(offset)});
    }
    gpu_lights_buffer.write_buffer(render_device.get(), render_queue.get());
}

void extract_lights(
    ecs::Commands commands,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const ::epix::render::sync_world::RenderEntity&,
                                     const DirectionalLight&, const ::epix::camera::CascadesVisibleEntities&,
                                     const Cascades&, const CascadeShadowConfig&,
                                     const ::epix::camera::CascadesFrusta&,
                                     const ::epix::transform::GlobalTransform&,
                                     const ::epix::camera::ViewVisibility&,
                                     ecs::Opt<const ::epix::camera::RenderLayers&>,
                                     ecs::Has<::epix::render::experimental::OcclusionCulling>>>>
        directional_lights,
    app::Extract<ecs::Res<PointLightShadowMap>> point_light_shadow_map,
    app::Extract<ecs::Res<DirectionalLightShadowMap>> directional_light_shadow_map,
    app::Extract<ecs::Query<ecs::Item<const ::epix::render::sync_world::RenderEntity&>>> mapper) {
    // Bevy extracts the two shadow-map resources here rather than through
    // `ExtractResourcePlugin`, so the prepare that reads them cannot race the extraction
    // (`render/light.rs:376-382`). Bevy guards its copy with `is_changed`; this port copies
    // every frame, which is the same value each time.
    commands.insert_resource(*point_light_shadow_map);
    commands.insert_resource(*directional_light_shadow_map);

    // Bevy `extract_lights`' directional arm (`render/light.rs:505-599`). The point and spot
    // arms share one `ExtractedPointLight` and are the next slice.
    for (auto&& [main_entity, render_entity, light, visible_entities, cascades, cascade_config, frusta, transform,
                 view_visibility, layers, occlusion_culling] : directional_lights.iter()) {
        const ecs::Entity render_light = render_entity.id();
        if (!view_visibility.get()) {
            // Bevy removes the pair rather than leaving a stale light on an invisible one.
            commands.entity(render_light)
                .remove<ExtractedDirectionalLight, RenderCascadesVisibleEntities>();
            continue;
        }

        // The main world keys these by view; the render world keys them by the view's render
        // entity, and a view the sync did not map is skipped - Bevy breaks out of its loop.
        std::unordered_map<ecs::Entity, std::vector<Cascade>> extracted_cascades;
        for (const auto& [view_entity, view_cascades] : cascades.cascades) {
            const auto row = mapper.get(view_entity);
            if (!row.has_value()) continue;
            extracted_cascades.emplace(std::get<0>(*row).id(), view_cascades);
        }
        std::unordered_map<ecs::Entity, std::vector<::epix::camera::Frustum>> extracted_frusta;
        for (const auto& [view_entity, view_frusta] : frusta.frusta) {
            const auto row = mapper.get(view_entity);
            if (!row.has_value()) continue;
            extracted_frusta.emplace(std::get<0>(*row).id(), view_frusta);
        }
        ::epix::render::sync_world::MainEntityHashMap<std::vector<RenderVisibleMeshEntities>>
            cascade_visible_entities;
        for (const auto& [view_entity, per_cascade] : visible_entities.entities) {
            const auto row = mapper.get(view_entity);
            if (!row.has_value()) continue;
            std::vector<RenderVisibleMeshEntities> rendered;
            rendered.reserve(per_cascade.size());
            for (const ::epix::camera::VisibleMeshEntities& list : per_cascade) {
                rendered.push_back(create_render_visible_mesh_entities(mapper, list));
            }
            cascade_visible_entities.emplace(std::get<0>(*row).id(), std::move(rendered));
        }

        commands.entity(render_light)
            .insert(ExtractedDirectionalLight{
                        .color                            = light.color.linear().vec4(),
                        .illuminance                      = light.illuminance,
                        .transform                        = transform.matrix,
                        .shadows_enabled                  = light.shadows_enabled,
                        .volumetric                       = false,
                        .affects_lightmapped_mesh_diffuse = light.affects_lightmapped_mesh_diffuse,
                        .shadow_depth_bias                = light.shadow_depth_bias,
                        // Bevy scales the normal bias by sqrt(2), for the worst-case
                        // diagonal offset (`light.rs:584`).
                        .shadow_normal_bias = light.shadow_normal_bias * std::numbers::sqrt2_v<float>,
                        .cascade_shadow_config = cascade_config,
                        .cascades              = std::move(extracted_cascades),
                        .frusta                = std::move(extracted_frusta),
                        .render_layers         = layers.has_value() ? layers->get()
                                                                   : ::epix::camera::RenderLayers::layer(0),
                        .soft_shadow_size      = std::nullopt,
                        .occlusion_culling     = occlusion_culling,
                        .sun_disk_angular_size = 0.0f,
                        .sun_disk_intensity    = 0.0f},
                    RenderCascadesVisibleEntities{.entities = std::move(cascade_visible_entities)},
                    ::epix::render::sync_world::MainEntity(main_entity));
    }
}

}  // namespace epix::pbr
