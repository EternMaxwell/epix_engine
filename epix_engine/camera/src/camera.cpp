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
    registrator.register_required<Projection>([] { return Projection{}; });
    registrator.register_required<::epix::transform::Transform>([] { return ::epix::transform::Transform{}; });
    registrator.register_required<VisibleEntities>([] { return VisibleEntities{}; });
    // Bevy: a camera without RenderLayers sees layer 0 (render_layers.rs:45-52).
    registrator.register_required<RenderLayers>([] { return RenderLayers::layer(0); });
    // Bevy registers Msaa as a required component of Camera (camera.rs:56)
    // with Msaa::default() = Sample4.
    registrator.register_required<Msaa>([] { return Msaa::Sample4; });
    registrator.register_required<CameraMainTextureUsages>([] { return CameraMainTextureUsages{}; });
    // Bevy Camera requires Frustum; update_frusta fills it.
    registrator.register_required<Frustum>([] { return Frustum{}; });
}

std::optional<RenderTarget> RenderTarget::normalize(std::optional<Entity> primary) const {
    return std::visit(utils::visitor{[&](const wgpu::Texture& tex) -> std::optional<RenderTarget> { return *this; },
                                     [&](const WindowRef& win_ref) -> std::optional<RenderTarget> {
                                         if (win_ref.primary) {
                                             if (primary.has_value()) {
                                                 return RenderTarget(WindowRef{false, primary.value()});
                                             } else {
                                                 return std::nullopt;
                                             }
                                         } else {
                                             return *this;
                                         }
                                     }},
                      *this);
}

RenderTargetId RenderTarget::identity() const noexcept {
    return std::visit(utils::visitor{
                          [](const wgpu::Texture& tex) -> RenderTargetId {
                              return RenderTargetId{reinterpret_cast<std::uintptr_t>(tex.raw())};
                          },
                          [](const WindowRef& w) -> RenderTargetId { return RenderTargetId{w.window_entity.uid}; },
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
    // Bevy reset_view_visibility: everything starts visible-by-default
    // (bit 0 clear); check_visibility culls entities hidden for every view.
    for (auto&& [view_visibility] : view_visibilities.iter()) {
        view_visibility.get_mut().flags = 0;
    }
}

void check_visibility_system(
    Query<Item<Entity, const Camera&, Mut<VisibleEntities>, const RenderLayers&, const Frustum&>> cameras,
    Query<Item<Entity, const InheritedVisibility&, Mut<ViewVisibility>, Opt<const RenderLayers&>>> entities) {
    // Bevy check_visibility: for each camera, mark entities visible to it in
    // its view slot and collect them into the camera's VisibleEntities.
    // Frustum culling is not applied yet — epix has no per-entity bounds; the
    // Frustum is kept in the query for that work.
    constexpr std::uint32_t kMaxViews = 15;  // ViewVisibility has 16 per-view bits
    const auto visibility_class       = ::epix::meta::type_index(::epix::meta::type_id<Visibility>());
    std::unordered_set<Entity> visible_anywhere;
    std::size_t view_index = 0;
    for (auto&& [camera_entity, camera, visible_entities, camera_layers, frustum] : cameras.iter()) {
        (void)camera_entity;
        (void)camera;
        (void)frustum;
        if (view_index >= kMaxViews) {
            spdlog::warn("[camera] More than {} cameras; visibility bits truncated.", kMaxViews);
            break;
        }
        auto& class_entities = visible_entities.get_mut().get_mut(visibility_class);
        for (auto&& [entity, inherited, view_visibility, opt_layers] : entities.iter()) {
            if (!inherited.is_visible) continue;
            const auto& entity_layers = opt_layers ? *opt_layers : RenderLayers::layer(0);
            if (!camera_layers.intersects(entity_layers)) continue;
            view_visibility.get_mut().set_in_view(view_index, true);
            visible_anywhere.insert(entity);
            class_entities.push_back(entity);
        }
        ++view_index;
    }
    // Entities visible to no view are culled (bit 0 set -> get() == false).
    for (auto&& [entity, inherited, view_visibility, opt_layers] : entities.iter()) {
        (void)inherited;
        (void)opt_layers;
        if (!visible_anywhere.contains(entity)) {
            view_visibility.get_mut().culled();
        }
    }
}

void update_frusta(Query<Item<const Camera&, const ::epix::transform::GlobalTransform&, Mut<Frustum>>> cameras) {
    // Bevy update_frusta: clip_from_world = projection * inverse(transform).
    for (auto&& [camera, gtransform, frustum] : cameras.iter()) {
        const glm::mat4 clip_from_world = camera.computed.projection * glm::inverse(gtransform.matrix);
        frustum.get_mut()               = Frustum::from_view_projection(clip_from_world);
    }
}

void CameraPlugin::attach(App& app) {
    // Camera projection updates include Bevy-compatible sub-camera cropping.
    app.configure_sets(sets(CameraUpdateSystems::CameraUpdateSystem));
    // Bevy: Visibility requires InheritedVisibility + ViewVisibility
    // (visibility/mod.rs:151-166); required components are auto-added on spawn.
    app.world_mut().register_required_components<Visibility, InheritedVisibility>();
    app.world_mut().register_required_components<Visibility, ViewVisibility>();
    app.add_systems(app::PostUpdate, into(visibility_propagate_system).set_name("visibility propagate"));
    app.add_systems(app::PostUpdate, into(reset_view_visibility).set_name("reset view visibility"));
    app.add_systems(app::PostUpdate,
                    into(update_frusta).after(CameraUpdateSystems::CameraUpdateSystem).set_name("update frusta"));
    app.add_systems(app::PostUpdate, into(check_visibility_system)
                                         .after(update_frusta)
                                         .after(visibility_propagate_system)
                                         .after(reset_view_visibility)
                                         .set_name("check visibility"));
    app.add_plugins(CameraProjectionPlugin<Projection>{}, CameraProjectionPlugin<OrthographicProjection>{},
                    CameraProjectionPlugin<PerspectiveProjection>{});
    // ClearColor extraction to the render world is registered by the render
    // module's RenderPlugin, like bevy_render.
    app.world_mut().insert_resource(ClearColor{});
}
}  // namespace epix::camera
