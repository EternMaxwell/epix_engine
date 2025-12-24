#include "epix/render/camera.hpp"
#include "epix/render/extract.hpp"
#include "epix/render/schedule.hpp"
#include "epix/render/view.hpp"

using namespace epix::render::view;
using namespace epix::render;
using namespace epix;

void render::view::prepare_view_target(Query<Item<Entity, const camera::ExtractedCamera&, const ExtractedView&>> views,
                                       Commands cmd,
                                       Res<window::ExtractedWindows> extracted_windows) {
    // Prepare the view target for each extracted camera view
    for (auto&& [entity, camera, view] : views.iter()) {
        std::optional<nvrhi::TextureHandle> target_texture = std::visit(
            assets::visitor{[&](const nvrhi::TextureHandle& tex) -> std::optional<nvrhi::TextureHandle> { return tex; },
                            [&](const camera::WindowRef& win_ref) -> std::optional<nvrhi::TextureHandle> {
                                auto&& id = win_ref.window_entity;
                                if (auto it = extracted_windows->windows.find(id);
                                    it != extracted_windows->windows.end()) {
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
        cmd.entity(entity).insert(view::ViewTarget{target_texture.value()});
    }
}

void render::view::create_view_depth(Query<Item<Entity, const ExtractedView&>> views,
                                     Res<nvrhi::DeviceHandle> device,
                                     ResMut<ViewDepthCache> depth_cache,
                                     Commands cmd) {
    auto commandlist =
        device.get()->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
    commandlist->open();
    for (auto&& [entity, view] : views.iter()) {
        glm::uvec2 size = view.viewport_size;
        if (size.x == 0 || size.y == 0) {
            continue;  // invalid size
        }
        // create new depth texture
        nvrhi::TextureHandle texture;
        if (auto it = depth_cache->cache.find(size); it != depth_cache->cache.end()) {
            texture = it->second;
            depth_cache->cache.erase(it);
        } else {
            nvrhi::TextureDesc desc;
            desc.width            = size.x;
            desc.height           = size.y;
            desc.format           = nvrhi::Format::D32;
            desc.dimension        = nvrhi::TextureDimension::Texture2D;
            desc.isRenderTarget   = true;
            desc.debugName        = "ViewDepth";
            desc.initialState     = nvrhi::ResourceStates::DepthWrite;
            desc.keepInitialState = true;  // keep initial state to avoid transition when used as read-only
            texture               = device.get()->createTexture(desc);
            if (!texture) {
                spdlog::error("Failed to create depth texture for view with size {}x{}", size.x, size.y);
                continue;
            }
        }
        commandlist->clearDepthStencilTexture(texture, nvrhi::TextureSubresourceSet(), true, 1.0f, false, 0);
        cmd.entity(entity).insert(view::ViewDepth{texture});
    }
    commandlist->close();
    device.get()->executeCommandList(commandlist);
}

void clear_cache(ResMut<ViewDepthCache> depth_cache) { depth_cache->cache.clear(); }

void recycle_depth(Query<Item<const ViewDepth&>> depths, ResMut<ViewDepthCache> depth_cache) {
    for (auto&& [depth] : depths.iter()) {
        if (depth.texture) {
            auto desc = depth.texture->getDesc();
            glm::uvec2 size{desc.width, desc.height};
            depth_cache->cache[size] = depth.texture;
        }
    }
}

struct ViewPluginImplUniformCache__ {
    std::deque<nvrhi::BufferHandle> cache;
};

void create_uniform_for_view(
    Commands cmd,
    Query<Item<Entity, const render::view::ExtractedView&, const render::camera::ExtractedCamera&>> views,
    ResMut<ViewPluginImplUniformCache__> uniform_cache,
    Res<nvrhi::DeviceHandle> device) {
    auto cmd_list = device.get()->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
    cmd_list->open();
    for (auto&& [entity, view, camera] : views.iter()) {
        // create a uniform buffer for the view
        nvrhi::BufferHandle view_buffer;
        if (uniform_cache->cache.empty()) {
            view_buffer = device.get()->createBuffer(nvrhi::BufferDesc()
                                                         .setByteSize(sizeof(ViewUniform))
                                                         .setIsConstantBuffer(true)
                                                         .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
                                                         .setKeepInitialState(true)
                                                         .setDebugName("Sprite View Uniform"));
        } else {
            view_buffer = uniform_cache->cache.back();
            uniform_cache->cache.pop_back();
        }

        // upload data
        ViewUniform uniform = {
            view.projection,
            glm::inverse(view.transform.matrix),
        };
        cmd_list->writeBuffer(view_buffer, &uniform, sizeof(uniform));
        // insert the uniform into the view entity
        cmd.entity(entity).insert(UniformBuffer{view_buffer});
    }
    cmd_list->close();
    device.get()->executeCommandList(cmd_list);

    // clean up unused view uniforms
    uniform_cache->cache.clear();
}

void view::ViewPlugin::build(App& app) {
    ViewUniformBindingLayout view_uniform_binding_layout(app.world_mut());
    app.world_mut().insert_resource(view_uniform_binding_layout);
    if (auto sub_app = app.get_sub_app_mut(render::Render)) {
        sub_app->get().world_mut().insert_resource(view_uniform_binding_layout);
        sub_app->get().world_mut().insert_resource(ViewDepthCache{});
        sub_app->get().world_mut().insert_resource(ViewPluginImplUniformCache__{});
        sub_app->get().add_systems(Render, into(prepare_view_target, create_view_depth)
                                               .after(window::prepare_windows)
                                               .in_set(RenderSet::ManageViews)
                                               .set_names(std::array{"prepare view targets", "create view depths"}));
        sub_app->get().add_systems(Render,
                                   into(clear_cache).after(RenderSet::ManageViews).set_name("clear view depth cache"));
        sub_app->get().add_systems(
            Render,
            into(create_uniform_for_view).in_set(render::RenderSet::PrepareResources).set_name("create view uniforms"));
        sub_app->get().add_systems(
            Render,
            into(recycle_depth).after(RenderSet::Render).before(RenderSet::Cleanup).set_name("recycle view depths"));
    }
}