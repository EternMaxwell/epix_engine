#include <epix/pbr/light_meta.hpp>

namespace epix::pbr {

AmbientLight resolve_ambient_light(const AmbientLight* camera_override,
                                                      const GlobalAmbientLight& global) {
    if (camera_override != nullptr) return *camera_override;
    return AmbientLight{.color                      = global.color,
                        .brightness                 = global.brightness,
                        .affects_lightmapped_meshes = global.affects_lightmapped_meshes};
}

std::uint32_t point_light_shadow_texture_layers(std::size_t point_light_shadow_maps_count) {
    return static_cast<std::uint32_t>(std::max<std::size_t>(point_light_shadow_maps_count, 1) *
                                      POINT_LIGHT_CUBE_FACE_COUNT);
}

std::uint32_t directional_light_shadow_texture_layers(std::size_t num_directional_cascades_enabled,
                                                                         std::size_t spot_light_shadow_maps_count) {
    return static_cast<std::uint32_t>(
        std::max<std::size_t>(num_directional_cascades_enabled + spot_light_shadow_maps_count, 1));
}

std::uint32_t directional_light_shadow_texture_size(std::size_t shadow_map_size,
                                                                       std::size_t max_texture_dimension_2d) {
    return static_cast<std::uint32_t>(std::min(shadow_map_size, max_texture_dimension_2d));
}

std::size_t point_light_shadow_view_count(std::size_t point_light_count,
                                                             std::size_t max_texture_cubes) {
    return std::min(point_light_count, max_texture_cubes);
}

LightCounts count_lights(const std::vector<ExtractedPointLight>& point_lights,
                                            const std::vector<ExtractedDirectionalLight>& directional_lights,
                                            std::size_t max_texture_array_layers,
                                            std::size_t max_texture_cubes) {
    LightCounts counts;

    const auto is_spot = [](const ExtractedPointLight& light) { return light.spot_light_angles.has_value(); };
    const auto capped  = [](std::size_t value, std::size_t cap) { return std::min(value, cap); };

    std::size_t points           = 0;
    std::size_t point_volumetric = 0;
    std::size_t point_shadows    = 0;
    std::size_t spots            = 0;
    std::size_t spot_volumetric  = 0;
    std::size_t spot_shadows     = 0;
    for (const ExtractedPointLight& light : point_lights) {
        if (is_spot(light)) {
            ++spots;
            if (light.volumetric) ++spot_volumetric;
            if (light.shadows_enabled) ++spot_shadows;
        } else {
            ++points;
            if (light.volumetric) ++point_volumetric;
            if (light.shadows_enabled) ++point_shadows;
        }
    }

    const std::size_t directional_limit = max_texture_array_layers / MAX_CASCADES_PER_LIGHT;
    std::size_t directional_volumetric  = 0;
    std::size_t directional_shadows     = 0;
    for (std::size_t i = 0; i < std::min(directional_lights.size(), MAX_DIRECTIONAL_LIGHTS); ++i) {
        if (directional_lights[i].volumetric) ++directional_volumetric;
        if (directional_lights[i].shadows_enabled) ++directional_shadows;
    }

    counts.point_lights                   = points;
    counts.point_light_volumetric_enabled = capped(point_volumetric, max_texture_cubes);
    counts.point_light_shadow_maps        = capped(point_shadows, max_texture_cubes);
    counts.directional_volumetric_enabled = capped(directional_volumetric, directional_limit);
    counts.directional_shadow_enabled     = capped(directional_shadows, directional_limit);
    // Spot lights share the texture array with the directional shadow maps.
    const std::size_t spot_limit =
        max_texture_array_layers - counts.directional_shadow_enabled * MAX_CASCADES_PER_LIGHT;
    counts.spot_lights                   = capped(spots, spot_limit);
    counts.spot_light_volumetric_enabled = capped(spot_volumetric, spot_limit);
    counts.spot_light_shadow_maps        = capped(spot_shadows, spot_limit);
    return counts;
}

ClusterableObjectType point_or_spot_light_to_clusterable(const ExtractedPointLight& light) {
    if (light.spot_light_angles.has_value()) {
        return ClusterableObjectType{.kind            = ClusterableObjectType::Kind::SpotLight,
                                     .shadows_enabled = light.shadows_enabled,
                                     .volumetric      = light.volumetric,
                                     .outer_angle     = light.spot_light_angles->second};
    }
    return ClusterableObjectType{.kind            = ClusterableObjectType::Kind::PointLight,
                                 .shadows_enabled = light.shadows_enabled,
                                 .volumetric      = light.volumetric};
}

std::tuple<std::uint8_t, bool, bool, ecs::Entity> point_light_sort_key(
    ecs::Entity entity, const ExtractedPointLight& light) {
    const auto ordering = point_or_spot_light_to_clusterable(light).ordering();
    return {std::get<0>(ordering), std::get<1>(ordering), std::get<2>(ordering), entity};
}

std::tuple<bool, bool, ecs::Entity> directional_light_sort_key(
    ecs::Entity entity, const ExtractedDirectionalLight& light) {
    return {light.volumetric, light.shadows_enabled, entity};
};

std::pair<glm::vec4, float> spot_light_custom_data(float inner_angle,
                                                                      float outer_angle,
                                                                      glm::vec3 light_direction) {
    const float cos_outer   = std::cos(outer_angle);
    const float spot_scale  = 1.0f / std::max(std::cos(inner_angle) - cos_outer, 1e-4f);
    const float spot_offset = -cos_outer * spot_scale;
    return {glm::vec4(light_direction.x, light_direction.z, spot_scale, spot_offset), std::tan(outer_angle)};
}

glm::vec4 point_light_custom_data(const glm::mat4& cube_face_projection) {
    return glm::vec4(cube_face_projection[2][2], cube_face_projection[2][3], cube_face_projection[3][2],
                     cube_face_projection[3][3]);
}

glm::vec4 pack_light_color_intensity_range(glm::vec4 color, float intensity, float range) {
    return glm::vec4(glm::vec3(color * intensity), 1.0f / (range * range));
}

std::uint32_t point_light_gpu_flags(std::size_t index,
                                                       const ExtractedPointLight& light,
                                                       const LightCounts& counts) {
    std::uint32_t flags        = PointLightFlags::NONE;
    const bool is_spot         = light.spot_light_angles.has_value();
    const std::size_t in_block = is_spot ? index - counts.point_lights : index;

    if (light.shadows_enabled &&
        (index < counts.point_light_shadow_maps || (is_spot && in_block < counts.spot_light_shadow_maps))) {
        flags |= PointLightFlags::SHADOWS_ENABLED;
    }
    if (light.shadows_enabled && light.volumetric &&
        (index < counts.point_light_volumetric_enabled ||
         (is_spot && in_block < counts.spot_light_volumetric_enabled))) {
        flags |= PointLightFlags::VOLUMETRIC;
    }
    if (light.affects_lightmapped_mesh_diffuse) {
        flags |= PointLightFlags::AFFECTS_LIGHTMAPPED_MESH_DIFFUSE;
    }
    return flags;
}

std::size_t num_directional_cascades_enabled(
    const std::vector<camera::RenderLayers>& view_render_layers,
    const std::vector<ExtractedDirectionalLight>& directional_lights,
    std::size_t max_texture_array_layers) {
    std::size_t enabled = 0;
    for (const camera::RenderLayers& view_layers : view_render_layers) {
        std::size_t for_this_view = 0;
        for (const ExtractedDirectionalLight& light : directional_lights) {
            if (!light.shadows_enabled) continue;
            if (!light.render_layers.intersects(view_layers)) continue;
            for_this_view += std::min(light.cascade_shadow_config.bounds.size(), MAX_CASCADES_PER_LIGHT);
        }
        enabled = std::max(enabled, for_this_view);
    }
    return std::min(enabled, max_texture_array_layers);
}

glm::vec4 pack_ambient_color(glm::vec4 linear_color, float brightness) {
    return linear_color * brightness;
}

glm::vec4 pack_cluster_factors(glm::uvec3 cluster_dimensions,
                                                  glm::vec2 viewport_size,
                                                  glm::vec2 log_factors) {
    return glm::vec4(static_cast<float>(cluster_dimensions.x) / viewport_size.x,
                     static_cast<float>(cluster_dimensions.y) / viewport_size.y, log_factors.x, log_factors.y);
}

glm::uvec4 pack_cluster_dimensions(glm::uvec3 cluster_dimensions) {
    return glm::uvec4(cluster_dimensions, cluster_dimensions.x * cluster_dimensions.y * cluster_dimensions.z);
}

std::int32_t spot_light_shadowmap_offset(std::size_t num_directional_cascades_enabled,
                                                            std::size_t point_light_count) {
    return static_cast<std::int32_t>(num_directional_cascades_enabled) - static_cast<std::int32_t>(point_light_count);
}

DirectionalLightFlagsAndCascades directional_light_gpu_flags(
    const ExtractedDirectionalLight& light,
    std::size_t index,
    std::size_t directional_volumetric_enabled_count,
    std::size_t cascades_used_so_far,
    std::size_t max_texture_array_layers) {
    DirectionalLightFlagsAndCascades result;

    if (light.volumetric && light.shadows_enabled && index < directional_volumetric_enabled_count) {
        result.flags |= DirectionalLightFlags::VOLUMETRIC;
    }

    if (light.shadows_enabled) {
        const std::size_t cascades = std::min(light.cascade_shadow_config.bounds.size(), MAX_CASCADES_PER_LIGHT);
        // The budget is what the earlier lights in this view left behind.
        if (cascades_used_so_far + cascades <= max_texture_array_layers) {
            result.flags |= DirectionalLightFlags::SHADOWS_ENABLED;
            result.num_cascades = static_cast<std::uint32_t>(cascades);
        }
    }

    if (light.affects_lightmapped_mesh_diffuse) {
        result.flags |= DirectionalLightFlags::AFFECTS_LIGHTMAPPED_MESH_DIFFUSE;
    }
    return result;
}

glm::mat4 cube_face_projection(float shadow_map_near_z) {
    return camera::perspective_infinite_reverse_rh(std::numbers::pi_v<float> / 2.0f, 1.0f, shadow_map_near_z);
}

std::vector<DirectionalShadowSlice> directional_shadow_slices(
    const std::vector<ExtractedDirectionalLight>& directional_lights,
    const camera::RenderLayers& view_layers,
    std::size_t directional_volumetric_enabled_count,
    std::size_t max_texture_array_layers,
    std::size_t* out_total_cascades) {
    std::vector<DirectionalShadowSlice> slices;
    slices.reserve(std::min(directional_lights.size(), MAX_DIRECTIONAL_LIGHTS));

    std::uint32_t base = 0;
    std::size_t index  = 0;
    for (const ExtractedDirectionalLight& light : directional_lights) {
        if (slices.size() >= MAX_DIRECTIONAL_LIGHTS) break;
        if (!light.render_layers.intersects(view_layers)) continue;

        const auto flags    = directional_light_gpu_flags(light, index, directional_volumetric_enabled_count, base,
                                                          max_texture_array_layers);
        const bool shadowed = (flags.flags & DirectionalLightFlags::SHADOWS_ENABLED) != 0u;
        slices.push_back(DirectionalShadowSlice{
            .light_index = index, .base_index = shadowed ? base : 0u, .num_cascades = flags.num_cascades});
        if (shadowed) base += flags.num_cascades;
        ++index;
    }

    if (out_total_cascades != nullptr) *out_total_cascades = base;
    return slices;
}

}  // namespace epix::pbr
