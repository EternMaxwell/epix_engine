#include <algorithm>
#include <cmath>

#include <epix/core_graph/core3d.hpp>
#include <epix/ecs.hpp>
#include <epix/pbr/cluster.hpp>
#include <epix/pbr/directional_light.hpp>
#include <epix/pbr/gpu_lights.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/pbr/shadow_phase.hpp>
#include <epix/render/render_resource.hpp>
#include <unordered_set>
#include <spdlog/spdlog.h>

namespace epix::pbr {

namespace {
struct Point {
    ecs::Entity entity;
    render::sync_world::MainEntity main;
    const ExtractedPointLight* light;
    const camera::CubemapFrusta* point_frusta;
    const camera::Frustum* spot_frustum;
};
struct Directional {
    ecs::Entity entity;
    render::sync_world::MainEntity main;
    const ExtractedDirectionalLight* light;
};

void pack_clusterable_objects(const wgpu::Device& device, const wgpu::Queue& queue,
                              GlobalClusterableObjectMeta& global_light_meta,
                              const std::vector<Point>& sorted_points, const LightCounts& counts) {
    auto& indices     = global_light_meta.entity_to_index;
    indices.clear();
    indices.reserve(sorted_points.size());
    std::vector<GpuClusterableObject> gpu_lights;
    gpu_lights.reserve(sorted_points.size());
    for (const auto& source : sorted_points) {
        const auto entity = source.entity;
        const auto& light = *source.light;
        const auto index = gpu_lights.size();
        glm::vec4 custom_data;
        float tan_angle = 0.0f;
        if (light.spot_light_angles) {
            const auto direction = -glm::normalize(glm::vec3(light.transform[2]));
            std::tie(custom_data, tan_angle) =
                spot_light_custom_data(light.spot_light_angles->first, light.spot_light_angles->second, direction);
        } else {
            custom_data = point_light_custom_data(cube_face_projection(light.shadow_map_near_z));
        }
        gpu_lights.push_back(GpuClusterableObject{
            .light_custom_data          = custom_data,
            .color_inverse_square_range = pack_light_color_intensity_range(light.color, light.intensity, light.range),
            .position_radius            = glm::vec4(glm::vec3(light.transform[3]), light.radius),
            .flags                      = point_light_gpu_flags(index, light, counts),
            .shadow_depth_bias          = light.shadow_depth_bias,
            .shadow_normal_bias         = light.shadow_normal_bias,
            .spot_light_tan_angle       = tan_angle,
            .soft_shadow_size           = light.soft_shadows_enabled ? light.radius : 0.0f,
            .shadow_map_near_z          = light.shadow_map_near_z,
            .decal_index                = NO_CLUSTERED_DECAL,
            .pad                        = 0.0f,
        });
        indices.emplace(entity, index);
    }
    global_light_meta.gpu_clusterable_objects.set(std::move(gpu_lights));
    global_light_meta.gpu_clusterable_objects.write_buffer(device, queue);
}

ViewShadowBindings allocate_shadow_bindings(
    const wgpu::Device& device, render::render_resource::TextureCache& texture_cache,
    const PointLightShadowMap& point_light_shadow_map,
    const DirectionalLightShadowMap& directional_light_shadow_map,
    const LightCounts& counts, std::size_t directional_cascades, std::size_t max_texture_dimension_2d) {
    const auto point_light_shadow_maps_count = counts.point_light_shadow_maps;
    const auto spot_light_shadow_maps_count = counts.spot_light_shadow_maps;

    // Six faces per cubemap, and at least one complete cube even without casters.
    const std::uint32_t point_shadow_texture_layers =
        point_light_shadow_texture_layers(point_light_shadow_maps_count);
    wgpu::TextureDescriptor point_light_depth_texture_descriptor{};
    point_light_depth_texture_descriptor.setLabel("point_light_shadow_map_texture")
        .setSize({static_cast<std::uint32_t>(point_light_shadow_map.size),
                  static_cast<std::uint32_t>(point_light_shadow_map.size),
                  point_shadow_texture_layers})
        .setFormat(::epix::core_graph::core_3d::CORE_3D_DEPTH_FORMAT)
        .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding)
        .setDimension(wgpu::TextureDimension::e2D);
    point_light_depth_texture_descriptor.setMipLevelCount(1).setSampleCount(1);

