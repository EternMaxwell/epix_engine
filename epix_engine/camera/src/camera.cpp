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
    return std::visit(utils::visitor{[&](const WindowRef& win_ref) -> std::optional<NormalizedRenderTarget> {
                                         if (win_ref.primary) {
                                             if (primary.has_value()) {
                                                 return NormalizedRenderTarget(WindowRef{false, primary.value()});
                                             } else {
                                                 return std::nullopt;
                                             }
                                         } else {
                                             return NormalizedRenderTarget(win_ref);
                                         }
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
    return std::visit(utils::visitor{
                          [](const ImageRenderTarget& image) -> RenderTargetId {
                              return RenderTargetId{1, reinterpret_cast<std::uintptr_t>(image.texture.raw())};
                          },
                          [](const WindowRef& w) -> RenderTargetId { return RenderTargetId{0, w.window_entity.uid}; },
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
    scaling_mode
        .on_fixed([&](float& fixed_width, float& fixed_height) {
            projection_width  = fixed_width;
            projection_height = fixed_height;
        })
        .on_window_size([&](float& pixels_per_unit) {
            projection_width  = width / pixels_per_unit;
            projection_height = height / pixels_per_unit;
        })
        .on_auto_min([&](float& min_width, float& min_height) {
            if (width * min_height > min_width * height) {
                projection_width  = width * min_height / height;
                projection_height = min_height;
            } else {
                projection_width  = min_width;
                projection_height = height * min_width / width;
            }
        })
        .on_auto_max([&](float& max_width, float& max_height) {
            if (width * max_height < max_width * height) {
                projection_width  = width * max_height / height;
                projection_height = max_height;
            } else {
                projection_width  = max_width;
                projection_height = height * max_width / width;
            }
        })
        .on_fixed_vertical([&](float& vertical) {
            projection_height = vertical;
            projection_width  = width * vertical / height;
        })
        .on_fixed_horizontal([&](float& horizontal) {
            projection_width  = horizontal;
            projection_height = height * horizontal / width;
        });
    rect.left   = -projection_width * viewport_origin.x * scale;
    rect.right  = projection_width * scale + rect.left;
    rect.bottom = -projection_height * viewport_origin.y * scale;
    rect.top    = projection_height * scale + rect.bottom;
}

// ==== Visibility systems (Bevy bevy_camera::visibility) ====

void visibility_propagate_system(Query<Item<const Visibility&, Mut<InheritedVisibility>>> visibilities) {
    // epix has no entity hierarchy (ChildOf) yet, so inherited visibility is
    // the entity's own state: Hidden hides, Visible/Inherited show (Bevy's
    // visibility_propagate_system collapses to this for hierarchy roots).
    for (auto&& [visibility, inherited] : visibilities.iter()) {
        inherited.get_mut().is_visible = visibility.type != Visibility::Type::Hidden;
    }
}

void reset_view_visibility(Query<Item<Mut<ViewVisibility>>> view_visibilities) {
    for (auto&& [view_visibility] : view_visibilities.iter()) {
        // Bevy ViewVisibility::update: current visibility becomes the
        // previous-frame scratch bit and the current bit is cleared.
        view_visibility.get_mut().update();
    }
}

void mark_newly_hidden_entities_invisible(Query<Item<Mut<ViewVisibility>>> view_visibilities) {
    for (auto&& [view_visibility] : view_visibilities.iter()) {
        if (view_visibility.get().was_visible_now_hidden()) {
            view_visibility.get_mut().flags = 0;
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

void check_visibility_system(
    Query<Item<Entity,
               const Camera&,
               Mut<VisibleEntities>,
               Opt<const RenderLayers&>,
               const Frustum&,
               Opt<const NoCpuCulling&>>>
        cameras,
    Query<Item<Entity,
               const InheritedVisibility&,
               Mut<ViewVisibility>,
               Opt<const VisibilityClass&>,
               Opt<const RenderLayers&>,
               Opt<const Aabb&>,
               Opt<const ::epix::transform::GlobalTransform&>,
               Opt<const NoFrustumCulling&>,
               Opt<const VisibilityRange&>>>
        entities,
    Res<VisibleEntityRanges> visible_entity_ranges) {
    // Bevy check_visibility: for each camera, mark entities visible to it in
    // its view slot and collect them into the camera's VisibleEntities. When
    // bounds are available, use its sphere broad phase followed by a
    // transformed-AABB frustum test.
    for (auto&& [camera_entity, camera, visible_entities, opt_camera_layers, frustum, no_cpu_culling] : cameras.iter()) {
        (void)camera_entity;
        if (!camera.is_active) continue;
        const auto& camera_layers = opt_camera_layers ? *opt_camera_layers : RenderLayers::layer(0);
        visible_entities.get_mut().clear_all();
        for (auto&& [entity, inherited, view_visibility, opt_classes, opt_layers, opt_aabb, opt_transform,
                     no_frustum_culling, opt_visibility_range] : entities.iter()) {
            if (!inherited.is_visible) continue;
            const auto& entity_layers = opt_layers ? *opt_layers : RenderLayers::layer(0);
            if (!camera_layers.intersects(entity_layers)) continue;
            if (opt_visibility_range && !visible_entity_ranges->entity_is_in_range_of_view(entity, camera_entity)) {
                continue;
            }
            if (!no_cpu_culling && !no_frustum_culling && opt_aabb && opt_transform) {
                const auto& aabb      = opt_aabb->get();
                const auto& transform = opt_transform->get().matrix;
                const glm::vec3 center = glm::vec3(transform * glm::vec4(aabb.center, 1.0f));
                const float radius = glm::length(glm::abs(glm::mat3(transform)) * aabb.half_extents);
                if (!frustum.intersects_sphere(Sphere{center, radius}, false) ||
                    !frustum.intersects_obb(aabb, transform, true, false)) {
                    continue;
                }
            }
            view_visibility.get_mut().visible();
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
    app.add_systems(app::PostUpdate,
                    into(check_visibility_ranges)
                        .in_set(VisibilitySystems::CheckVisibility)
                        .before(check_visibility_system)
                        .set_name("check visibility ranges"));
}

void update_frusta(Query<Item<const ::epix::transform::GlobalTransform&, const ::epix::camera::Projection&, Mut<Frustum>>>
                        cameras) {
    // Bevy Projection::compute_frustum uses the explicit far distance even
    // when the projection matrix is infinite reverse-Z.  The matrix alone
    // cannot recover that finite culling bound.
    for (auto&& [gtransform, projection, frustum] : cameras.iter()) {
        const glm::mat4 clip_from_world = projection.get_projection_matrix() * glm::inverse(gtransform.matrix);
        const glm::vec3 translation     = glm::vec3(gtransform.matrix[3]);
        glm::vec3 backward               = glm::vec3(gtransform.matrix[2]);
        const float backward_length      = glm::length(backward);
        if (backward_length > 0.0f && std::isfinite(backward_length)) {
            backward /= backward_length;
        } else {
            backward = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        frustum.get_mut() = Frustum::from_clip_from_world_custom_far(clip_from_world, translation, backward,
                                                                       projection.get_far());
    }
}

void CameraPlugin::attach(App& app) {
    // Camera projection updates include Bevy-compatible sub-camera cropping.
    app.configure_sets(sets(CameraUpdateSystems::CameraUpdateSystem));
    app.configure_sets(sets(VisibilitySystems::CalculateBounds));
    app.configure_sets(sets(VisibilitySystems::UpdateFrusta));
    app.configure_sets(sets(VisibilitySystems::VisibilityPropagate));
    app.configure_sets(sets(VisibilitySystems::CheckVisibility));
    app.configure_sets(sets(VisibilitySystems::MarkNewlyHidden));
    // Bevy: Visibility requires InheritedVisibility + ViewVisibility
    // (visibility/mod.rs:151-166); required components are auto-added on spawn.
    app.world_mut().register_required_components<Visibility, InheritedVisibility>();
    app.world_mut().register_required_components<Visibility, ViewVisibility>();
    VisibilityRangePlugin{}.attach(app);
    app.add_systems(app::PostUpdate,
                    into(visibility_propagate_system)
                        .in_set(VisibilitySystems::VisibilityPropagate)
                        .set_name("visibility propagate"));
    app.add_systems(app::PostUpdate,
                    into(reset_view_visibility).in_set(VisibilitySystems::CheckVisibility).set_name("reset view visibility"));
    app.add_systems(app::PostUpdate,
                    into(update_frusta)
                        .after(CameraUpdateSystems::CameraUpdateSystem)
                        .in_set(VisibilitySystems::UpdateFrusta)
                        .set_name("update frusta"));
    app.add_systems(app::PostUpdate, into(check_visibility_system)
                                         .after(update_frusta)
                                         .after(visibility_propagate_system)
                                         .after(reset_view_visibility)
                                         .in_set(VisibilitySystems::CheckVisibility)
                                         .set_name("check visibility"));
    app.add_systems(app::PostUpdate,
                    into(mark_newly_hidden_entities_invisible)
                        .after(check_visibility_system)
                        .in_set(VisibilitySystems::MarkNewlyHidden)
                        .set_name("mark newly hidden entities invisible"));
    app.add_plugins(CameraProjectionPlugin<Projection>{}, CameraProjectionPlugin<OrthographicProjection>{},
                    CameraProjectionPlugin<PerspectiveProjection>{});
    // ClearColor extraction to the render world is registered by the render
    // module's RenderPlugin, like bevy_render.
    app.world_mut().insert_resource(ClearColor{});
}
}  // namespace epix::camera
