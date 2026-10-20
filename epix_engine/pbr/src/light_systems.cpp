#include <spdlog/spdlog.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <epix/pbr/light_systems.hpp>
#include <glm/glm.hpp>
#include <limits>
#include <stdexcept>
// `compMax` is a gtx extension, and glm guards those behind this define.
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>

namespace epix::pbr {

void add_clusters(ecs::Commands commands,
                  ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const ClusterConfig&>, const ::epix::camera::Camera&>,
                             ecs::Filter<ecs::Without<Clusters>, ecs::With<::epix::camera::Camera3d>>> cameras) {
    for (auto&& [entity, config, camera] : cameras.iter()) {
        if (!camera.is_active) {
            continue;
        }
        // Bevy's comment, kept because it explains the odd-looking default: these
        // settings do not matter here, since `assign_objects_to_clusters` overwrites
        // them later in the frame.
        const ClusterConfig value = config ? ClusterConfig(config->get()) : ClusterConfig{};
        commands.entity(entity).insert(Clusters{}, value);
    }
}

}  // namespace epix::pbr

namespace epix::pbr {

void validate_shadow_map_size(ecs::ResMut<DirectionalLightShadowMap> shadow_map) {
    // Bevy `validate_shadow_map_size` (directional_light.rs:198-204): only a
    // changed resource is inspected, and a size that is not a power of two is
    // corrected with a warning - unstable cascade positioning is the reason the
    // requirement exists. `0` is not a power of two either, and rounds up to 1.
    if (!shadow_map.is_modified()) return;
    const std::size_t size = shadow_map.get().size;
    if (std::has_single_bit(size)) return;

    // Rust next_power_of_two maps zero to one. Overflow panics in its checked
    // profile and wraps to zero in its unchecked profile; never shift by the
    // type width or let an unsigned doubling loop wrap and run forever.
    const int shift = std::bit_width(size == 0 ? std::size_t{0} : size - 1);
    std::size_t power_of_two;
    if (shift == std::numeric_limits<std::size_t>::digits) {
#ifndef NDEBUG
        throw std::overflow_error("DirectionalLightShadowMap next power of two overflows size_t");
#else
        power_of_two = 0;
#endif
    } else {
        power_of_two = std::size_t{1} << shift;
    }
    spdlog::warn("[pbr] Non-power-of-two DirectionalLightShadowMap sizes are not supported, correcting {} to {}", size,
                 power_of_two);
    shadow_map.get_mut().size = power_of_two;
}

void update_point_light_frusta(ecs::Res<GlobalVisibleClusterableObjects> global_lights,
                               ecs::Query<ecs::Item<ecs::Entity,
                                                    ecs::Ref<::epix::transform::GlobalTransform>,
                                                    ecs::Ref<PointLight>,
                                                    ecs::Mut<::epix::camera::CubemapFrusta>>> lights) {
    const bool global_lights_changed = global_lights.is_modified();
    const auto& clustered            = global_lights.get().entities;

    for (auto&& [entity, transform, point_light, cubemap_frusta] : lights.iter()) {
        const bool light_changed = transform.is_modified() || point_light.is_modified();
        if (!should_update_point_light_frusta(light_changed, global_lights_changed, point_light.get().shadows_enabled,
                                              clustered.contains(entity))) {
            continue;
        }

        // Epix's GlobalTransform is a matrix, so the light's world position is its
        // translation column and its backward axis is the third column - the local
        // +Z of a right-handed transform. The light's scale and rotation are
        // deliberately not used, as in Bevy: Bevy passes `transform.back()`, a unit
        // direction, and the far plane's distance is measured along it, so the
        // column has to be normalised or a scaled light's frusta reach `s * range`.
        const glm::mat4 world_from_light = transform.get().matrix;
        const glm::vec3 translation      = glm::vec3(world_from_light[3]);
        const glm::vec3 backward         = glm::normalize(glm::vec3(world_from_light[2]));

        const auto frusta = point_light_cubemap_frusta(translation, backward, point_light.get().range,
                                                       point_light.get().shadow_map_near_z);
        auto& destination = (*cubemap_frusta).frusta;
        for (std::size_t face = 0; face < frusta.size(); ++face) {
            destination[face] = frusta[face];
        }
    }
}

void update_spot_light_frusta(
    ecs::Res<GlobalVisibleClusterableObjects> global_lights,
    ecs::Query<ecs::Item<ecs::Entity,
                         const ::epix::transform::GlobalTransform&,
                         const SpotLight&,
                         ecs::Mut<::epix::camera::Frustum>>,
               ecs::Filter<ecs::Or<ecs::Modified<::epix::transform::GlobalTransform>, ecs::Modified<SpotLight>>>>
        lights) {
    // Bevy `update_spot_light_frusta` (bevy_light/src/spot_light.rs:204-237). The
    // change filter is in the query, as Bevy's is, so a light that neither moved
    // nor changed keeps the frustum it has.
    const auto& clustered = global_lights.get().entities;

    for (auto&& [entity, transform, spot_light, frustum] : lights.iter()) {
        // A light that casts no shadows needs no frusta, and one in no cluster is
        // culled for no view.
        if (!spot_light.shadows_enabled || !clustered.contains(entity)) continue;

        // Scale is deliberately not applied: the shadow view must not scale the
        // light's radius and range. The backward axis is the normalised third
        // column, as in the point-light system.
        const glm::vec3 view_backward = glm::normalize(glm::vec3(transform.matrix[2]));

        const glm::mat4 world_from_view = spot_light_world_from_view(transform);
        const glm::mat4 clip_from_view =
            spot_light_clip_from_view(spot_light.outer_angle, spot_light.shadow_map_near_z);
        const glm::mat4 clip_from_world = clip_from_view * glm::inverse(world_from_view);

        *frustum = ::epix::camera::Frustum::from_clip_from_world_custom_far(
            clip_from_world, glm::vec3(transform.matrix[3]), view_backward, spot_light.range);
    }
}

void update_directional_light_frusta(ecs::Query<ecs::Item<const Cascades&,
                                                          const DirectionalLight&,
                                                          const ::epix::camera::ViewVisibility&,
                                                          ecs::Mut<::epix::camera::CascadesFrusta>>,
                                                ecs::Without<::epix::camera::Camera>> lights) {
    for (auto&& [cascades, directional_light, visibility, frusta] : lights.iter()) {
        if (!should_update_directional_cascade_frusta(directional_light.shadows_enabled, visibility.get())) {
            continue;
        }

        // Rebuilt whole, preserving the view keys, and with each cascade's near
        // plane pushed to infinity so it culls nothing in front of the light.
        camera::CascadesFrusta rebuilt;
        for (const auto& [view, view_cascades] : cascades.cascades) {
            auto& destination = rebuilt.frusta[view];
            destination.reserve(view_cascades.size());
            for (const Cascade& cascade : view_cascades) {
                destination.push_back(
                    directional_view_frustum(camera::Frustum::from_clip_from_world(cascade.clip_from_world)));
            }
        }
        *frusta = std::move(rebuilt);
    }
}

void clear_directional_light_cascades(ecs::Query<ecs::Item<const DirectionalLight&, ecs::Mut<Cascades>>> lights) {
    for (auto&& [directional_light, cascades] : lights.iter()) {
        if (!directional_light.shadows_enabled) continue;
        (*cascades).cascades.clear();
    }
}

void build_directional_light_cascades(ecs::Res<DirectionalLightShadowMap> shadow_map,
                                      ecs::Query<ecs::Item<ecs::Entity,
                                                           const ::epix::transform::GlobalTransform&,
                                                           const ::epix::camera::Projection&,
                                                           const ::epix::camera::Camera&>> views,
                                      ecs::Query<ecs::Item<const ::epix::transform::GlobalTransform&,
                                                           const DirectionalLight&,
                                                           const CascadeShadowConfig&,
                                                           ecs::Mut<Cascades>>> lights) {
    // The active views, with their projection and world matrix, collected first as
    // Bevy does - the light loop below needs each of them.
    struct ActiveView {
        ecs::Entity entity;
        const ::epix::camera::Projection* projection;
        glm::mat4 view_to_world;
    };
    std::vector<ActiveView> active_views;
    for (auto&& [entity, transform, projection, camera] : views.iter()) {
        if (!camera.is_active) continue;
        active_views.push_back(
            ActiveView{.entity = entity, .projection = &projection, .view_to_world = transform.matrix});
    }

    for (auto&& [transform, directional_light, config, cascades] : lights.iter()) {
        if (!directional_light.shadows_enabled) continue;

        // Rotation only, no translation, and with any scale stripped: the matrix
        // must be orthonormal for the shadows to be stable.
        const glm::mat3 basis = glm::mat3(transform.matrix);
        glm::mat3 rotation_basis;
        for (int column = 0; column < 3; ++column) {
            const glm::vec3 axis   = basis[column];
            const float length     = glm::length(axis);
            rotation_basis[column] = length > 0.0f ? axis / length : axis;
        }
        const glm::quat light_rotation   = glm::quat_cast(rotation_basis);
        const glm::mat4 world_from_light = glm::mat4_cast(light_rotation);

        const auto bounds = cascade_bounds(config);
        for (const ActiveView& view : active_views) {
            // Bevy zips the far bounds with near bounds that start at the configured
            // minimum distance and then follow the previous far bound scaled by the
            // overlap, which is what `cascade_bounds` returns.
            std::vector<std::array<glm::vec3, 8>> slices;
            slices.reserve(bounds.size());
            for (const auto& [near_bound, far_bound] : bounds) {
                // Negated because -Z is the camera's forward direction.
                slices.push_back(view.projection->get_frustum_corners(-near_bound, -far_bound));
            }

            (*cascades).cascades[view.entity] = build_view_cascades(slices, static_cast<float>(shadow_map.get().size),
                                                                    light_rotation, view.view_to_world);
        }
    }
}

void assign_objects_to_clusters(
    ecs::Commands commands,
    ecs::ResMut<GlobalVisibleClusterableObjects> global_clusterable_objects,
    ecs::Query<ecs::Item<ecs::Entity,
                         const ::epix::transform::GlobalTransform&,
                         const ::epix::camera::Camera&,
                         const ::epix::camera::Frustum&,
                         const ClusterConfig&,
                         Clusters&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>,
                         ecs::Opt<VisibleClusterableObjects&>>> views,
    ecs::Query<ecs::Item<ecs::Entity,
                         const ::epix::transform::GlobalTransform&,
                         const PointLight&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>,
                         ecs::Opt<const VolumetricLight&>,
                         const ::epix::camera::ViewVisibility&>> point_lights,
    ecs::Query<ecs::Item<ecs::Entity,
                         const ::epix::transform::GlobalTransform&,
                         const SpotLight&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>,
                         ecs::Opt<const VolumetricLight&>,
                         const ::epix::camera::ViewVisibility&>> spot_lights,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, ecs::Has<EnvironmentMapLight>>,
               ecs::With<LightProbe>> light_probes,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&>, ecs::With<ClusteredDecal>> decals,
    ecs::Local<std::vector<ClusterableObjectAssignmentData>> clusterable_objects,
    ecs::Local<std::vector<std::optional<::epix::camera::Sphere>>> cluster_spheres,
    ecs::Local<bool> warning_emitted,
    std::optional<ecs::Res<GlobalClusterSettings>> global_cluster_settings) {
    if (!global_cluster_settings.has_value()) return;

    const GlobalClusterSettings& settings = global_cluster_settings->get();

    std::vector<ClusterableObjectAssignmentData>& objects = *clusterable_objects;
    objects.clear();
    (*global_clusterable_objects).entities.clear();

    const auto layers_of = [](auto&& maybe_layers) {
        return maybe_layers.has_value() ? maybe_layers->get() : ::epix::camera::RenderLayers::layer(0);
    };

    // Visible point lights, whose reach is a sphere, so their transform is reduced
    // to a translation; visible spot lights, which keep theirs so the cone survives.
    for (auto&& [entity, transform, point_light, layers, volumetric, visibility] : point_lights.iter()) {
        if (!visibility.get()) continue;
        objects.push_back(point_light_assignment_data(entity, glm::vec3(transform.matrix[3]), point_light.range,
                                                      point_light.shadows_enabled, volumetric.has_value(),
                                                      layers_of(layers)));
    }
    for (auto&& [entity, transform, spot_light, layers, volumetric, visibility] : spot_lights.iter()) {
        if (!visibility.get()) continue;
        objects.push_back(spot_light_assignment_data(entity, transform.matrix, spot_light.range, spot_light.outer_angle,
                                                     spot_light.shadows_enabled, volumetric.has_value(),
                                                     layers_of(layers)));
    }
    if (settings.supports_storage_buffers) {
        for (auto&& [entity, transform, is_reflection_probe] : light_probes.iter()) {
            objects.push_back(light_probe_assignment_data(entity, transform.matrix, is_reflection_probe));
        }
    }
    if (settings.clustered_decals_are_usable) {
        for (auto&& [entity, transform] : decals.iter()) {
            objects.push_back(decal_assignment_data(entity, transform.matrix));
        }
    }

    // Without storage buffers the list has to fit a uniform, so it is cut to that
    // limit using whichever views can see each object.
    if (objects.size() > settings.max_uniform_buffer_clusterable_objects && !settings.supports_storage_buffers) {
        std::vector<::epix::camera::Frustum> view_frusta;
        for (auto&& [entity, transform, camera, frustum, config, clusters, layers, visible] : views.iter()) {
            view_frusta.push_back(frustum);
        }
        const auto in_any_view = [&view_frusta](const ::epix::camera::Sphere& sphere) {
            for (const ::epix::camera::Frustum& view_frustum : view_frusta) {
                if (view_frustum.intersects_sphere(sphere, true)) return true;
            }
            return false;
        };
        auto limited = limit_clusterable_objects_for_uniform_buffer(std::move(objects), in_any_view,
                                                                    settings.max_uniform_buffer_clusterable_objects);
        if (limited.exceeded && !*warning_emitted) {
            // Bevy's warning, emitted once per app (assign.rs:281-290).
            spdlog::warn("[pbr] max_uniform_buffer_clusterable_objects ({}) exceeded",
                         settings.max_uniform_buffer_clusterable_objects);
            *warning_emitted = true;
        }
        objects = std::move(limited.objects);
    }

    for (auto&& [view_entity, camera_transform, camera, frustum, config, clusters, layers, visible] : views.iter()) {
        if (config.kind == ClusterConfig::Kind::None) {
            if (visible.has_value()) commands.entity(view_entity).remove<VisibleClusterableObjects>();
            clusters.clear();
            continue;
        }

        const std::optional<glm::uvec2> screen_size = camera.physical_viewport_size();
        if (!screen_size.has_value() || screen_size->x == 0 || screen_size->y == 0) {
            clusters.clear();
            continue;
        }

        const glm::mat4 view_from_world = glm::inverse(camera_transform.matrix);
        // A view's own scale is the reciprocal of the world scale, and its largest
        // component bounds an object's extent under a non-uniform view.
        const glm::vec3 scale{1.0f / glm::length(glm::vec3(camera_transform.matrix[0])),
                              1.0f / glm::length(glm::vec3(camera_transform.matrix[1])),
                              1.0f / glm::length(glm::vec3(camera_transform.matrix[2]))};
        const float scale_max          = glm::compMax(glm::abs(scale));
        const bool is_orthographic     = camera.clip_from_view()[3].w == 1.0f;
        const glm::mat4 clip_from_view = camera.clip_from_view();

        glm::uvec3 dimensions = config.dimensions_for_screen_size(*screen_size);
        const float far_z     = view_far_z(config.far_z_mode(), objects, view_from_world, scale);
        const float first_slice =
            first_slice_depth(config, is_orthographic, dimensions.z, clip_from_view, scale, far_z);
        const float far_z_at_least_first = far_z_at_least_first_slice_depth(far_z, first_slice);

        const glm::vec2 cluster_factors = calculate_cluster_factors(first_slice, far_z_at_least_first,
                                                                    static_cast<float>(dimensions.z), is_orthographic);

        // Bevy shrinks the grid when the objects would need more indices than the
        // device's bindings hold.
        if (config.may_resize_dynamically()) {
            const float estimate = estimate_cluster_index_count(
                objects, view_from_world, scale, clip_from_view, cluster_factors, dimensions, is_orthographic,
                [&frustum](const ::epix::camera::Sphere& sphere) { return frustum.intersects_sphere(sphere, true); });
            dimensions = reduce_cluster_dimensions_for_index_limit(dimensions, estimate,
                                                                   settings.view_cluster_bindings_max_indices);
        }

        ViewClusterAssignment assignment;
        assignment.dimensions      = dimensions;
        assignment.cluster_factors = calculate_cluster_factors(first_slice, far_z_at_least_first,
                                                               static_cast<float>(dimensions.z), is_orthographic);
        assignment.view_from_world = view_from_world;
        assignment.view_from_world_scale_max = scale_max;
        assignment.is_orthographic           = is_orthographic;
        assignment.first_slice_depth         = first_slice;
        assignment.far_z                     = far_z_at_least_first;
        assignment.clip_from_view            = clip_from_view;
        assignment.view_from_clip            = glm::inverse(clip_from_view);
        assignment.screen_size               = glm::vec2(*screen_size);

        // The grid: whole pixels per tile, with the dimensions recomputed from them.
        clusters.update(*screen_size, dimensions);
        assignment.dimensions = clusters.dimensions;
        assignment.tile_size  = glm::vec2(clusters.tile_size);

        // Bevy `assign.rs:454-477`: the near/far depths of the grid are recorded
        // after the update, and the per-cluster lists are cleared and resized to
        // the cluster count - which changes with the dimensions, and which a
        // fresh `Clusters` starts without.
        clusters.near_plane = first_slice;
        clusters.far_plane  = far_z;
        for (auto& cluster : clusters.clusterable_objects) {
            cluster.entities.clear();
            cluster.counts = ClusterableObjectCounts{};
        }
        clusters.clusterable_objects.resize(static_cast<std::size_t>(clusters.dimensions.x) * clusters.dimensions.y *
                                            clusters.dimensions.z);

        assignment.layers = layers_of(layers);

        if (is_orthographic) {
            const auto planes   = orthographic_cluster_xy_planes(assignment.view_from_clip, assignment.dimensions);
            assignment.x_planes = planes.first;
            assignment.y_planes = planes.second;
        } else {
            const auto planes   = perspective_cluster_xy_planes(assignment.view_from_clip, assignment.dimensions);
            assignment.x_planes = planes.first;
            assignment.y_planes = planes.second;
        }
        assignment.z_planes =
            cluster_z_planes(assignment.first_slice_depth, assignment.far_z, assignment.dimensions.z, is_orthographic);
        (*cluster_spheres)
            .assign(
                static_cast<std::size_t>(assignment.dimensions.x) * assignment.dimensions.y * assignment.dimensions.z,
                std::nullopt);

        for (const ClusterableObjectAssignmentData& object : objects) {
            if (assign_object_to_view_clusters(clusters.clusterable_objects, assignment, frustum, *cluster_spheres,
                                               object)) {
                (*global_clusterable_objects).entities.insert(object.entity);
                if (visible.has_value()) visible->get().entities.push_back(object.entity);
            }
        }
    }
}

namespace {
/** @brief Whether any view's visible set names this light (the union Bevy's
 *  `checked_lights` walks). */
bool light_is_visible_to_any_view(const ecs::Query<ecs::Item<const VisibleClusterableObjects&>>& visible_lights,
                                  ecs::Entity light) {
    for (auto&& [visible] : visible_lights.iter()) {
        // The per-view list is a vector in Bevy and here.
        if (std::find(visible.entities.begin(), visible.entities.end(), light) != visible.entities.end()) return true;
    }
    return false;
}

::epix::camera::RenderLayers layers_or_default(
    const std::optional<std::reference_wrapper<const ::epix::camera::RenderLayers>>& layers) {
    return layers.has_value() ? layers->get() : ::epix::camera::RenderLayers::layer(0);
}
}  // namespace

void check_point_light_mesh_visibility(
    ecs::Query<ecs::Item<const VisibleClusterableObjects&>> visible_lights,
    ecs::Query<ecs::Item<ecs::Entity,
                         const PointLight&,
                         const ::epix::transform::GlobalTransform&,
                         const ::epix::camera::CubemapFrusta&,
                         ::epix::camera::CubemapVisibleEntities&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>>> point_lights,
    ecs::Query<ecs::Item<ecs::Entity,
                         const SpotLight&,
                         const ::epix::transform::GlobalTransform&,
                         const ::epix::camera::Frustum&,
                         ::epix::camera::VisibleMeshEntities&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>>> spot_lights,
    ecs::Query<ecs::Item<ecs::Entity,
                         const ::epix::camera::InheritedVisibility&,
                         ::epix::camera::ViewVisibility&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>,
                         ecs::Opt<const ::epix::camera::Aabb&>,
                         ecs::Opt<const ::epix::transform::GlobalTransform&>,
                         ecs::Opt<const ::epix::camera::VisibilityRange&>,
                         ecs::Opt<const ::epix::camera::NoFrustumCulling&>,
                         ecs::Opt<const ::epix::mesh::Mesh3d&>,
                         ecs::Opt<const NotShadowCaster&>,
                         ecs::Opt<const DirectionalLight&>>> visible_entities,
    std::optional<ecs::Res<::epix::camera::VisibleEntityRanges>> visible_entity_ranges) {
    using ::epix::camera::Aabb;
    using ::epix::camera::RenderLayers;
    using ::epix::camera::Sphere;

    const ::epix::camera::VisibleEntityRanges* ranges =
        visible_entity_ranges.has_value() ? &visible_entity_ranges->get() : nullptr;

    for (auto&& [light_entity, point_light, transform, cubemap_frusta, cubemap_visible, maybe_layers] :
         point_lights.iter()) {
        if (!light_is_visible_to_any_view(visible_lights, light_entity)) continue;

        // Clear first, and leave empty when shadows are off: a light without shadow
        // mapping must have no visible entities rather than stale ones.
        for (::epix::camera::VisibleMeshEntities& face : cubemap_visible.iter_mut()) face.entities.clear();
        if (!point_light.shadows_enabled) continue;

        const RenderLayers light_layers = layers_or_default(maybe_layers);
        const Sphere light_sphere{.center = glm::vec3(transform.matrix[3]), .radius = point_light.range};

        for (auto&& [mesh_entity, inherited, visibility, maybe_mesh_layers, maybe_aabb, maybe_transform, maybe_range,
                     maybe_no_culling, maybe_mesh3d, maybe_not_shadow_caster, maybe_directional_light] :
             visible_entities.iter()) {
            if (!inherited.get()) continue;
            // Bevy's query filters, as presence tests: it asks for meshes With<Mesh3d>,
            // Without<NotShadowCaster> and Without<DirectionalLight>.
            if (!maybe_mesh3d.has_value()) continue;
            if (maybe_not_shadow_caster.has_value()) continue;
            if (maybe_directional_light.has_value()) continue;
            if (!light_layers.intersects(layers_or_default(maybe_mesh_layers))) continue;
            if (maybe_range.has_value() && ranges != nullptr && !ranges->entity_is_in_range_of_any_view(mesh_entity)) {
                continue;
            }

            // If we have an aabb and transform, do frustum culling.
            if (maybe_aabb.has_value() && maybe_transform.has_value()) {
                const Aabb& aabb                 = maybe_aabb->get();
                const glm::mat4 world_from_local = maybe_transform->get().matrix;

                // A cheap sphere-versus-OBB test prunes most meshes outside the light's
                // sphere before any face is considered.
                if (!maybe_no_culling.has_value() && !light_sphere.intersects_obb(aabb, world_from_local)) {
                    continue;
                }

                for (std::size_t face = 0; face < POINT_LIGHT_CUBE_FACE_COUNT; ++face) {
                    if (maybe_no_culling.has_value() ||
                        cubemap_frusta.frusta[face].intersects_obb(aabb, world_from_local, true, true)) {
                        visibility.set_visible();
                        cubemap_visible.get_mut(face).entities.push_back(mesh_entity);
                    }
                }
            } else {
                // Nothing to cull against: visible to every face.
                visibility.set_visible();
                for (::epix::camera::VisibleMeshEntities& face : cubemap_visible.iter_mut()) {
                    face.entities.push_back(mesh_entity);
                }
            }
        }

        // Bevy bounds each face list's capacity after the walk (lib.rs:604-606).
        for (::epix::camera::VisibleMeshEntities& face : cubemap_visible.iter_mut()) shrink_entities(face.entities);
    }

    for (auto&& [light_entity, spot_light, transform, frustum, visible_meshes, maybe_layers] : spot_lights.iter()) {
        if (!light_is_visible_to_any_view(visible_lights, light_entity)) continue;

        visible_meshes.entities.clear();
        if (!spot_light.shadows_enabled) continue;

        const RenderLayers light_layers = layers_or_default(maybe_layers);
        const Sphere light_sphere{.center = glm::vec3(transform.matrix[3]), .radius = spot_light.range};

        for (auto&& [mesh_entity, inherited, visibility, maybe_mesh_layers, maybe_aabb, maybe_transform, maybe_range,
                     maybe_no_culling, maybe_mesh3d, maybe_not_shadow_caster, maybe_directional_light] :
             visible_entities.iter()) {
            if (!inherited.get()) continue;
            // Bevy's query filters, as presence tests: it asks for meshes With<Mesh3d>,
            // Without<NotShadowCaster> and Without<DirectionalLight>.
            if (!maybe_mesh3d.has_value()) continue;
            if (maybe_not_shadow_caster.has_value()) continue;
            if (maybe_directional_light.has_value()) continue;
            if (!light_layers.intersects(layers_or_default(maybe_mesh_layers))) continue;
            if (maybe_range.has_value() && ranges != nullptr && !ranges->entity_is_in_range_of_any_view(mesh_entity)) {
                continue;
            }

            // The same two tests as the point branch, against a single cone.
            if (maybe_aabb.has_value() && maybe_transform.has_value()) {
                const Aabb& aabb                 = maybe_aabb->get();
                const glm::mat4 world_from_local = maybe_transform->get().matrix;

                if (!maybe_no_culling.has_value() && !light_sphere.intersects_obb(aabb, world_from_local)) {
                    continue;
                }
                if (maybe_no_culling.has_value() || frustum.intersects_obb(aabb, world_from_local, true, true)) {
                    visibility.set_visible();
                    visible_meshes.entities.push_back(mesh_entity);
                }
            } else {
                visibility.set_visible();
                visible_meshes.entities.push_back(mesh_entity);
            }
        }

        // Bevy bounds the spot list's capacity after the walk (lib.rs:682).
        shrink_entities(visible_meshes.entities);
    }
}

void check_dir_light_mesh_visibility(
    ecs::Commands commands,
    ecs::Query<ecs::Item<const DirectionalLight&,
                         const ::epix::camera::CascadesFrusta&,
                         ::epix::camera::CascadesVisibleEntities&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>,
                         const ::epix::camera::ViewVisibility&,
                         ecs::Opt<const SpotLight&>>> directional_lights,
    ecs::Query<ecs::Item<ecs::Entity,
                         const ::epix::camera::InheritedVisibility&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>,
                         ecs::Opt<const ::epix::camera::Aabb&>,
                         ecs::Opt<const ::epix::transform::GlobalTransform&>,
                         ecs::Opt<const ::epix::camera::VisibilityRange&>,
                         ecs::Opt<const ::epix::camera::NoFrustumCulling&>,
                         ecs::Opt<const ::epix::mesh::Mesh3d&>,
                         ecs::Opt<const NotShadowCaster&>,
                         ecs::Opt<const DirectionalLight&>>> visible_entities,
    std::optional<ecs::Res<::epix::camera::VisibleEntityRanges>> visible_entity_ranges) {
    using ::epix::camera::Aabb;
    using ::epix::camera::CascadesVisibleEntities;
    using ::epix::camera::Frustum;
    using ::epix::camera::RenderLayers;

    const ::epix::camera::VisibleEntityRanges* ranges =
        visible_entity_ranges.has_value() ? &visible_entity_ranges->get() : nullptr;

    // The meshes a light sees are marked visible through a command, so this system can
    // run beside the point and spot one rather than both wanting ViewVisibility.
    std::vector<ecs::Entity> deferred_visible;

    for (auto&& [directional_light, frusta, visible, maybe_light_layers, light_visibility, maybe_spot] :
         directional_lights.iter()) {
        if (maybe_spot.has_value()) continue;  // Bevy's Without<SpotLight>

        // Reconcile the per-view cascade lists with the cascades this light has. Views
        // are only remembered for removal here, since erasing while iterating would
        // invalidate the iteration. Resizing matters as much as clearing: a light whose
        // cascade count shrank must not keep rendering the cascades it no longer has.
        std::vector<ecs::Entity> views_to_remove;
        for (auto& [view, cascade_lists] : visible.entities) {
            const auto found = frusta.frusta.find(view);
            if (found == frusta.frusta.end()) {
                views_to_remove.push_back(view);
                continue;
            }
            cascade_lists.resize(found->second.size());
            for (::epix::camera::VisibleMeshEntities& cascade : cascade_lists) cascade.entities.clear();
        }
        for (const auto& [view, view_frusta] : frusta.frusta) {
            auto [entry, inserted] = visible.entities.try_emplace(view);
            if (inserted || entry->second.size() != view_frusta.size()) {
                entry->second.resize(view_frusta.size());
            }
        }
        for (ecs::Entity view : views_to_remove) visible.entities.erase(view);

        // Shadows off, or the light itself not visible: the lists stay empty.
        if (!directional_light.shadows_enabled || !light_visibility.get()) continue;

        const RenderLayers light_layers = layers_or_default(maybe_light_layers);

        for (const auto& [view, view_frusta] : frusta.frusta) {
            const auto entry = visible.entities.find(view);
            if (entry == visible.entities.end()) continue;
            std::vector<::epix::camera::VisibleMeshEntities>& cascade_lists = entry->second;

            for (auto&& [mesh_entity, inherited, maybe_mesh_layers, maybe_aabb, maybe_transform, maybe_range,
                         maybe_no_culling, maybe_mesh3d, maybe_not_shadow_caster, maybe_directional] :
                 visible_entities.iter()) {
                if (!inherited.get()) continue;
                // Bevy's query filters, as presence tests: it asks for meshes With<Mesh3d>,
                // Without<NotShadowCaster> and Without<DirectionalLight>.
                if (!maybe_mesh3d.has_value()) continue;
                if (maybe_not_shadow_caster.has_value()) continue;
                if (maybe_directional.has_value()) continue;
                if (!light_layers.intersects(layers_or_default(maybe_mesh_layers))) continue;
                // Per view, not "any view": a cascade belongs to one view.
                if (maybe_range.has_value() && ranges != nullptr &&
                    !ranges->entity_is_in_range_of_view(mesh_entity, view)) {
                    continue;
                }

                if (maybe_aabb.has_value() && maybe_transform.has_value()) {
                    const Aabb& aabb                 = maybe_aabb->get();
                    const glm::mat4 world_from_local = maybe_transform->get().matrix;
                    bool visible_to_any_cascade      = false;
                    for (std::size_t cascade = 0; cascade < view_frusta.size() && cascade < cascade_lists.size();
                         ++cascade) {
                        // Near-plane culling is off: a shadow caster may lie in front of
                        // the near plane and still cast into this cascade.
                        if (!maybe_no_culling.has_value() &&
                            !view_frusta[cascade].intersects_obb(aabb, world_from_local, false, true)) {
                            continue;
                        }
                        visible_to_any_cascade = true;
                        cascade_lists[cascade].entities.push_back(mesh_entity);
                    }
                    if (visible_to_any_cascade) deferred_visible.push_back(mesh_entity);
                } else {
                    // Nothing to cull against: every cascade.
                    deferred_visible.push_back(mesh_entity);
                    for (::epix::camera::VisibleMeshEntities& list : cascade_lists)
                        list.entities.push_back(mesh_entity);
                }
            }
        }

        for (auto& [view, cascade_lists] : visible.entities) {
            for (::epix::camera::VisibleMeshEntities& list : cascade_lists) shrink_entities(list.entities);
        }
    }

    // The deferred marking, queued as a command exactly as Bevy does.
    if (!deferred_visible.empty()) {
        commands.queue([entities = std::move(deferred_visible)](ecs::World& world) {
            for (ecs::Entity entity : entities) {
                world.get_entity_mut(entity).and_then(
                    [&](ecs::EntityWorldMut&& entity_world) mutable -> std::optional<bool> {
                        if (auto visibility = entity_world.template get_mut<::epix::camera::ViewVisibility>()) {
                            visibility->get_mut().set_visible();
                        }
                        return true;
                    });
            }
        });
    }
}

}  // namespace epix::pbr