    // The cache is keyed by hand here because this wrapper's descriptor exposes setters
    // only, so the key restates the fields the descriptor carries.
    const ::epix::render::render_resource::TextureCacheKey point_light_depth_texture_key{
        .format                = ::epix::core_graph::core_3d::CORE_3D_DEPTH_FORMAT,
        .dimension             = wgpu::TextureDimension::e2D,
        .width                 = static_cast<std::uint32_t>(point_light_shadow_map.size),
        .height                = static_cast<std::uint32_t>(point_light_shadow_map.size),
        .depth_or_array_layers = point_shadow_texture_layers,
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
        directional_light_shadow_texture_layers(directional_cascades, spot_light_shadow_maps_count);

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

    // Every sorted camera receives these bindings after its light views are prepared.
    return ViewShadowBindings{
        .point_light_depth_texture            = point_light_depth_texture.texture,
        .point_light_depth_texture_view       = point_light_depth_texture_view,
        .directional_light_depth_texture      = directional_light_depth_texture.texture,
        .directional_light_depth_texture_view = directional_light_depth_texture_view};
}

}  // namespace

/** @brief Queue one fallible World command for the complete list, preserving
 * native retirement order (Bevy despawn_entities, render/light.rs:1673-1682). */
void despawn_entities(ecs::Commands commands, const std::vector<ecs::Entity>& entities) {
    if (entities.empty()) {
        return;
    }
    commands.queue([entities](ecs::World& world) {
        for (const auto entity : entities)
            if (auto value = world.get_entity_mut(entity)) value->despawn();
    });
}

/** @brief Re-key one view's visible meshes into the render world (Bevy
 *  `create_render_visible_mesh_entities`, `render/light.rs:600-614`).
 *
 *  An unmapped mesh keeps its main identity and receives the named placeholder,
 *  not a default-constructed Entity, in its render slot. */
namespace {
RenderVisibleMeshEntities create_render_visible_mesh_entities(
    app::Extract<ecs::Query<ecs::Item<const ::epix::render::sync_world::RenderEntity&>>>& mapper,
    const ::epix::camera::VisibleMeshEntities& visible_entities) {
    RenderVisibleMeshEntities rendered;
    rendered.entities.reserve(visible_entities.entities.size());
    for (const ecs::Entity visible : visible_entities.entities) {
        const auto row = mapper.get(visible);
        rendered.entities.emplace_back(row.has_value() ? std::get<0>(*row).id() : ecs::Entity::PLACEHOLDER,
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

void prepare_lights(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    ecs::Res<wgpu::Queue> render_queue,
    ecs::ResMut<render::render_resource::TextureCache> texture_cache,
    ecs::Res<GlobalAmbientLight> ambient_light,
    ecs::ResMut<LightMeta> light_meta,
    ecs::ResMut<GlobalClusterableObjectMeta> global_light_meta,
    ecs::Res<PointLightShadowMap> point_light_shadow_map,
    ecs::Res<DirectionalLightShadowMap> directional_light_shadow_map,
    ecs::Res<render::camera::SortedCameras> sorted_cameras,
    ecs::Res<render::batching::GpuPreprocessingSupport> gpu_preprocessing_support,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<Shadow>> shadow_render_phases,
    ecs::Query<ecs::Item<ecs::Entity, const render::sync_world::MainEntity&,
                         const render::view::ExtractedView&, const ExtractedClusterConfig&,
                         ecs::Opt<const camera::RenderLayers&>, ecs::Has<render::view::NoIndirectDrawing>,
                         ecs::Opt<const AmbientLight&>>, ecs::With<camera::Camera3d>> views,
    ecs::Query<ecs::Item<ecs::Entity, const render::sync_world::MainEntity&, const ExtractedPointLight&,
                         ecs::Opt<const camera::CubemapFrusta&>, ecs::Opt<const camera::Frustum&>>,
               ecs::Or<ecs::With<camera::CubemapFrusta>, ecs::With<camera::Frustum>>> point_lights,
    ecs::Query<ecs::Item<ecs::Entity, const render::sync_world::MainEntity&,
                         const ExtractedDirectionalLight&>> directional_lights,
    ecs::Query<ecs::Item<ecs::Mut<LightViewEntities>>> light_view_entities,
    ecs::Local<bool> max_directional_lights_warning_emitted,
    ecs::Local<bool> max_cascades_per_light_warning_emitted,
    ecs::Local<std::unordered_set<render::view::RetainedViewEntity>> live_shadow_mapping_lights) {
    const auto view_count = static_cast<std::size_t>(std::ranges::distance(views.iter()));
    auto view_gpu_lights_writer =
        light_meta->view_gpu_lights.get_writer(view_count, *render_device, *render_queue);
    if (!view_gpu_lights_writer) return;

    using RetainedView = render::view::RetainedViewEntity;
    using DepthAttachment = render::render_resource::DepthAttachment;
    using Mode = render::batching::GpuPreprocessingMode;
    std::vector<Point> sorted_points;
    std::vector<ExtractedPointLight> extracted_point_lights;
    for (auto&& [entity, main, light, cubemap, frustum] : point_lights.iter()) {
        sorted_points.push_back({entity, main, &light, cubemap ? &cubemap->get() : nullptr,
                                 frustum ? &frustum->get() : nullptr});
        extracted_point_lights.push_back(light);
    }
    std::vector<Directional> sorted_directional_lights;
    std::vector<ExtractedDirectionalLight> original_directional_values;
    for (auto&& [entity, main, light] : directional_lights.iter()) {
        sorted_directional_lights.push_back({entity, main, &light});
        original_directional_values.push_back(light);
    }

    wgpu::Limits limits{};
    render_device->getLimits(&limits);
    const auto max_texture_array_layers = static_cast<std::size_t>(limits.maxTextureArrayLayers);
    const auto max_texture_cubes        = max_texture_array_layers / 6;
    const LightCounts counts =
        count_lights(extracted_point_lights, original_directional_values, max_texture_array_layers,
                     max_texture_cubes);

    if (!*max_directional_lights_warning_emitted && sorted_directional_lights.size() > MAX_DIRECTIONAL_LIGHTS) {
        spdlog::warn("The amount of directional lights of {} is exceeding the supported limit of {}.",
                     sorted_directional_lights.size(), MAX_DIRECTIONAL_LIGHTS);
        *max_directional_lights_warning_emitted = true;
    }
    if (!*max_cascades_per_light_warning_emitted &&
        std::ranges::any_of(original_directional_values, [](const auto& light) {
            return light.cascade_shadow_config.bounds.size() > MAX_CASCADES_PER_LIGHT;
        })) {
        spdlog::warn("The number of cascades configured for a directional light exceeds the supported limit of {}.",
                     MAX_CASCADES_PER_LIGHT);
        *max_cascades_per_light_warning_emitted = true;
    }
    std::ranges::sort(sorted_points, {}, [](const Point& entry) {
        return point_light_sort_key(entry.entity, *entry.light);
    });
    std::ranges::sort(sorted_directional_lights, {},
                      [](const Directional& entry) { return directional_light_sort_key(entry.entity, *entry.light); });
    std::vector<ExtractedDirectionalLight> extracted_directional_lights;
    for (const auto& entry : sorted_directional_lights) extracted_directional_lights.push_back(*entry.light);
    pack_clusterable_objects(*render_device, *render_queue, *global_light_meta, sorted_points, counts);
    std::vector<camera::RenderLayers> camera_layers;
    for (const auto& camera : sorted_cameras->cameras) {
        const auto row = views.get(camera.entity);
        if (!row) continue;
        const auto& layers = std::get<4>(*row);
        camera_layers.push_back(layers ? layers->get() : camera::RenderLayers::layer(0));
    }
    const auto global_cascade_count =
        num_directional_cascades_enabled(camera_layers, extracted_directional_lights, max_texture_array_layers);
    const auto bindings = allocate_shadow_bindings(*render_device, *texture_cache, *point_light_shadow_map,
        *directional_light_shadow_map, counts, global_cascade_count, limits.maxTextureDimension2D);
    std::unordered_map<std::uint32_t, DepthAttachment> point_attachments;
    std::unordered_map<std::uint32_t, DepthAttachment> spot_attachments;
    std::unordered_set<ecs::Entity> live_views;
    auto& live_phases = *live_shadow_mapping_lights;
    live_phases.clear();
    const auto face_rotations = cube_face_view_rotations();
    const auto retire = [&](LightViewEntities& map, ecs::Entity camera) {
        if (auto it = map.entities.find(camera); it != map.entities.end()) {
            despawn_entities(commands, it->second);
            map.entities.erase(it);
        }
    };
    const auto spawn_views = [&](std::size_t count) {
        std::vector<ecs::Entity> result;
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i) result.push_back(commands.spawn().id());
        return result;
    };
    const auto layer_view = [](const wgpu::Texture& texture, std::uint32_t layer, const char* label) {
        return texture.createView(wgpu::TextureViewDescriptor{}.setLabel(label)
            .setDimension(wgpu::TextureViewDimension::e2D).setAspect(wgpu::TextureAspect::eAll)
            .setBaseMipLevel(0).setBaseArrayLayer(layer).setArrayLayerCount(1));
    };
    for (const auto& camera : sorted_cameras->cameras) {
        auto row = views.get(camera.entity);
        if (!row) continue;
        auto&& [entity, camera_main, view, clusters, view_layers, no_indirect, view_ambient] = *row;
        live_views.insert(entity);
        const auto mode = gpu_preprocessing_support->min(no_indirect ? Mode::PreprocessingOnly : Mode::Culling);
        std::vector<ecs::Entity> view_lights;
        std::vector<ecs::Entity> occlusion_views;
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
            // Transform::back() is local +Z; forward() would reverse N.L.
            gpu.dir_to_light                = glm::normalize(glm::vec3(light.transform[2]));
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
        const glm::vec2 cluster_factors_zw =
            calculate_cluster_factors(clusters.near_plane, clusters.far_plane,
                                      static_cast<float>(clusters.dimensions.z), view.clip_from_view[3][3] == 1.0f);

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
            spot_light_shadowmap_offset(global_cascade_count, counts.point_lights);
        gpu_lights.ambient_light_affects_lightmapped_meshes =
            resolved_ambient.affects_lightmapped_meshes ? 1u : 0u;

        // Point/spot views exist per camera, but each texture layer has one
        // shared attachment and phase. Sorted camera order chooses that phase.
        for (std::size_t light_index = 0; light_index < std::min(counts.point_lights, max_texture_cubes);
             ++light_index) {
            const auto& source = sorted_points[light_index];
            auto map_row = light_view_entities.get(source.entity);
            if (!map_row) continue;
            auto& map = *std::get<0>(*map_row);
            const auto& light = *source.light;
            if (!light.shadows_enabled) { retire(map, entity); continue; }
            if (!source.point_frusta) throw std::logic_error("Point shadow light has no CubemapFrusta");
            auto it = map.entities.find(entity);
            if (it == map.entities.end()) it = map.entities.emplace(entity, spawn_views(6)).first;
            const auto gpu_index = global_light_meta->entity_to_index.at(source.entity);
            for (std::size_t face = 0; face < 6; ++face) {
                const auto layer = point_light_shadow_array_layer(gpu_index, face);
                auto attachment = point_attachments.find(layer);
                const bool first = attachment == point_attachments.end();
                if (first) attachment = point_attachments.emplace(layer, DepthAttachment{
                    layer_view(bindings.point_light_depth_texture, layer, "point_light_shadow_map_texture_view"), 0.0f}).first;
                const RetainedView retained{source.main, camera_main, static_cast<std::uint32_t>(face)};
                const auto shadow = it->second.at(face);
                commands.entity(shadow).insert(
                    ShadowView{attachment->second, point_light_shadow_pass_name(gpu_index, face)},
                    render::view::ExtractedView{
                        .retained_view_entity = retained,
                        .clip_from_view = cube_face_projection(light.shadow_map_near_z),
                        .world_from_view = transform::GlobalTransform{
                            point_light_face_world_from_view(face_rotations, glm::vec3(light.transform[3]), face)},
                        .viewport = {0, 0, static_cast<std::uint32_t>(point_light_shadow_map->size),
                                          static_cast<std::uint32_t>(point_light_shadow_map->size)}},
                    camera::Frustum(source.point_frusta->frusta[face]),
                    LightEntity{.kind = LightEntity::Kind::Point, .light_entity = source.entity, .face_index = face});
                if (mode != Mode::Culling) commands.entity(shadow).insert(render::view::NoIndirectDrawing{});
                view_lights.push_back(shadow);
                if (first) {
                    shadow_render_phases->prepare_for_new_frame(retained, mode);
                    live_phases.insert(retained);
                }
            }
        }
        for (std::size_t light_index = 0; light_index < counts.spot_lights; ++light_index) {
            const auto& source = sorted_points[counts.point_lights + light_index];
            auto map_row = light_view_entities.get(source.entity);
            if (!map_row) continue;
            auto& map = *std::get<0>(*map_row);
            const auto& light = *source.light;
            if (!light.shadows_enabled) { retire(map, entity); continue; }
            if (!source.spot_frustum) throw std::logic_error("Spot shadow light has no Frustum");
            const auto layer = static_cast<std::uint32_t>(global_cascade_count + light_index);
            auto attachment = spot_attachments.find(layer);
            const bool first = attachment == spot_attachments.end();
            if (first) attachment = spot_attachments.emplace(layer, DepthAttachment{
                layer_view(bindings.directional_light_depth_texture, layer, "spot_light_shadow_map_texture_view"), 0.0f}).first;
            auto it = map.entities.find(entity);
            if (it == map.entities.end()) it = map.entities.emplace(entity, spawn_views(1)).first;
            const auto shadow = it->second.at(0);
            const RetainedView retained{source.main, camera_main, 0};
            commands.entity(shadow).insert(
                ShadowView{attachment->second, "shadow_spot_light_" + std::to_string(light_index)},
                render::view::ExtractedView{
                    .retained_view_entity = retained,
                    .clip_from_view = spot_light_clip_from_view(light.spot_light_angles.value().second, light.shadow_map_near_z),
                    .world_from_view = transform::GlobalTransform{
                        spot_light_world_from_view(transform::GlobalTransform{light.transform})},
                    .viewport = {0, 0, static_cast<std::uint32_t>(directional_light_shadow_map->size),
                                      static_cast<std::uint32_t>(directional_light_shadow_map->size)}},
                camera::Frustum(*source.spot_frustum),
                LightEntity{.kind = LightEntity::Kind::Spot, .light_entity = source.entity});
            if (mode != Mode::Culling) commands.entity(shadow).insert(render::view::NoIndirectDrawing{});
            view_lights.push_back(shadow);
            if (first) {
                shadow_render_phases->prepare_for_new_frame(retained, mode);
                live_phases.insert(retained);
            }
        }

        std::size_t directional_index = 0;
        std::uint32_t directional_layer = 0;
        for (const auto& source : sorted_directional_lights) {
            const auto& light = *source.light;
            const bool intersects = layers.intersects(light.render_layers);
            if (intersects && directional_index >= MAX_DIRECTIONAL_LIGHTS) continue;
            auto map_row = light_view_entities.get(source.entity);
            const auto index = directional_index;
            if (intersects) ++directional_index;
            if (!map_row) continue;
            auto& map = *std::get<0>(*map_row);
            if (!intersects || !(gpu_lights.directional_lights[index].flags & DirectionalLightFlags::SHADOWS_ENABLED)) {
                retire(map, entity);
                continue;
            }
            const auto& cascades = light.cascades.at(entity);
            const auto& frusta = light.frusta.at(entity);
            const auto count = std::min({cascades.size(), frusta.size(),
                                        light.cascade_shadow_config.bounds.size(), MAX_CASCADES_PER_LIGHT});
            auto it = map.entities.find(entity);
            if (it == map.entities.end()) it = map.entities.emplace(entity, spawn_views(count)).first;
            if (it->second.size() != count) {
                despawn_entities(commands, it->second);
                it->second = spawn_views(count);
            }
            for (std::size_t cascade_index = 0; cascade_index < count; ++cascade_index) {
                const auto& cascade = cascades[cascade_index];
                gpu_lights.directional_lights[index].cascades[cascade_index] = {
                    .clip_from_world = cascade.clip_from_world, .texel_size = cascade.texel_size,
                    .far_bound = light.cascade_shadow_config.bounds[cascade_index]};
                const auto depth_view = layer_view(bindings.directional_light_depth_texture, directional_layer++,
                                                    "directional_light_shadow_map_array_texture_view");
                const auto shadow = it->second[cascade_index];
                const RetainedView retained{source.main, camera_main, static_cast<std::uint32_t>(cascade_index)};
                commands.entity(shadow).insert(
                    ShadowView{DepthAttachment{depth_view, 0.0f}, directional_shadow_pass_name(index, cascade_index)},
                    render::view::ExtractedView{
                        .retained_view_entity = retained, .clip_from_view = cascade.clip_from_cascade,
                        .world_from_view = transform::GlobalTransform{cascade.world_from_cascade},
                        .clip_from_world = cascade.clip_from_world,
                        .viewport = {0, 0, static_cast<std::uint32_t>(directional_light_shadow_map->size),
                                          static_cast<std::uint32_t>(directional_light_shadow_map->size)}},
                    directional_view_frustum(frusta[cascade_index]),
                    LightEntity{.kind = LightEntity::Kind::Directional, .light_entity = source.entity,
                                .cascade_index = cascade_index});
                if (mode != Mode::Culling) commands.entity(shadow).insert(render::view::NoIndirectDrawing{});
                view_lights.push_back(shadow);
                if (light.occlusion_culling) {
                    commands.entity(shadow).insert(render::experimental::OcclusionCulling{},
                        render::experimental::OcclusionCullingSubview{
                            depth_view, static_cast<std::uint32_t>(directional_light_shadow_map->size)});
                    occlusion_views.push_back(shadow);
                }
                shadow_render_phases->prepare_for_new_frame(retained, mode);
                live_phases.insert(retained);
            }
        }

        const auto offset = view_gpu_lights_writer->write(gpu_lights);
        commands.entity(entity).insert(bindings, ViewLightEntities{std::move(view_lights)},
            ViewLightsUniformOffset{.offset = static_cast<std::uint32_t>(offset)});
        if (!occlusion_views.empty()) commands.entity(entity).insert(
            render::experimental::OcclusionCullingSubviewEntities{std::move(occlusion_views)});
    }
    for (auto&& [map] : light_view_entities.iter()) {
        std::erase_if(map->entities, [&](const auto& entry) {
            if (live_views.contains(entry.first)) return false;
            despawn_entities(commands, entry.second);
            return true;
        });
    }
    std::erase_if(shadow_render_phases->phases, [&](const auto& entry) { return !live_phases.contains(entry.first); });
}

void extract_lights(
    ecs::Commands commands,
    app::Extract<ecs::Res<PointLightShadowMap>> point_light_shadow_map,
    app::Extract<ecs::Res<DirectionalLightShadowMap>> directional_light_shadow_map,
    app::Extract<ecs::Res<GlobalVisibleClusterableObjects>> global_visible_clusterable,
    ecs::Query<ecs::Entity, ecs::Filter<ecs::With<RenderCubemapVisibleEntities>,
                                      ecs::With<ExtractedPointLight>>> previous_point_lights,
    ecs::Query<ecs::Entity, ecs::Filter<ecs::With<RenderVisibleMeshEntities>,
                                      ecs::With<ExtractedPointLight>>> previous_spot_lights,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const ::epix::render::sync_world::RenderEntity&,
                                     const PointLight&, const ::epix::camera::CubemapVisibleEntities&,
                                     const ::epix::transform::GlobalTransform&,
                                     const ::epix::camera::ViewVisibility&, const ::epix::camera::CubemapFrusta&,
                                     ecs::Opt<const VolumetricLight&>>>> point_lights,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const ::epix::render::sync_world::RenderEntity&,
                                     const SpotLight&, const ::epix::camera::VisibleMeshEntities&,
                                     const ::epix::transform::GlobalTransform&,
                                     const ::epix::camera::ViewVisibility&, const ::epix::camera::Frustum&,
                                     ecs::Opt<const VolumetricLight&>>>> spot_lights,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const ::epix::render::sync_world::RenderEntity&,
                                     const DirectionalLight&, const ::epix::camera::CascadesVisibleEntities&,
                                     const Cascades&, const CascadeShadowConfig&,
                                     const ::epix::camera::CascadesFrusta&,
                                     const ::epix::transform::GlobalTransform&,
                                     const ::epix::camera::ViewVisibility&,
                                     ecs::Opt<const ::epix::camera::RenderLayers&>,
                                     ecs::Opt<const VolumetricLight&>,
                                     ecs::Has<::epix::render::experimental::OcclusionCulling>,
                                     ecs::Opt<const SunDisk&>>, ecs::Without<SpotLight>>>
        directional_lights,
    app::Extract<ecs::Query<ecs::Item<const ::epix::render::sync_world::RenderEntity&>>> mapper,
    ecs::Local<std::size_t> previous_point_lights_len,
    ecs::Local<std::size_t> previous_spot_lights_len) {
    // Keep extraction and resource change detection together (Bevy light.rs:346-353).
    if (point_light_shadow_map.is_modified()) commands.insert_resource(*point_light_shadow_map);
    if (directional_light_shadow_map.is_modified()) commands.insert_resource(*directional_light_shadow_map);

    std::vector<std::pair<ecs::Entity, RenderCubemapVisibleEntities>> clear_points;
    for (const auto entity : previous_point_lights.iter()) clear_points.emplace_back(entity, RenderCubemapVisibleEntities{});
    commands.try_insert_batch(std::move(clear_points));
    std::vector<std::pair<ecs::Entity, RenderVisibleMeshEntities>> clear_spots;
    for (const auto entity : previous_spot_lights.iter()) clear_spots.emplace_back(entity, RenderVisibleMeshEntities{});
    commands.try_insert_batch(std::move(clear_spots));

    // Every bundle argument is an owned rvalue: deferred insertion must not retain
    // references to loop-local records or source-world query rows.
    using PointBundle = decltype(ecs::make_bundle(ExtractedPointLight{}, RenderCubemapVisibleEntities{},
        ::epix::camera::CubemapFrusta{}, ::epix::render::sync_world::MainEntity(ecs::Entity::PLACEHOLDER)));
    using SpotBundle = decltype(ecs::make_bundle(ExtractedPointLight{}, RenderVisibleMeshEntities{},
        ::epix::camera::Frustum{}, ::epix::render::sync_world::MainEntity(ecs::Entity::PLACEHOLDER)));
    const float point_texel_size = 2.0f / static_cast<float>(point_light_shadow_map->size);
    std::vector<std::pair<ecs::Entity, PointBundle>> point_values;
    point_values.reserve(*previous_point_lights_len);
    for (const auto entity : global_visible_clusterable->entities) {
        const auto row = point_lights.get(entity);
        if (!row) continue;
        const auto& [main_entity, render_entity, light, visible, transform, visibility, frusta, volumetric] = *row;
        if (!visibility.get()) continue;
        RenderCubemapVisibleEntities rendered;
        for (std::size_t face = 0; face < 6; ++face)
            rendered.get_mut(face) = create_render_visible_mesh_entities(mapper, visible.get(face));
        point_values.emplace_back(render_entity.id(), ecs::make_bundle(
            ExtractedPointLight{
                .color = light.color.linear().vec4(),
                .intensity = light.intensity / (4.0f * std::numbers::pi_v<float>),
                .range = light.range, .radius = light.radius, .transform = transform.matrix,
                .shadows_enabled = light.shadows_enabled,
                .shadow_depth_bias = light.shadow_depth_bias,
                .shadow_normal_bias = light.shadow_normal_bias * point_texel_size * std::numbers::sqrt2_v<float>,
                .shadow_map_near_z = light.shadow_map_near_z, .spot_light_angles = std::nullopt,
                .volumetric = volumetric.has_value(), .soft_shadows_enabled = false,
                .affects_lightmapped_mesh_diffuse = light.affects_lightmapped_mesh_diffuse},
            std::move(rendered), ::epix::camera::CubemapFrusta(frusta),
            ::epix::render::sync_world::MainEntity(main_entity)));
    }
    *previous_point_lights_len = point_values.size();
    commands.try_insert_batch(std::move(point_values));

    std::vector<std::pair<ecs::Entity, SpotBundle>> spot_values;
    spot_values.reserve(*previous_spot_lights_len);
    for (const auto entity : global_visible_clusterable->entities) {
        const auto row = spot_lights.get(entity);
        if (!row) continue;
        const auto& [main_entity, render_entity, light, visible, transform, visibility, frustum, volumetric] = *row;
        if (!visibility.get()) continue;
        const float texel_size = 2.0f * std::tan(light.outer_angle) /
                                 static_cast<float>(directional_light_shadow_map->size);
        spot_values.emplace_back(render_entity.id(), ecs::make_bundle(
            ExtractedPointLight{
                .color = light.color.linear().vec4(),
                .intensity = light.intensity / (4.0f * std::numbers::pi_v<float>),
                .range = light.range, .radius = light.radius, .transform = transform.matrix,
                .shadows_enabled = light.shadows_enabled,
                .shadow_depth_bias = light.shadow_depth_bias,
                .shadow_normal_bias = light.shadow_normal_bias * texel_size * std::numbers::sqrt2_v<float>,
                .shadow_map_near_z = light.shadow_map_near_z,
                .spot_light_angles = std::pair{light.inner_angle, light.outer_angle},
                .volumetric = volumetric.has_value(), .soft_shadows_enabled = false,
                .affects_lightmapped_mesh_diffuse = light.affects_lightmapped_mesh_diffuse},
            create_render_visible_mesh_entities(mapper, visible), ::epix::camera::Frustum(frustum),
            ::epix::render::sync_world::MainEntity(main_entity)));
    }
    *previous_spot_lights_len = spot_values.size();
    commands.try_insert_batch(std::move(spot_values));

    for (auto&& [main_entity, render_entity, light, visible_entities, cascades, cascade_config, frusta, transform,
                 view_visibility, layers, volumetric, occlusion_culling, sun_disk] : directional_lights.iter()) {
        const ecs::Entity render_light = render_entity.id();
        if (!view_visibility.get()) {
            // Bevy removes the pair rather than leaving a stale light on an invisible one.
            commands.entity(render_light)
                .remove<ExtractedDirectionalLight, RenderCascadesVisibleEntities>();
            continue;
        }

        // The main world keys these by view; the render world keys them by the view's render
        // entity. Each map stops at its first unsynced view, as Bevy's loops do.
        std::unordered_map<ecs::Entity, std::vector<Cascade>> extracted_cascades;
        for (const auto& [view_entity, view_cascades] : cascades.cascades) {
            const auto row = mapper.get(view_entity);
            if (!row.has_value()) break;
            extracted_cascades.emplace(std::get<0>(*row).id(), view_cascades);
        }
        std::unordered_map<ecs::Entity, std::vector<::epix::camera::Frustum>> extracted_frusta;
        for (const auto& [view_entity, view_frusta] : frusta.frusta) {
            const auto row = mapper.get(view_entity);
            if (!row.has_value()) break;
            extracted_frusta.emplace(std::get<0>(*row).id(), view_frusta);
        }
        std::unordered_map<ecs::Entity, std::vector<RenderVisibleMeshEntities>> cascade_visible_entities;
        for (const auto& [view_entity, per_cascade] : visible_entities.entities) {
            const auto row = mapper.get(view_entity);
            if (!row.has_value()) break;
            std::vector<RenderVisibleMeshEntities> rendered;
            rendered.reserve(per_cascade.size());
            for (const ::epix::camera::VisibleMeshEntities& list : per_cascade) {
                rendered.push_back(create_render_visible_mesh_entities(mapper, list));
            }
            cascade_visible_entities.emplace(std::get<0>(*row).id(), std::move(rendered));
        }

        const SunDisk& disk = sun_disk.has_value() ? sun_disk->get() : SunDisk::EARTH;
        commands.entity(render_light)
            .insert(ExtractedDirectionalLight{
                        .color                            = light.color.linear().vec4(),
                        .illuminance                      = light.illuminance,
                        .transform                        = transform.matrix,
                        .shadows_enabled                  = light.shadows_enabled,
                        .volumetric                       = volumetric.has_value(),
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
                        .sun_disk_angular_size = disk.angular_size,
                        .sun_disk_intensity    = disk.intensity},
                    RenderCascadesVisibleEntities{.entities = std::move(cascade_visible_entities)},
                    ::epix::render::sync_world::MainEntity(main_entity));
    }
}

}  // namespace epix::pbr
