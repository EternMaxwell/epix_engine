#include "epix/render/camera.h"
#include "epix/render/view.h"

using namespace epix;
using namespace epix::render::view;

EPIX_API void render::view::prepare_view_target(Query<Item<Entity, camera::ExtractedCamera, ExtractedView>> views,
                                                Commands& cmd,
                                                Res<window::ExtractedWindows> extracted_windows) {
    // Prepare the view target for each extracted camera view
    for (auto&& [entity, camera, view] : views.iter()) {
        std::optional<nvrhi::TextureHandle> target_texture = std::visit(
            epix::util::visitor{
                [&](const nvrhi::TextureHandle& tex) -> std::optional<nvrhi::TextureHandle> { return tex; },
                [&](const camera::WindowRef& win_ref) -> std::optional<nvrhi::TextureHandle> {
                    auto&& id = win_ref.window_entity;
                    if (auto it = extracted_windows->windows.find(id); it != extracted_windows->windows.end()) {
                        return it->second.swapchain_texture;
                    } else {
                        return std::nullopt;
                    }
                }},
            camera.render_target);
        if (!target_texture.has_value() || !target_texture.value()) {
            // invalid target texture, handle error;
            // no need to remove the entity, it will just be missing ViewTarget.
            continue;
        }
        cmd.entity(entity).emplace<view::ViewTarget>(target_texture.value());
    }
}

EPIX_API void render::view::ViewPlugin::build(App& app) {
    if (auto sub_app = app.get_sub_app(Render)) {
        sub_app->add_systems(Render, into(prepare_view_target)
                                         .after(window::prepare_windows)
                                         .in_set(RenderSet::ManageViews)
                                         .set_names({"prepare view targets"}));
    }
}