#pragma once

#include <epix/assets.h>
#include <epix/image.h>
#include <epix/render.h>

namespace epix::sprite {
struct Sprite {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    bool flip_x = false;
    bool flip_y = false;
    std::optional<glm::vec2> size;  // if not set, use image size
    glm::vec2 anchor{0.5f, 0.5f};   // (0,0) is bottom-left, (1,1) is top-right
};

struct SpriteBundle : Bundle {
    Sprite sprite;
    transform::Transform transform;
    assets::Handle<image::Image> texture;

    auto unpack() { return std::make_tuple(sprite, transform, std::move(texture)); }
};

struct ExtractedSprite {
    Sprite sprite;
    transform::GlobalTransform transform;
    assets::AssetId<image::Image> texture;
};

struct ExtractedSprites {
    entt::dense_map<Entity, ExtractedSprite> sprites;
};

struct SpritePipeline {
    render::RenderPipelineId pipeline_id;
    nvrhi::BindingLayoutHandle image_layout;
    nvrhi::BindingLayoutHandle uniform_layout;

    static assets::Handle<render::Shader> vertex_shader =
        assets::AssetId<render::Shader>(uuids::uuid::from_string("1a75cef9-87bb-4325-a50f-a81f96fe414c").value());
    static assets::Handle<render::Shader> fragment_shader =
        assets::AssetId<render::Shader>(uuids::uuid::from_string("a799ba44-0bb4-4dd9-98b1-e70deea84e1f").value());

    static std::optional<SpritePipeline> from_world(World& world) {
        if (auto pipeline_server = world.get_resource<render::PipelineServer>()) {
            SpritePipeline pipeline;
            auto device = world.resource<nvrhi::DeviceHandle>();

            // a texture and a sampler
            pipeline.image_layout =
                device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                                .addItem(nvrhi::BindingLayoutItem::Sampler(1))
                                                .setVisibility(nvrhi::ShaderType::Pixel)
                                                .setBindingOffsets(nvrhi::VulkanBindingOffsets{0, 0, 0, 0}));

            // a uniform buffer and a push constant for the transform
            pipeline.uniform_layout =
                device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
                                                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                                                    1))  // storage buffer for batched model matrices
                                                .setVisibility(nvrhi::ShaderType::Vertex)
                                                .setBindingOffsets(nvrhi::VulkanBindingOffsets{0, 0, 0, 0}));

            render::RenderPipelineDesc pipeline_desc;
            pipeline_desc.addBindingLayout(pipeline.uniform_layout).addBindingLayout(pipeline.image_layout);
            pipeline_desc.setVertexShader(render::ShaderInfo{
                .shader    = vertex_shader,
                .debugName = "SpriteVertex",
            });
            pipeline_desc.setFragmentShader(render::ShaderInfo{
                .shader    = fragment_shader,
                .debugName = "SpriteFragment",
            });
            auto input_layouts = std::array{
                nvrhi::VertexAttributeDesc()
                    .setName("Position")
                    .setFormat(nvrhi::Format::RG32_FLOAT)
                    .setOffset(0)
                    .setBufferIndex(0),
                nvrhi::VertexAttributeDesc()
                    .setName("TexCoord")
                    .setFormat(nvrhi::Format::RG32_FLOAT)
                    .setOffset(0)
                    .setBufferIndex(1),
            };
            nvrhi::InputLayoutHandle =
                device->createInputLayout(input_layouts.data(), (uint32_t)input_layouts.size(), nullptr);
            pipeline_desc.setInputLayout(nvrhi::InputLayoutHandle);
            pipeline.pipeline_id = pipeline_server->queue_render_pipeline(pipeline_desc);
            return pipeline;
        } else {
            return std::nullopt;
        }
    }
};

struct ViewUniform {
    nvrhi::BufferHandle view_buffer;
};
struct ViewUniformCache {
    std::deque<ViewUniform> cache;
};

void extract_sprites(
    ResMut<ExtractedSprites> extracted_sprites,
    Extract<Query<Item<Entity, Sprite, transform::GlobalTransform, assets::Handle<image::Image>>>> sprites) {
    for (auto&& [entity, sprite, transform, texture] : sprites.iter()) {
        if (!texture) continue;
        extracted_sprites->sprites[entity] = ExtractedSprite{
            .sprite    = sprite,
            .transform = transform,
            .texture   = texture.id(),
        };
    }
}
void queue_sprites(Query<Item<Mut<render::render_phase::RenderPhase<render::core_2d::Transparent2D>>>,
                         With<render::camera::ExtractedCamera, render::view::ViewTarget>> views,
                   Res<SpritePipeline> pipeline,
                   Res<render::render_phase::DrawFunctions<render::core_2d::Transparent2D>> draw_functions,
                   Res<ExtractedSprites> extracted_sprites) {
    for (auto&& [phase] : views.iter()) {
        for (auto&& [entity, sprite] : extracted_sprites->sprites) {
            phase->add(render::core_2d::Transparent2D{
                .id          = entity,
                .pipeline_id = pipeline->pipeline_id,
                .draw_func   = render::render_phase::get_or_add_render_commands<
                      render::core_2d::Transparent2D, render::render_phase::SetItemPipeline>(*draw_functions),
            });
        }
    }
}
void create_uniform_for_view(Commands cmd,
                             Query<Item<Entity, render::view::ExtractedView, render::camera::ExtractedCamera>> views,
                             ResMut<ViewUniformCache> uniform_cache,
                             Res<nvrhi::DeviceHandle> device) {
    for (auto&& [entity, view, camera] : views.iter()) {
        if (camera.render_graph != render::camera::CameraRenderGraph(render::core_2d::Core2d)) {
            continue;
        }

        // create a uniform buffer for the view
        nvrhi::BufferHandle view_buffer;
        if (uniform_cache->cache.empty()) {
            view_buffer = device.get()->createBuffer(nvrhi::BufferDesc()
                                                         .setByteSize(sizeof(glm::mat4) * 2)
                                                         .setIsConstantBuffer(true)
                                                         .setDebugName("Sprite View Uniform"));
        } else {
            view_buffer = uniform_cache->cache.back().view_buffer;
            uniform_cache->cache.pop_back();
        }
        cmd.entity(entity).emplace(ViewUniform{.view_buffer = view_buffer});
    }

    // clean up unused view uniforms
    uniform_cache->cache.clear();
}
}  // namespace epix::sprite