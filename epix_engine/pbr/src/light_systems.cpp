#include <epix/pbr/light_systems.hpp>

#include <algorithm>
#include <cstddef>
#include <glm/glm.hpp>
// `compMax` is a gtx extension, and glm guards those behind this define.
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>

namespace epix::pbr {

void update_point_light_frusta(
    ecs::Res<GlobalVisibleClusterableObjects> global_lights,
    ecs::Query<ecs::Item<ecs::Entity, ecs::Ref<::epix::transform::GlobalTransform>, ecs::Ref<PointLight>,
                         ecs::Mut<::epix::camera::CubemapFrusta>>>
        lights) {
    const bool global_lights_changed = global_lights.is_modified();
    const auto& clustered          = global_lights.get().entities;

    for (auto&& [entity, transform, point_light, cubemap_frusta] : lights.iter()) {
        const bool light_changed = transform.is_modified() || point_light.is_modified();
        if (!should_update_point_light_frusta(light_changed, global_lights_changed,
                                              point_light.get().shadows_enabled, clustered.contains(entity))) {
            continue;
        }

        // Epix's GlobalTransform is a matrix, so the light's world position is its
        // translation column and its backward axis is the third column - the local
        // +Z of a right-handed transform. The light's scale and rotation are
        // deliberately not used, as in Bevy.
        const glm::mat4 world_from_light = transform.get().matrix;
        const glm::vec3 translation      = glm::vec3(world_from_light[3]);
        const glm::vec3 backward         = glm::vec3(world_from_light[2]);

        const auto frusta = point_light_cubemap_frusta(translation, backward, point_light.get().range,
                                                      point_light.get().shadow_map_near_z);
        auto& destination = (*cubemap_frusta).frusta;
        for (std::size_t face = 0; face < frusta.size(); ++face) {
            destination[face] = frusta[face];
        }
    }
}

void update_directional_light_frusta(
    ecs::Query<ecs::Item<const Cascades&, const DirectionalLight&, const ::epix::camera::ViewVisibility&,
                         ecs::Mut<::epix::camera::CascadesFrusta>>,
                ecs::Without<::epix::camera::Camera>>
        lights) {
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

void build_directional_light_cascades(
    ecs::Res<DirectionalLightShadowMap> shadow_map,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, const ::epix::camera::Projection&,
                         const ::epix::camera::Camera&>>
        views,
    ecs::Query<ecs::Item<const ::epix::transform::GlobalTransform&, const DirectionalLight&,
                         const CascadeShadowConfig&, ecs::Mut<Cascades>>>
        lights) {
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
        active_views.push_back(ActiveView{.entity       = entity,
                                          .projection   = &projection,
                                          .view_to_world = transform.matrix});
    }

    for (auto&& [transform, directional_light, config, cascades] : lights.iter()) {
        if (!directional_light.shadows_enabled) continue;

        // Rotation only, no translation, and with any scale stripped: the matrix
        // must be orthonormal for the shadows to be stable.
        const glm::mat3 basis = glm::mat3(transform.matrix);
        glm::mat3 rotation_basis;
        for (int column = 0; column < 3; ++column) {
            const glm::vec3 axis = basis[column];
            const float length   = glm::length(axis);
            rotation_basis[column] = length > 0.0f ? axis / length : axis;
        }
        const glm::quat light_rotation = glm::quat_cast(rotation_basis);
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

            (*cascades).cascades[view.entity] = build_view_cascades(
                slices, static_cast<float>(shadow_map.get().size), light_rotation, view.view_to_world);
        }
    }
}

