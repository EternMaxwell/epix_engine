module;

#include <spdlog/spdlog.h>

module epix.render;

import :view;

using namespace epix::core;
using namespace epix::render;
using namespace epix::render::view;
using namespace epix::render::camera;

void view::prepare_view_target(Query<Item<Entity, const camera::ExtractedCamera&, const ExtractedView&>> views,
                               Commands cmd,
                               Res<window::ExtractedWindows> extracted_windows) {
    // Prepare the view target for each extracted camera view
    for (auto&& [entity, camera, view] : views.iter()) {
        std::optional<wgpu::TextureView> target_texture = std::visit(
            utils::visitor{
                [&](const wgpu::Texture& tex) -> std::optional<wgpu::TextureView> { return tex.createView(); },
                [&](const camera::WindowRef& win_ref) -> std::optional<wgpu::TextureView> {
                    auto&& id = win_ref.window_entity;
                    if (auto it = extracted_windows->windows.find(id);
                        it != extracted_windows->windows.end() && it->second.swapchain_texture_view) {
                        return it->second.swapchain_texture_view;
                    } else {
                        return std::nullopt;
                    }
                }},
            camera.render_target);
        std::optional<wgpu::TextureFormat> target_format = std::visit(
            utils::visitor{
                [&](const wgpu::Texture& tex) -> std::optional<wgpu::TextureFormat> { return tex.getFormat(); },
                [&](const camera::WindowRef& win_ref) -> std::optional<wgpu::TextureFormat> {
                    auto&& id = win_ref.window_entity;
                    if (auto it = extracted_windows->windows.find(id); it != extracted_windows->windows.end()) {
                        return it->second.swapchain_texture_format;
                    }
                    return std::nullopt;
                }},
            camera.render_target);
        if (!target_texture.has_value() || !target_texture.value()) {
            // invalid target texture, handle error;
            // no need to remove the entity, it will just be missing ViewTarget.
            continue;
        }
        if (!target_format.has_value()) {
            continue;
        }
        cmd.entity(entity).insert(view::ViewTarget{target_texture.value(), *target_format});
    }
}

void view::create_view_depth(Query<Item<Entity, const ExtractedView&>> views,
                             Res<wgpu::Device> device,
                             Res<wgpu::Queue> queue,
                             ResMut<ViewDepthCache> depth_cache,
                             Commands cmd) {
    wgpu::CommandEncoder encoder = device->createCommandEncoder();
    for (auto&& [entity, view] : views.iter()) {
        glm::uvec2 size = view.viewport_size;
        if (size.x == 0 || size.y == 0) {
            continue;  // invalid size
        }
        // create new depth texture
        wgpu::Texture texture;
        if (auto it = depth_cache->cache.find(size); it != depth_cache->cache.end()) {
            texture = std::move(it->second);
            depth_cache->cache.erase(it);
        } else {
            wgpu::TextureDescriptor desc;
            desc.setSize({size.x, size.y, 1})
                .setFormat(wgpu::TextureFormat::eDepth32Float)
                .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc)
                .setDimension(wgpu::TextureDimension::e2D)
                .setSampleCount(1)
                .setMipLevelCount(1)
                .setLabel("ViewDepth");
            texture = device.get().createTexture(desc);
            if (!texture) {
                spdlog::error("Failed to create depth texture for view with size {}x{}", size.x, size.y);
                continue;
            }
        }
        wgpu::RenderPassDepthStencilAttachment depth_attachment;
        auto view = texture.createView();
        depth_attachment.setView(view)
            .setDepthLoadOp(wgpu::LoadOp::eClear)
            .setDepthStoreOp(wgpu::StoreOp::eStore)
            .setDepthClearValue(1.0f);
        wgpu::RenderPassEncoder pass =
            encoder.beginRenderPass(wgpu::RenderPassDescriptor().setDepthStencilAttachment(depth_attachment));
        pass.end();
        cmd.entity(entity).insert(view::ViewDepth{std::move(texture), std::move(view)});
    }
    queue->submit(encoder.finish());
}

