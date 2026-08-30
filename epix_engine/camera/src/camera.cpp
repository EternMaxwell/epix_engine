#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <epix/camera.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <webgpu/webgpu.hpp>

using namespace epix::ecs;
using namespace epix::app;

namespace epix::camera {
namespace {
Frustum compute_camera_projection_frustum(const glm::mat4& clip_from_view,
                                          const ::epix::transform::GlobalTransform& camera_transform,
                                          float far_distance) {
    const glm::mat4 clip_from_world = clip_from_view * glm::inverse(camera_transform.matrix);
    const glm::vec3 translation     = glm::vec3(camera_transform.matrix[3]);
    glm::vec3 backward              = glm::vec3(camera_transform.matrix[2]);
    const float backward_length     = glm::length(backward);
    if (backward_length > 0.0f && std::isfinite(backward_length)) {
        backward /= backward_length;
    } else {
        backward = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return Frustum::from_clip_from_world_custom_far(clip_from_world, translation, backward, far_distance);
}
}  // namespace

Frustum OrthographicProjection::compute_frustum(const ::epix::transform::GlobalTransform& camera_transform) const {
    return compute_camera_projection_frustum(get_clip_from_view(), camera_transform, get_far());
}

Frustum PerspectiveProjection::compute_frustum(const ::epix::transform::GlobalTransform& camera_transform) const {
    return compute_camera_projection_frustum(get_clip_from_view(), camera_transform, get_far());
}

Frustum CustomProjection::compute_frustum(const ::epix::transform::GlobalTransform& camera_transform) const {
    return compute_camera_projection_frustum(get_clip_from_view(), camera_transform, get_far());
}

Frustum Projection::compute_frustum(const ::epix::transform::GlobalTransform& camera_transform) const {
    return compute_camera_projection_frustum(get_clip_from_view(), camera_transform, get_far());
}

std::expected<void, CameraUpdateError> camera_system(
    EventReader<::epix::window::WindowResized> window_resized_reader,
    EventReader<::epix::window::WindowCreated> window_created_reader,
    EventReader<::epix::window::WindowScaleFactorChanged> window_scale_factor_changed_reader,
    Query<Item<Entity, const ::epix::window::Window&>, With<::epix::window::PrimaryWindow>> primary_window_query,
    Query<Item<Entity, const ::epix::window::Window&>> window_query,
    Query<Item<Mut<Camera>, Ref<RenderTarget>, Mut<Projection>>> cameras) {
    std::unordered_set<Entity> changed_windows;
    for (const auto& event : window_created_reader.read()) changed_windows.insert(event.window);
    for (const auto& event : window_resized_reader.read()) changed_windows.insert(event.window);
    std::unordered_set<Entity> scale_factor_changed_windows;
    for (const auto& event : window_scale_factor_changed_reader.read()) {
        changed_windows.insert(event.window);
        scale_factor_changed_windows.insert(event.window);
    }

    const auto primary_window = primary_window_query.single().transform(
        [](const auto& primary) { return std::get<0>(primary); });

    for (auto&& [camera, target, projection] : cameras.iter()) {
        const auto normalized_target = target.get().normalize(primary_window);
        if (!normalized_target) continue;

        const auto viewport_size =
            camera.get().viewport.transform([](const Viewport& viewport) { return viewport.physical_size; });
        const bool target_changed = std::visit(
            utils::visitor{
                [&](const window::NormalizedWindowRef& window) { return changed_windows.contains(window.entity()); },
                [&](const ImageRenderTarget&) { return target.is_added() || target.is_modified(); },
                [](const ManualTextureViewHandle&) { return true; },
                [&](const NoColorTarget&) { return target.is_added() || target.is_modified(); },
            },
            *normalized_target);
        const auto& camera_ref = camera.get();
        const bool needs_update = target_changed || camera.is_added() || projection.is_modified() ||
                                  camera_ref.computed.old_viewport_size != viewport_size ||
                                  camera_ref.computed.old_sub_camera_view != camera_ref.sub_camera_view;
        if (!needs_update) continue;

        const auto target_info = std::visit(
            utils::visitor{
                [&](const window::NormalizedWindowRef& window)
                    -> std::expected<RenderTargetInfo, CameraUpdateError> {
                    if (const auto value = window_query.get(window.entity())) {
                        const auto& resolved = std::get<1>(*value);
                        return RenderTargetInfo{{resolved.physical_size.first, resolved.physical_size.second},
                                                resolved.scale_factor};
                    }
                    return std::unexpected(CameraUpdateError{CameraUpdateError::Window{window.entity()}});
                },
                [](const ImageRenderTarget& image) -> std::expected<RenderTargetInfo, CameraUpdateError> {
                    if (image.texture)
                        return RenderTargetInfo{{image.texture.getWidth(), image.texture.getHeight()}, image.scale_factor};
                    return std::unexpected(CameraUpdateError{CameraUpdateError::Image{}});
                },
                [](const ManualTextureViewHandle&) -> std::expected<RenderTargetInfo, CameraUpdateError> {
                    // The renderer owns ManualTextureViews. Its correction pass
                    // resolves this target after the general camera pass.
                    return RenderTargetInfo{};
                },
                [](const NoColorTarget& no_color) -> std::expected<RenderTargetInfo, CameraUpdateError> {
                    return RenderTargetInfo{no_color.size, 1.0f};
                },
            },
            *normalized_target);
        if (!target_info) return std::unexpected(std::move(target_info).error());
        if (std::holds_alternative<ManualTextureViewHandle>(*normalized_target)) continue;

        auto& camera_mut = camera.get_mut();
        if (const auto* window = std::get_if<window::NormalizedWindowRef>(&*normalized_target);
            window && scale_factor_changed_windows.contains(window->entity())) {
            if (const auto old_scale_factor = camera_mut.computed.target_info.transform(
                    [](const RenderTargetInfo& info) { return info.scale_factor; });
                old_scale_factor && *old_scale_factor > 0.0f) {
                const float resize_factor = target_info->scale_factor / *old_scale_factor;
                if (camera_mut.viewport) {
                    camera_mut.viewport->physical_position =
                        glm::uvec2(glm::vec2(camera_mut.viewport->physical_position) * resize_factor);
                    camera_mut.viewport->physical_size =
                        glm::uvec2(glm::vec2(camera_mut.viewport->physical_size) * resize_factor);
                }
            }
        }
        if (camera_mut.viewport) camera_mut.viewport->clamp_to_size(target_info->physical_size);
        camera_mut.computed.target_info = *target_info;
        if (const auto logical_size = camera_mut.logical_viewport_size(); logical_size && logical_size->x != 0.0f &&
                                                               logical_size->y != 0.0f) {
            projection.get_mut().update(logical_size->x, logical_size->y);
            camera_mut.computed.clip_from_view =
                camera_mut.sub_camera_view ? projection.get().get_clip_from_view_for_sub(*camera_mut.sub_camera_view)
                                           : projection.get().get_clip_from_view();
        }

        const auto updated_viewport_size =
            camera_mut.viewport.transform([](const Viewport& viewport) { return viewport.physical_size; });
        if (camera_mut.computed.old_viewport_size != updated_viewport_size)
            camera_mut.computed.old_viewport_size = updated_viewport_size;
        if (camera_mut.computed.old_sub_camera_view != camera_mut.sub_camera_view)
            camera_mut.computed.old_sub_camera_view = camera_mut.sub_camera_view;
    }
    return {};
}

void Camera::register_required_components(RequiredComponentsRegistrator& registrator) {
    // ComputedCameraValues starts at a defined 0x0 target until this system's
    // first target-resolution pass; target resolution reads the current
    // Window component rather than a backend cache snapshot.
    registrator.register_required<::epix::transform::Transform>([] { return ::epix::transform::Transform{}; });
    registrator.register_required<VisibleEntities>([] { return VisibleEntities{}; });
    // Bevy Camera requires Visibility. CameraPlugin in turn supplies its
    // InheritedVisibility and ViewVisibility dependencies.
    registrator.register_required<Visibility>([] { return Visibility{}; });
    // In Bevy 0.18 RenderTarget is a separate required component, rather
    // than state stored in Camera.
    registrator.register_required<RenderTarget>([] { return RenderTarget::from_primary(); });
    registrator.register_required<CameraMainTextureUsages>([] { return CameraMainTextureUsages{}; });
    // Bevy Camera requires Frustum; update_frusta fills it.
    registrator.register_required<Frustum>([] { return Frustum{}; });
}

std::optional<NormalizedRenderTarget> RenderTarget::normalize(std::optional<Entity> primary) const {
    return std::visit(
        utils::visitor{[&](const window::WindowRef& win_ref) -> std::optional<NormalizedRenderTarget> {
                           return win_ref.normalize(primary).transform(
                               [](window::NormalizedWindowRef window) { return NormalizedRenderTarget(window); });
                       },
                       [&](const ImageRenderTarget& target) -> std::optional<NormalizedRenderTarget> {
                           return NormalizedRenderTarget(target);
                       },
                       [&](const ManualTextureViewHandle& target) -> std::optional<NormalizedRenderTarget> {
                           return NormalizedRenderTarget(target);
                       },
                       [&](const NoColorTarget& target) -> std::optional<NormalizedRenderTarget> {
                           return NormalizedRenderTarget(target);
                       }},
        *this);
}

RenderTargetId NormalizedRenderTarget::identity() const noexcept {
    return std::visit(
        utils::visitor{
            [](const ImageRenderTarget& image) -> RenderTargetId {
                return RenderTargetId{1, reinterpret_cast<std::uintptr_t>(image.texture.raw())};
            },
            [](const window::NormalizedWindowRef& w) -> RenderTargetId { return RenderTargetId{0, w.entity().uid}; },
            [](const ManualTextureViewHandle& handle) -> RenderTargetId { return RenderTargetId{2, handle.id}; },
            [](const NoColorTarget& target) -> RenderTargetId {
                return RenderTargetId{3, (std::uint64_t{target.size.x} << 32) | target.size.y};
            },
        },
        *this);
}

void OrthographicProjection::update(float width, float height) {
    float projection_width  = rect.right - rect.left;
    float projection_height = rect.top - rect.bottom;
    std::visit(utils::visitor{[&](const ScalingMode::Fixed& fixed) {
                                  projection_width  = fixed.width;
                                  projection_height = fixed.height;
                              },
                              [&](const ScalingMode::WindowSize&) {
                                  projection_width  = width;
                                  projection_height = height;
                              },
                              [&](const ScalingMode::AutoMin& auto_min) {
                                  if (width * auto_min.min_height > auto_min.min_width * height) {
                                      projection_width  = width * auto_min.min_height / height;
                                      projection_height = auto_min.min_height;
                                  } else {
                                      projection_width  = auto_min.min_width;
                                      projection_height = height * auto_min.min_width / width;
                                  }
                              },
                              [&](const ScalingMode::AutoMax& auto_max) {
                                  if (width * auto_max.max_height < auto_max.max_width * height) {
                                      projection_width  = width * auto_max.max_height / height;
                                      projection_height = auto_max.max_height;
                                  } else {
                                      projection_width  = auto_max.max_width;
                                      projection_height = height * auto_max.max_width / width;
                                  }
                              },
                              [&](const ScalingMode::FixedVertical& fixed_vertical) {
                                  projection_height = fixed_vertical.viewport_height;
                                  projection_width  = width * fixed_vertical.viewport_height / height;
                              },
                              [&](const ScalingMode::FixedHorizontal& fixed_horizontal) {
                                  projection_width  = fixed_horizontal.viewport_width;
                                  projection_height = height * fixed_horizontal.viewport_width / width;
                              }},
               scaling_mode);
    rect.left   = -projection_width * viewport_origin.x * scale;
    rect.right  = projection_width * scale + rect.left;
    rect.bottom = -projection_height * viewport_origin.y * scale;
    rect.top    = projection_height * scale + rect.bottom;
}

// ==== Visibility systems (Bevy bevy_camera::visibility) ====

void visibility_propagate_system(
    // The changed query mirrors Bevy's propagation trigger exactly.
    Query<Item<Entity, const Visibility&, Opt<const Parent&>, Opt<const Children&>>,
          Filter<With<InheritedVisibility>, Or<Modified<Visibility>, Modified<Parent>>>> changed,
    Query<Item<const Visibility&, Mut<InheritedVisibility>>> visibility_query,
    Query<Item<const Children&>> children_query) {
    auto propagate = [&](auto&& self, bool parent_is_visible, Entity entity) -> void {
        auto item = visibility_query.get(entity);
        if (!item) return;

        auto&& [visibility, inherited] = *item;
        const bool is_visible = visibility.type == Visibility::Type::Visible
                                    ? true
                                    : visibility.type == Visibility::Type::Hidden ? false : parent_is_visible;
        if (inherited.get().get() != is_visible) VisibilityPropagationAccess::set(inherited.get_mut(), is_visible);
        if (auto children = children_query.get(entity)) {
            for (const Entity child : std::get<0>(*children).entities()) {
                self(self, is_visible, child);
            }
        }
    };

    for (auto&& [entity, visibility, parent, children] : changed.iter()) {
        const bool is_visible = visibility.type == Visibility::Type::Visible
                                    ? true
                                    : visibility.type == Visibility::Type::Hidden
                                          ? false
                                          : parent.and_then([&](const auto& child_of) {
                                                       return visibility_query.get(child_of.get().entity()).transform(
                                                            [](const auto& parent_visibility) {
                                                                return std::get<1>(parent_visibility).get().get();
                                                            });
                                                   }).value_or(true);
        if (auto own_visibility = visibility_query.get(entity);
            own_visibility && std::get<1>(*own_visibility).get().get() != is_visible) {
            VisibilityPropagationAccess::set(std::get<1>(*own_visibility).get_mut(), is_visible);
            if (children) {
                for (const Entity child : children->get().entities()) {
                    propagate(propagate, is_visible, child);
                }
            }
        }
    }
}

void reset_view_visibility(Query<Item<Mut<ViewVisibility>>> view_visibilities) {
    for (auto&& [view_visibility] : view_visibilities.iter()) {
        // Bevy ViewVisibility::update: current visibility becomes the
        // previous-frame scratch bit and the current bit is cleared.
        view_visibility.bypass_change_detection().update();
    }
}

void set_view_visible(Mut<ViewVisibility>& visibility) noexcept {
    // Bevy SetViewVisibility for Mut<ViewVisibility>: a previous-frame
    // visible entity that remains visible must not spuriously trigger change
    // detection, even when multiple views mark it during the frame.
    if (visibility.get().get()) return;
    if (visibility.get().was_visible_now_hidden()) {
        visibility.bypass_change_detection().set_visible();
    } else {
        visibility.get_mut().set_visible();
    }
}

void mark_newly_hidden_entities_invisible(Query<Item<Mut<ViewVisibility>>> view_visibilities) {
    for (auto&& [view_visibility] : view_visibilities.iter()) {
        if (view_visibility.get().was_visible_now_hidden()) {
            view_visibility.get_mut() = ViewVisibility::hidden();
        }
    }
}

void check_visibility_ranges(
    ResMut<VisibleEntityRanges> visible_entity_ranges,
    Query<Item<Entity, const ::epix::transform::GlobalTransform&>, With<Camera>> cameras,
    Query<Item<Entity, const ::epix::transform::GlobalTransform&, Opt<const Aabb&>, const VisibilityRange&>> entities) {
    visible_entity_ranges->clear();
    if (entities.iter().max_remaining() == 0) return;

    std::vector<std::pair<Entity, glm::vec3>> views;
    for (auto&& [entity, transform] : cameras.iter()) {
        if (views.size() == 32) break;
        const auto index = static_cast<std::uint8_t>(views.size());
        visible_entity_ranges->views.emplace(entity, index);
        views.emplace_back(entity, glm::vec3(transform.matrix[3]));
    }
    for (auto&& [entity, transform, opt_aabb, range] : entities.iter()) {
        std::uint32_t visibility = 0;
        glm::vec3 model_position = glm::vec3(transform.matrix[3]);
        if (range.use_aabb && opt_aabb) {
            model_position = glm::vec3(transform.matrix * glm::vec4(opt_aabb->get().center, 1.0f));
        }
        for (std::size_t index = 0; index < views.size(); ++index) {
            if (range.is_visible_at_all(glm::length(views[index].second - model_position))) {
                visibility |= std::uint32_t{1} << index;
            }
        }
        if (visibility != 0) visible_entity_ranges->entities.emplace(entity, visibility);
    }
}

void check_visibility_system(Query<Item<Entity,
                                        const Camera&,
                                        Mut<VisibleEntities>,
                                        Opt<const RenderLayers&>,
                                        const Frustum&,
                                        Opt<const NoCpuCulling&>>> cameras,
                             Query<Item<Entity,
                                        const InheritedVisibility&,
                                        Mut<ViewVisibility>,
                                        Opt<const VisibilityClass&>,
                                        Opt<const RenderLayers&>,
                                        Opt<const Aabb&>,
                                        Opt<const ::epix::transform::GlobalTransform&>,
                                        Opt<const NoFrustumCulling&>,
                                        Opt<const VisibilityRange&>>> entities,
                             Res<VisibleEntityRanges> visible_entity_ranges) {
    // Bevy check_visibility: for each camera, mark entities visible to it in
    // its view slot and collect them into the camera's VisibleEntities. When
    // bounds are available, use its sphere broad phase followed by a
    // transformed-AABB frustum test.
    for (auto&& [camera_entity, camera, visible_entities, opt_camera_layers, frustum, no_cpu_culling] :
         cameras.iter()) {
        (void)camera_entity;
        if (!camera.is_active) continue;
        const auto& camera_layers = opt_camera_layers ? *opt_camera_layers : RenderLayers::layer(0);
        visible_entities.get_mut().clear_all();
        for (auto&& [entity, inherited, view_visibility, opt_classes, opt_layers, opt_aabb, opt_transform,
                     no_frustum_culling, opt_visibility_range] : entities.iter()) {
            if (!inherited.get()) continue;
            const auto& entity_layers = opt_layers ? *opt_layers : RenderLayers::layer(0);
            if (!camera_layers.intersects(entity_layers)) continue;
            if (opt_visibility_range && !visible_entity_ranges->entity_is_in_range_of_view(entity, camera_entity)) {
                continue;
            }
            if (!no_cpu_culling && !no_frustum_culling && opt_aabb && opt_transform) {
                const auto& aabb       = opt_aabb->get();
                const auto& transform  = opt_transform->get().matrix;
                const glm::vec3 center = glm::vec3(transform * glm::vec4(aabb.center, 1.0f));
                const float radius     = glm::length(glm::abs(glm::mat3(transform)) * aabb.half_extents);
                if (!frustum.intersects_sphere(Sphere{center, radius}, false) ||
                    !frustum.intersects_obb(aabb, transform, true, false)) {
                    continue;
                }
            }
            set_view_visible(view_visibility);
            if (opt_classes) {
                for (const auto& visibility_class : opt_classes->get().classes) {
                    visible_entities.get_mut().push(entity, visibility_class);
                }
            }
        }
    }
}

void VisibilityRangePlugin::attach(App& app) {
    app.world_mut().init_resource<VisibleEntityRanges>();
    app.add_systems(app::PostUpdate, into(check_visibility_ranges)
                                         .in_set(VisibilitySystems::CheckVisibility)
                                         .before(check_visibility_system)
                                         .set_name("check visibility ranges"));
}

void update_frusta(
    Query<Item<const ::epix::transform::GlobalTransform&, const ::epix::camera::Projection&, Mut<Frustum>>> cameras) {
    for (auto&& [gtransform, projection, frustum] : cameras.iter()) {
        frustum.get_mut() = projection.compute_frustum(gtransform);
    }
}

void CameraProjectionPlugin::attach(App& app) {
    app.configure_sets(app::PostUpdate,
                       sets(VisibilitySystems::UpdateFrusta)
                           .after(::epix::transform::TransformSystems::Propagate)
                           .after(CameraUpdateSystems::CameraUpdateSystem));
    app.add_systems(app::PostUpdate, into(update_frusta)
                                         .in_set(VisibilitySystems::UpdateFrusta)
                                         .set_name("update frusta"));
}

void VisibilityPlugin::attach(App& app) {
    // Epix requires every set label to be registered explicitly before it can
    // participate in another set's ordering edge.
    app.configure_sets(app::PostUpdate,
                       sets(VisibilitySystems::CalculateBounds,
                            VisibilitySystems::UpdateFrusta,
                            VisibilitySystems::VisibilityPropagate,
                            VisibilitySystems::CheckVisibility,
                            VisibilitySystems::MarkNewlyHiddenEntitiesInvisible));
    app.configure_sets(app::PostUpdate,
                       sets(VisibilitySystems::UpdateFrusta, VisibilitySystems::VisibilityPropagate)
                           .before(VisibilitySystems::CheckVisibility)
                           .after(::epix::transform::TransformSystems::Propagate));
    app.configure_sets(app::PostUpdate,
                       sets(VisibilitySystems::MarkNewlyHiddenEntitiesInvisible)
                           .after(VisibilitySystems::CheckVisibility));
    app.configure_sets(app::PostUpdate,
                       sets(VisibilitySystems::CalculateBounds)
                           .before(VisibilitySystems::CheckVisibility)
                           .after(::epix::transform::TransformSystems::Propagate));
    // Bevy: Visibility requires InheritedVisibility + ViewVisibility
    // (visibility/mod.rs:151-166); required components are auto-added on spawn.
    app.world_mut().register_required_components<Visibility, InheritedVisibility>();
    app.world_mut().register_required_components<Visibility, ViewVisibility>();
    app.add_systems(app::PostUpdate, into(visibility_propagate_system)
                                         .in_set(VisibilitySystems::VisibilityPropagate)
                                         .set_name("visibility propagate"));
    app.add_systems(app::PostUpdate,
                    into(reset_view_visibility)
                        .in_set(VisibilitySystems::VisibilityPropagate)
                        .set_name("reset view visibility"));
    app.add_systems(app::PostUpdate, into(check_visibility_system)
                                         .in_set(VisibilitySystems::CheckVisibility)
                                         .set_name("check visibility"));
    app.add_systems(app::PostUpdate, into(mark_newly_hidden_entities_invisible)
                                         .in_set(VisibilitySystems::MarkNewlyHiddenEntitiesInvisible)
                                         .set_name("mark newly hidden entities invisible"));
}

void CameraPlugin::attach(App& app) {
    // Camera projection updates include Bevy-compatible sub-camera cropping.
    app.configure_sets(sets(CameraUpdateSystems::CameraUpdateSystem));
    app.add_plugins(CameraProjectionPlugin{}, VisibilityPlugin{}, VisibilityRangePlugin{});
    // The public camera module can run without the renderer, so it owns the
    // window messages consumed by its Bevy-compatible update system.
    app.add_events<::epix::window::WindowResized, ::epix::window::WindowCreated,
                   ::epix::window::WindowScaleFactorChanged>();
    app.add_systems(app::PostUpdate, into(camera_system).in_set(CameraUpdateSystems::CameraUpdateSystem));
    // ClearColor extraction to the render world is registered by the render
    // module's RenderPlugin, like bevy_render.
    // Bevy CameraPlugin::build uses init_resource, preserving an application
    // clear color configured before the plugin is attached.
    app.world_mut().init_resource<ClearColor>();
}
}  // namespace epix::camera