void assign_objects_to_clusters(
    ecs::Commands commands,
    ecs::ResMut<GlobalVisibleClusterableObjects> global_clusterable_objects,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, const ::epix::camera::Camera&,
                         const ::epix::camera::Frustum&, const ClusterConfig&, Clusters&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>, ecs::Opt<VisibleClusterableObjects&>>>
        views,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, const PointLight&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>, ecs::Opt<const VolumetricLight&>,
                         const ::epix::camera::ViewVisibility&>>
        point_lights,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, const SpotLight&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>, ecs::Opt<const VolumetricLight&>,
                         const ::epix::camera::ViewVisibility&>>
        spot_lights,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, ecs::Has<EnvironmentMapLight>>,
               ecs::With<LightProbe>>
        light_probes,
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
        objects.push_back(spot_light_assignment_data(entity, transform.matrix, spot_light.range,
                                                     spot_light.outer_angle, spot_light.shadows_enabled,
                                                     volumetric.has_value(), layers_of(layers)));
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
            *warning_emitted = true;  // Bevy warns once that the limit was exceeded.
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
        const float scale_max = glm::compMax(glm::abs(scale));
        const bool is_orthographic     = camera.clip_from_view()[3].w == 1.0f;
        const glm::mat4 clip_from_view = camera.clip_from_view();

        glm::uvec3 dimensions = config.dimensions_for_screen_size(*screen_size);
        const float far_z     = view_far_z(config.far_z_mode(), objects, view_from_world, scale);
        const float first_slice =
            first_slice_depth(config, is_orthographic, dimensions.z, clip_from_view, scale, far_z);
        const float far_z_at_least_first = far_z_at_least_first_slice_depth(far_z, first_slice);

        const glm::vec2 cluster_factors =
            calculate_cluster_factors(first_slice, far_z_at_least_first, static_cast<float>(dimensions.z),
                                      is_orthographic);

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
        assignment.dimensions                = dimensions;
        assignment.cluster_factors           = calculate_cluster_factors(
            first_slice, far_z_at_least_first, static_cast<float>(dimensions.z), is_orthographic);
        assignment.view_from_world           = view_from_world;
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

        if (is_orthographic) {
            const auto planes   = orthographic_cluster_xy_planes(assignment.view_from_clip, assignment.dimensions);
            assignment.x_planes = planes.first;
            assignment.y_planes = planes.second;
        } else {
            const auto planes   = perspective_cluster_xy_planes(assignment.view_from_clip, assignment.dimensions);
            assignment.x_planes = planes.first;
            assignment.y_planes = planes.second;
        }
        assignment.z_planes = cluster_z_planes(assignment.first_slice_depth, assignment.far_z,
                                              assignment.dimensions.z, is_orthographic);
        (*cluster_spheres).assign(static_cast<std::size_t>(assignment.dimensions.x) * assignment.dimensions.y *
                                      assignment.dimensions.z,
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
bool light_is_visible_to_any_view(
    const ecs::Query<ecs::Item<const VisibleClusterableObjects&>>& visible_lights, ecs::Entity light) {
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
    ecs::Query<ecs::Item<ecs::Entity, const PointLight&, const ::epix::transform::GlobalTransform&,
                         const ::epix::camera::CubemapFrusta&, ::epix::camera::CubemapVisibleEntities&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>>>
        point_lights,
    ecs::Query<ecs::Item<ecs::Entity, const SpotLight&, const ::epix::transform::GlobalTransform&,
                         const ::epix::camera::Frustum&, ::epix::camera::VisibleMeshEntities&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>>>
        spot_lights,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::camera::InheritedVisibility&, ::epix::camera::ViewVisibility&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>, ecs::Opt<const ::epix::camera::Aabb&>,
                         ecs::Opt<const ::epix::transform::GlobalTransform&>, ecs::Opt<const ::epix::camera::VisibilityRange&>,
                         ecs::Opt<const ::epix::camera::NoFrustumCulling&>, ecs::Opt<const ::epix::mesh::Mesh3d&>,
                         ecs::Opt<const NotShadowCaster&>, ecs::Opt<const DirectionalLight&>>>
        visible_entities,
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
        for (::epix::camera::VisibleMeshEntities& face : cubemap_visible.data) face.entities.clear();
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

            const std::optional<Aabb> aabb = maybe_aabb.has_value() ? std::optional<Aabb>{maybe_aabb->get()} : std::nullopt;
            const std::optional<glm::mat4> world_from_local =
                maybe_transform.has_value() ? std::optional<glm::mat4>{maybe_transform->get().matrix} : std::nullopt;

            const auto faces = point_light_face_visibility(light_sphere, cubemap_frusta.frusta, aabb, world_from_local,
                                                          maybe_no_culling.has_value());
            for (std::size_t face = 0; face < POINT_LIGHT_CUBE_FACE_COUNT; ++face) {
                if (!faces[face]) continue;
                visibility.set_visible();
                cubemap_visible.data[face].entities.push_back(mesh_entity);
            }
        }
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

            const std::optional<Aabb> aabb = maybe_aabb.has_value() ? std::optional<Aabb>{maybe_aabb->get()} : std::nullopt;
            const std::optional<glm::mat4> world_from_local =
                maybe_transform.has_value() ? std::optional<glm::mat4>{maybe_transform->get().matrix} : std::nullopt;

            if (!spot_light_mesh_visible(light_sphere, frustum, aabb, world_from_local, maybe_no_culling.has_value())) continue;
            visibility.set_visible();
            visible_meshes.entities.push_back(mesh_entity);
        }
    }
}

