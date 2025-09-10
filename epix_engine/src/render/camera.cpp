#include "epix/render/camera.h"

using namespace epix::render::camera;

EPIX_API std::optional<RenderTarget> RenderTarget::normalize(std::optional<Entity> primary) const {
    return std::visit(
        epix::util::visitor{[&](const nvrhi::TextureHandle& tex) -> std::optional<RenderTarget> { return *this; },
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

EPIX_API void OrthographicProjection::update(float width, float height) {
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
    rect.right  = projection_width * (1.0f - viewport_origin.x) * scale + rect.left;
    rect.bottom = -projection_height * viewport_origin.y * scale;
    rect.top    = projection_height * (1.0f - viewport_origin.y) * scale + rect.bottom;
}

EPIX_API void epix::render::camera::extract_cameras(
    Commands& cmd,
    std::optional<Res<ClearColor>> global_clear_color,
    Extract<Query<Get<Camera, CameraRenderGraph, transform::GlobalTransform, view::VisibleEntities>>> cameras,
    Extract<Query<Get<Entity>, With<epix::window::PrimaryWindow, epix::window::Window>>> primary_window) {
    // extract camera entities to render world, this will spawn an related
    // entity with ExtractedCamera, ExtractedView and other components.

    auto primary = primary_window.get_single().transform([](auto&& tup) { return std::get<0>(tup); });

    for (auto&& [camera, graph, gtransform, visible_entities] : cameras.iter()) {
        if (!camera.active) continue;
        auto target_size = camera.get_target_size();
        if (target_size.x == 0 || target_size.y == 0) continue;
        auto normalized_target = camera.render_target.normalize(primary);
        if (!normalized_target.has_value()) continue;
        auto viewport_size   = camera.get_viewport_size();
        auto viewport_origin = camera.get_viewport_origin();

        auto commands = cmd.spawn();
        commands.emplace(ExtractedCamera{
            .render_target = *normalized_target,
            .viewport_size = viewport_size,
            .target_size   = target_size,
            .viewport      = camera.viewport,
            .render_graph  = graph,
            .order         = camera.order,
            .clear_color   = [&]() -> std::optional<ClearColor> {
                if (camera.clear_color.type == ClearColorConfig::Type::Global ||
                    camera.clear_color.type == ClearColorConfig::Type::Default) {
                    return global_clear_color.transform([](const Res<ClearColor>& color) { return *color; });
                } else if (camera.clear_color.type == ClearColorConfig::Type::Custom) {
                    return ClearColor(camera.clear_color.clear_color);
                } else {
                    return std::nullopt;
                }
            }(),
        });
        commands.emplace(view::ExtractedView{
            .projection      = camera.computed.projection,
            .transform       = gtransform,
            .viewport_size   = viewport_size,
            .viewport_origin = viewport_origin,
        });
        commands.emplace(visible_entities);
    }
}

EPIX_API void CameraDriverNode::run(graph::GraphContext& graph, graph::RenderContext& render_ctx, app::World& world) {
    Query<Get<Entity, ExtractedCamera, view::ViewTarget>> cameras(world);
    auto&& windows = world.resource<epix::render::window::ExtractedWindows>();
    for (auto&& [entity, camera, target] : cameras.iter()) {
        if (camera.clear_color) {
            auto&& commandlist = render_ctx.commands();
            commandlist->clearTextureFloat(target.texture, nvrhi::TextureSubresourceSet(),
                                           nvrhi::Color{camera.clear_color->r, camera.clear_color->g,
                                                        camera.clear_color->b, camera.clear_color->a});
        }
        if (!graph.run_sub_graph(camera.render_graph, {}, entity)) {
            spdlog::warn("Failed to run camera render graph for entity {:#x}, with render graph label {}",
                         entity.index(), camera.render_graph.name());
        }
    }
}

EPIX_API void CameraPlugin::build(App& app) {
    app.add_plugins(CameraProjectionPlugin<Projection>{}, CameraProjectionPlugin<OrthographicProjection>{},
                    CameraProjectionPlugin<PerspectiveProjection>{}, ExtractResourcePlugin<ClearColor>{});
    if (auto sub_app = app.get_sub_app(Render)) {
        sub_app->insert_resource(ClearColor{0.05f, 0.05f, 0.05f, 1.0f});
        sub_app->add_systems(ExtractSchedule, into(extract_cameras));
        if (auto render_graph = sub_app->get_resource<graph::RenderGraph>()) {
            render_graph->add_node(CameraDriverNodeLabel, CameraDriverNode{});
        }
    }
}