void clear_cache(ResMut<ViewDepthCache> depth_cache) { depth_cache->cache.clear(); }

void recycle_depth(Query<const ViewDepth&> depths, ResMut<ViewDepthCache> depth_cache) {
    for (auto&& depth : depths.iter()) {
        if (depth.texture) {
            glm::uvec2 size{depth.texture.getWidth(), depth.texture.getHeight()};
            depth_cache->cache[size] = depth.texture;
        }
    }
}

std::size_t getOffsetInUniform(std::size_t index, std::size_t size, std::size_t alignment) {
    std::size_t stride = (size + alignment - 1) / alignment * alignment;
    return index * stride;
}

void create_uniform_for_view(Commands cmd,
                             Query<Item<Entity, const view::ExtractedView&, const camera::ExtractedCamera&>> views,
                             Res<wgpu::Device> device,
                             Res<wgpu::Limits> limits,
                             ResMut<UniformBuffer> uniform_buffer,
                             Res<ViewUniformBindingLayout> uniform_layout,
                             Res<wgpu::Queue> queue) {
    // we need a vector for ViewUniform but with given stride
    auto view_vec            = std::make_unique<std::vector<uint8_t>>();
    std::size_t uniform_size = sizeof(ViewUniform);
    std::size_t alignment    = limits->minUniformBufferOffsetAlignment;
    std::size_t stride       = ((uniform_size + alignment - 1) / alignment) * alignment;
    view_vec->resize(views.iter().max_remaining() * stride);
    wgpu::Buffer buffer;
    if (uniform_buffer->buffer && uniform_buffer->buffer.getSize() >= view_vec->size()) {
        buffer = uniform_buffer->buffer;
    } else {
        wgpu::BufferDescriptor desc;
        desc.setSize(view_vec->size())
            .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst)
            .setLabel("ViewUniformBuffer");
        buffer = device.get().createBuffer(desc);
        if (!buffer) {
            spdlog::error("Failed to create uniform buffer for views");
            return;
        }
        uniform_buffer->buffer = buffer;
    }
    std::size_t index = 0;
    for (auto&& [entity, view, camera] : views.iter()) {
        // upload data
        ViewUniform uniform = {
            view.projection,
            glm::inverse(view.transform.matrix),
        };
        std::memcpy(view_vec->data() + getOffsetInUniform(index, uniform_size, alignment), &uniform, sizeof(uniform));
        wgpu::BindGroup bind_group =
            device.get().createBindGroup(wgpu::BindGroupDescriptor()
                                             .setLayout(uniform_layout->layout)
                                             .setEntries(std::array{
                                                 wgpu::BindGroupEntry()
                                                     .setBinding(0)
                                                     .setBuffer(buffer)
                                                     .setOffset(getOffsetInUniform(index, uniform_size, alignment))
                                                     .setSize(sizeof(ViewUniform)),
                                             })
                                             .setLabel("ViewUniformBindGroup"));
        cmd.entity(entity).insert(ViewBindGroup{std::move(bind_group)});
        index++;
    }
    queue->writeBuffer(buffer, 0, view_vec->data(), view_vec->size());
}

