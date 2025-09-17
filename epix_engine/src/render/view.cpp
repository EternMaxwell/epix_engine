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

void render::view::create_view_depth(Query<Item<Entity, ExtractedView>> views,
                                     Res<nvrhi::DeviceHandle> device,
                                     ResMut<ViewDepthCache> depth_cache,
                                     Commands& cmd) {
    for (auto&& [entity, view] : views.iter()) {
        glm::uvec2 size = view.viewport_size;
        if (size.x == 0 || size.y == 0) {
            continue;  // invalid size
        }
        // create new depth texture
        nvrhi::TextureDesc desc;
        desc.width            = size.x;
        desc.height           = size.y;
        desc.format           = nvrhi::Format::D32;
        desc.dimension        = nvrhi::TextureDimension::Texture2D;
        desc.isRenderTarget   = true;
        desc.debugName        = "ViewDepth";
        desc.initialState     = nvrhi::ResourceStates::DepthWrite;
        desc.keepInitialState = true;  // keep initial state to avoid transition when used as read-only
        auto texture          = device.get()->createTexture(desc);
        if (!texture) {
            spdlog::error("Failed to create depth texture for view with size {}x{}", size.x, size.y);
            continue;
        }
        cmd.entity(entity).emplace<view::ViewDepth>(texture);
    }
}

void clear_cache(ResMut<ViewDepthCache> depth_cache) { depth_cache->cache.clear(); }

void recycle_depth(Query<Item<ViewDepth>> depths, ResMut<ViewDepthCache> depth_cache) {
    for (auto&& [depth] : depths.iter()) {
        if (depth.texture) {
            auto desc = depth.texture->getDesc();
            glm::uvec2 size{desc.width, desc.height};
            depth_cache->cache[size] = depth.texture;
        }
    }
}

EPIX_API void render::view::ViewPlugin::build(App& app) {
    if (auto sub_app = app.get_sub_app(Render)) {
        sub_app->insert_resource(ViewDepthCache{});
        sub_app->add_systems(Render, into(prepare_view_target, create_view_depth)
                                         .after(window::prepare_windows)
                                         .in_set(RenderSet::ManageViews)
                                         .set_names({"prepare view targets", "create view depths"}));
        sub_app->add_systems(Render, into(clear_cache).after(RenderSet::ManageViews));
        sub_app->add_systems(Render, into(recycle_depth).after(RenderSet::Render).before(RenderSet::Cleanup));
    }
}