namespace {
/** @brief The candidate-mesh checks the light-visibility systems share.
 *
 *  Returns false when the mesh should be skipped: it carries no `Mesh3d`, it opted out
 *  with `NotShadowCaster`, it is itself a directional light, or the light's layers do
 *  not intersect its own. Factored out because the point, spot and directional loops
 *  make exactly these checks, and writing them three times is how they drift apart. */
bool mesh_candidate_passes(const ::epix::camera::RenderLayers& light_layers,
                           const std::optional<std::reference_wrapper<const ::epix::camera::RenderLayers>>& mesh_layers,
                           bool has_mesh3d, bool opted_out_of_casting, bool is_directional_light) {
    if (!has_mesh3d) return false;
    if (opted_out_of_casting) return false;
    if (is_directional_light) return false;
    return light_layers.intersects(layers_or_default(mesh_layers));
}
}  // namespace

void check_dir_light_mesh_visibility(
    ecs::Commands commands,
    ecs::Query<ecs::Item<const DirectionalLight&, const ::epix::camera::CascadesFrusta&,
                         ::epix::camera::CascadesVisibleEntities&, ecs::Opt<const ::epix::camera::RenderLayers&>,
                         const ::epix::camera::ViewVisibility&, ecs::Opt<const SpotLight&>>>
        directional_lights,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::camera::InheritedVisibility&,
                         ecs::Opt<const ::epix::camera::RenderLayers&>, ecs::Opt<const ::epix::camera::Aabb&>,
                         ecs::Opt<const ::epix::transform::GlobalTransform&>, ecs::Opt<const ::epix::camera::VisibilityRange&>,
                         ecs::Opt<const ::epix::camera::NoFrustumCulling&>, ecs::Opt<const ::epix::mesh::Mesh3d&>,
                         ecs::Opt<const NotShadowCaster&>, ecs::Opt<const DirectionalLight&>>>
        visible_entities,
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

        reconcile_cascade_visible_entities(visible, frusta.frusta);

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
                if (!mesh_candidate_passes(light_layers, maybe_mesh_layers, maybe_mesh3d.has_value(),
                                           maybe_not_shadow_caster.has_value(), maybe_directional.has_value())) {
                    continue;
                }
                // Per view, not "any view": a cascade belongs to one view.
                if (maybe_range.has_value() && ranges != nullptr && !ranges->entity_is_in_range_of_view(mesh_entity, view)) {
                    continue;
                }

                if (maybe_aabb.has_value() && maybe_transform.has_value()) {
                    const Aabb& aabb             = maybe_aabb->get();
                    const glm::mat4 world_from_local = maybe_transform->get().matrix;
                    bool visible_to_any_cascade  = false;
                    for (std::size_t cascade = 0; cascade < view_frusta.size() && cascade < cascade_lists.size(); ++cascade) {
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
                    for (::epix::camera::VisibleMeshEntities& list : cascade_lists) list.entities.push_back(mesh_entity);
                }
            }
        }

        for (auto& [view, cascade_lists] : visible.entities) {
            for (::epix::camera::VisibleMeshEntities& list : cascade_lists) list.entities.shrink_to_fit();
        }
    }

    // The deferred marking, queued as a command exactly as Bevy does.
    if (!deferred_visible.empty()) {
        commands.queue([entities = std::move(deferred_visible)](ecs::World& world) {
            for (ecs::Entity entity : entities) {
                world.get_entity_mut(entity).and_then([&](ecs::EntityWorldMut&& entity_world) mutable -> std::optional<bool> {
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