void view::ViewPlugin::build(App& app) {
    spdlog::debug("[render.view] Building ViewPlugin.");
    ViewUniformBindingLayout view_uniform_binding_layout(app.world_mut());
    app.world_mut().insert_resource(view_uniform_binding_layout);
    if (auto sub_app = app.get_sub_app_mut(render::Render)) {
        sub_app->get().world_mut().insert_resource(view_uniform_binding_layout);
        sub_app->get().world_mut().insert_resource(ViewDepthCache{});
        sub_app->get().world_mut().insert_resource(UniformBuffer{});
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

void camera::extract_cameras(
    Commands cmd,
    Res<ClearColor> global_clear_color,
    Extract<Query<
        Item<const Camera&, const CameraRenderGraph&, const transform::GlobalTransform&, const view::VisibleEntities&>>>
        cameras,
    Extract<Query<Entity, With<::epix::window::PrimaryWindow, ::epix::window::Window>>> primary_window) {
    // extract camera entities to render world, this will spawn an related
    // entity with ExtractedCamera, ExtractedView and other components.

    auto primary = primary_window.single();

    for (auto&& [camera, graph, gtransform, visible_entities] : cameras.iter()) {
        if (!camera.active) continue;
        auto target_size = camera.get_target_size();
        if (target_size.x == 0 || target_size.y == 0) continue;
        auto normalized_target = camera.render_target.normalize(primary);
        if (!normalized_target.has_value()) continue;
        auto viewport_size   = camera.get_viewport_size();
        auto viewport_origin = camera.get_viewport_origin();

        auto commands = cmd.spawn();
        // single call to insert to reduce overhead
        commands.insert(
            ExtractedCamera{
                .render_target = *normalized_target,
                .viewport_size = viewport_size,
                .target_size   = target_size,
                .viewport      = camera.viewport,
                .render_graph  = graph,
                .order         = camera.order,
                .clear_color   = [&]() -> std::optional<ClearColor> {
                    if (camera.clear_color.type == ClearColorConfig::Type::Global ||
                        camera.clear_color.type == ClearColorConfig::Type::Default) {
                        return *global_clear_color;
                    } else if (camera.clear_color.type == ClearColorConfig::Type::Custom) {
                        return ClearColor(camera.clear_color.clear_color);
                    } else {
                        return std::nullopt;
                    }
                }(),
            },
            view::ExtractedView{
                .projection      = camera.computed.projection,
                .transform       = gtransform,
                .viewport_size   = viewport_size,
                .viewport_origin = viewport_origin,
            },
            visible_entities);
    }
}

struct CameraDriverNode : graph::Node {
    void run(graph::GraphContext& graph, graph::RenderContext& render_ctx, const World& world) override;
};
void CameraDriverNode::run(graph::GraphContext& graph, graph::RenderContext& render_ctx, const World& world) {
    auto cameras = world.try_query<Item<Entity, const ExtractedCamera&, const view::ViewTarget&>>();
    if (!cameras) return;
    auto&& windows = world.resource<window::ExtractedWindows>();
    auto encoder   = render_ctx.command_encoder();
    for (auto&& [entity, camera, target] : cameras->query(world).iter()) {
        if (camera.clear_color) {
            auto render_pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor().setColorAttachments(std::array{
                wgpu::RenderPassColorAttachment()
                    .setView(target.texture_view)
                    .setLoadOp(wgpu::LoadOp::eClear)
                    .setStoreOp(wgpu::StoreOp::eStore)
                    .setDepthSlice(~0u)
                    .setClearValue(wgpu::Color(camera.clear_color->r, camera.clear_color->g, camera.clear_color->b,
                                               camera.clear_color->a)),
            }));
            render_pass.end();
        }
        if (!graph.run_sub_graph(camera.render_graph, {}, entity)) {
            spdlog::warn("Failed to run camera render graph for entity {:#x}, with render graph label {}", entity.index,
                         camera.render_graph.type_index().short_name());
        }
    }
}

void CameraPlugin::build(App& app) {
    app.configure_sets(sets(CameraUpdateSystems::CameraUpdateSystem));
    app.add_plugins(CameraProjectionPlugin<Projection>{}, CameraProjectionPlugin<OrthographicProjection>{},
                    CameraProjectionPlugin<PerspectiveProjection>{}, ExtractResourcePlugin<ClearColor>{});
    app.world_mut().insert_resource(ClearColor{0.05f, 0.05f, 0.05f, 1.0f});
    if (auto sub_app = app.get_sub_app_mut(Render)) {
        sub_app->get().world_mut().insert_resource(ClearColor{0.05f, 0.05f, 0.05f, 1.0f});
        sub_app->get().add_systems(ExtractSchedule, into(extract_cameras).set_name("extract cameras"));
        if (auto render_graph = sub_app->get().get_resource_mut<graph::RenderGraph>()) {
            render_graph->get().add_node(CameraDriverNodeLabel, CameraDriverNode{});
        }
    }
}
