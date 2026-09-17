#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;
using namespace epix::sprite_render;

namespace {
constexpr std::string_view kSpriteShaderPath = "sprite/sprite.slang";

// Slang representation of pinned Bevy 0.18 sprite.wgsl and
// sprite_view_bindings.wgsl. The quad corners come from SV_VertexID, while
// all per-sprite data arrives in the one 80-byte instance-rate stream.
constexpr std::string_view kSpriteShader = R"slang(
import epix.view;
#ifdef TONEMAP_IN_SHADER
import epix.tonemapping;
#endif

[[vk::binding(0, 0)]] ConstantBuffer<epix::view::View> view;
[[vk::binding(1, 0)]] Texture3D<float4> dt_lut_texture;
[[vk::binding(2, 0)]] SamplerState dt_lut_sampler;

[[vk::binding(0, 1)]] Texture2D<float4> sprite_texture;
[[vk::binding(1, 1)]] SamplerState sprite_sampler;

struct VertexInput {
    uint index : SV_VertexID;
    [[vk::location(0)]] float4 i_model_transpose_col0;
    [[vk::location(1)]] float4 i_model_transpose_col1;
    [[vk::location(2)]] float4 i_model_transpose_col2;
    [[vk::location(3)]] float4 i_color;
    [[vk::location(4)]] float4 i_uv_offset_scale;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
    [[vk::location(1)]] nointerpolation float4 color;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    float3 vertex_position = float3(float(input.index & 1u), float((input.index & 2u) >> 1u), 0.0);
    float3 world_position = float3(
        dot(input.i_model_transpose_col0.xyz, vertex_position) + input.i_model_transpose_col0.w,
        dot(input.i_model_transpose_col1.xyz, vertex_position) + input.i_model_transpose_col1.w,
        dot(input.i_model_transpose_col2.xyz, vertex_position) + input.i_model_transpose_col2.w);

    VertexOutput output;
    output.position = mul(view.clip_from_world, float4(world_position, 1.0));
    output.uv = vertex_position.xy * input.i_uv_offset_scale.zw + input.i_uv_offset_scale.xy;
    output.color = input.i_color;
    return output;
}

[shader("fragment")]
float4 fs_main(VertexOutput input) : SV_Target {
    float4 color = input.color * sprite_texture.Sample(sprite_sampler, input.uv);
#ifdef TONEMAP_IN_SHADER
    epix::view::ColorGrading grading = view.color_grading;
    color = epix::tonemapping::tone_mapping(color, grading, dt_lut_texture, dt_lut_sampler);
#endif
    return color;
}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return {reinterpret_cast<const std::byte*>(source.data()), source.size()};
}

struct SpriteShaderHandle {
    assets::Handle<shader::Shader> value;
};

std::optional<SpriteShaderHandle> load_sprite_shader(World& world) {
    auto registry = world.get_resource_mut<assets::EmbeddedAssetRegistry>();
    auto server   = world.get_resource<assets::AssetServer>();
    if (!registry || !server) return std::nullopt;
    registry->get().insert_asset_static(kSpriteShaderPath, shader_bytes(kSpriteShader));
    return SpriteShaderHandle{
        server->get().load<shader::Shader>("embedded://sprite/sprite.slang"),
    };
}

SpritePipelineKey tonemapping_key(core_graph::Tonemapping method) {
    using Key = SpritePipelineKey;
    switch (method) {
        case core_graph::Tonemapping::None:
            return Key{Key::TONEMAP_METHOD_NONE};
        case core_graph::Tonemapping::Reinhard:
            return Key{Key::TONEMAP_METHOD_REINHARD};
        case core_graph::Tonemapping::ReinhardLuminance:
            return Key{Key::TONEMAP_METHOD_REINHARD_LUMINANCE};
        case core_graph::Tonemapping::AcesFitted:
            return Key{Key::TONEMAP_METHOD_ACES_FITTED};
        case core_graph::Tonemapping::AgX:
            return Key{Key::TONEMAP_METHOD_AGX};
        case core_graph::Tonemapping::SomewhatBoringDisplayTransform:
            return Key{Key::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM};
        case core_graph::Tonemapping::TonyMcMapface:
            return Key{Key::TONEMAP_METHOD_TONY_MC_MAPFACE};
        case core_graph::Tonemapping::BlenderFilmic:
            return Key{Key::TONEMAP_METHOD_BLENDER_FILMIC};
    }
    return {};
}

glm::vec4 sprite_uv(image::Rect rect, glm::vec2 image_size, bool flip_x, bool flip_y) {
    const auto rect_size = rect.size();
    glm::vec4 uv{rect.min.x / image_size.x, rect.max.y / image_size.y, rect_size.x / image_size.x,
                 -rect_size.y / image_size.y};
    if (flip_x) {
        uv.x += uv.z;
        uv.z *= -1.0f;
    }
    if (flip_y) {
        uv.y += uv.w;
        uv.w *= -1.0f;
    }
    return uv;
}

SpriteInstance make_single_instance(const ExtractedSprite& extracted,
                                    const ExtractedSpriteSingle& single,
                                    glm::vec2 image_size) {
    auto quad_size    = image_size;
    auto texture_size = image_size;
    glm::vec4 uv{0.0f, 1.0f, 1.0f, -1.0f};
    if (single.rect) {
        quad_size    = single.rect->size();
        texture_size = quad_size;
        uv           = sprite_uv(*single.rect, image_size, false, false);
    }
    if (extracted.flip_x) {
        uv.x += uv.z;
        uv.z *= -1.0f;
    }
    if (extracted.flip_y) {
        uv.y += uv.w;
        uv.w *= -1.0f;
    }
    quad_size = single.custom_size.value_or(quad_size);
    glm::vec2 quad_translation{0.0f};
    if (single.scaling_mode) {
        sprite_render::detail::apply_scaling(*single.scaling_mode, texture_size, quad_size, quad_translation, uv);
    }
    const glm::vec3 translation{(quad_size + quad_translation) * (-single.anchor - glm::vec2(0.5f)), 0.0f};
    const glm::mat4 local =
        glm::translate(glm::mat4(1.0f), translation) * glm::scale(glm::mat4(1.0f), glm::vec3(quad_size, 1.0f));
    return sprite_render::detail::make_instance(extracted.transform.matrix * local, extracted.color, uv);
}

SpriteInstance make_slice_instance(const ExtractedSprite& extracted,
                                   const ExtractedSlice& slice,
                                   glm::vec2 image_size) {
    const glm::vec3 translation{slice.size * -glm::vec2(0.5f) + slice.offset, 0.0f};
    const glm::mat4 local =
        glm::translate(glm::mat4(1.0f), translation) * glm::scale(glm::mat4(1.0f), glm::vec3(slice.size, 1.0f));
    return sprite_render::detail::make_instance(extracted.transform.matrix * local, extracted.color,
                                                sprite_uv(slice.rect, image_size, extracted.flip_x, extracted.flip_y));
}

std::optional<ComputedTextureSlices> compute_sprite_slices(
    const sprite::Sprite& value,
    const assets::Assets<image::Image>& images,
    const assets::Assets<image::TextureAtlasLayout>& atlas_layouts) {
    glm::vec2 image_size;
    image::Rect texture_rect;
    if (value.texture_atlas) {
        const auto layout = atlas_layouts.get(value.texture_atlas->layout.id());
        if (!layout || value.texture_atlas->index >= layout->get().textures.size()) return std::nullopt;
        image_size   = glm::vec2(layout->get().size);
        texture_rect = layout->get().textures[value.texture_atlas->index].as_rect();
    } else {
        const auto source = images.get(value.image.id());
        if (!source) return std::nullopt;
        image_size   = glm::vec2(static_cast<float>(source->get().width()), static_cast<float>(source->get().height()));
        texture_rect = value.rect.value_or(image::Rect{glm::vec2(0.0f), image_size});
    }

    if (const auto* sliced = std::get_if<sprite::SpriteImageMode::Sliced>(&value.image_mode)) {
        return ComputedTextureSlices{sliced->slicer.compute_slices(texture_rect, value.custom_size)};
    }
    if (const auto* tiled = std::get_if<sprite::SpriteImageMode::Tiled>(&value.image_mode)) {
        const sprite::TextureSlice whole{
            .texture_rect = texture_rect,
            .draw_size    = value.custom_size.value_or(image_size),
            .offset       = glm::vec2(0.0f),
        };
        return ComputedTextureSlices{whole.tiled(tiled->stretch_value, {tiled->tile_x, tiled->tile_y})};
    }
    return std::nullopt;
}
}  // namespace

void sprite_render::detail::apply_scaling(sprite::SpriteScalingMode mode,
                                          glm::vec2 texture_size,
                                          glm::vec2& quad_size,
                                          glm::vec2& quad_translation,
                                          glm::vec4& uv_offset_scale) {
    const auto quad_ratio     = quad_size.x / quad_size.y;
    const auto texture_ratio  = texture_size.x / texture_size.y;
    const auto tex_quad_scale = texture_ratio / quad_ratio;
    const auto quad_tex_scale = quad_ratio / texture_ratio;

    switch (mode) {
        case sprite::SpriteScalingMode::FillCenter:
            if (quad_ratio > texture_ratio) {
                uv_offset_scale.y += (uv_offset_scale.w - uv_offset_scale.w * tex_quad_scale) * 0.5f;
                uv_offset_scale.w *= tex_quad_scale;
            } else {
                uv_offset_scale.x += (uv_offset_scale.z - uv_offset_scale.z * quad_tex_scale) * 0.5f;
                uv_offset_scale.z *= quad_tex_scale;
            }
            break;
        case sprite::SpriteScalingMode::FillStart:
            if (quad_ratio > texture_ratio) {
                uv_offset_scale.y += uv_offset_scale.w - uv_offset_scale.w * tex_quad_scale;
                uv_offset_scale.w *= tex_quad_scale;
            } else {
                uv_offset_scale.z *= quad_tex_scale;
            }
            break;
        case sprite::SpriteScalingMode::FillEnd:
            if (quad_ratio > texture_ratio) {
                uv_offset_scale.w *= tex_quad_scale;
            } else {
                uv_offset_scale.x += uv_offset_scale.z - uv_offset_scale.z * quad_tex_scale;
                uv_offset_scale.z *= quad_tex_scale;
            }
            break;
        case sprite::SpriteScalingMode::FitCenter:
            if (texture_ratio > quad_ratio) quad_size.y *= quad_tex_scale;
            else quad_size.x *= tex_quad_scale;
            break;
        case sprite::SpriteScalingMode::FitStart: {
            const auto next = texture_ratio > quad_ratio ? quad_size * glm::vec2(1.0f, quad_tex_scale)
                                                         : quad_size * glm::vec2(tex_quad_scale, 1.0f);
            const auto offset = quad_size - next;
            quad_translation  = texture_ratio > quad_ratio ? glm::vec2(0.0f, -offset.y)
                                                           : glm::vec2(offset.x, 0.0f);
            quad_size = next;
            break;
        }
        case sprite::SpriteScalingMode::FitEnd: {
            const auto next = texture_ratio > quad_ratio ? quad_size * glm::vec2(1.0f, quad_tex_scale)
                                                         : quad_size * glm::vec2(tex_quad_scale, 1.0f);
            const auto offset = quad_size - next;
            quad_translation  = texture_ratio > quad_ratio ? glm::vec2(0.0f, offset.y)
                                                           : glm::vec2(-offset.x, 0.0f);
            quad_size = next;
            break;
        }
    }
}

SpriteInstance sprite_render::detail::make_instance(const glm::mat4& transform, glm::vec4 color, glm::vec4 uv) {
    return SpriteInstance{
        .i_model_transpose_col0 = {transform[0][0], transform[1][0], transform[2][0], transform[3][0]},
        .i_model_transpose_col1 = {transform[0][1], transform[1][1], transform[2][1], transform[3][1]},
        .i_model_transpose_col2 = {transform[0][2], transform[1][2], transform[2][2], transform[3][2]},
        .i_color                = color,
        .i_uv_offset_scale      = uv,
    };
}

SpritePipelineKey sprite_render::detail::view_pipeline_key(bool hdr,
                                                           std::uint32_t msaa_samples,
                                                           std::optional<core_graph::Tonemapping> tonemapping,
                                                           std::optional<core_graph::DebandDither> dither) {
    auto key = SpritePipelineKey::from_hdr(hdr) | SpritePipelineKey::from_msaa_samples(msaa_samples);
    if (!hdr) {
        if (tonemapping) {
            key |= SpritePipelineKey::TONEMAP_IN_SHADER;
            key |= tonemapping_key(*tonemapping);
        }
        if (dither == core_graph::DebandDither::Enabled) key |= SpritePipelineKey::DEBAND_DITHER;
    }
    return key;
}

render::RenderPipelineDescriptor SpritePipeline::specialize(Key key) const {
    std::vector<shader::ShaderDefVal> shader_defs;
    if (key.contains(Key::TONEMAP_IN_SHADER)) {
        shader_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_IN_SHADER"));
        const auto method     = key.intersection(Key::TONEMAP_METHOD_RESERVED_BITS);
        const auto add_method = [&](std::string_view name) {
            shader_defs.push_back(shader::ShaderDefVal::from_bool(std::string(name)));
        };
        if (method == Key{Key::TONEMAP_METHOD_NONE})
            add_method("TONEMAP_METHOD_NONE");
        else if (method == Key{Key::TONEMAP_METHOD_REINHARD})
            add_method("TONEMAP_METHOD_REINHARD");
        else if (method == Key{Key::TONEMAP_METHOD_REINHARD_LUMINANCE})
            add_method("TONEMAP_METHOD_REINHARD_LUMINANCE");
        else if (method == Key{Key::TONEMAP_METHOD_ACES_FITTED})
            add_method("TONEMAP_METHOD_ACES_FITTED");
        else if (method == Key{Key::TONEMAP_METHOD_AGX})
            add_method("TONEMAP_METHOD_AGX");
        else if (method == Key{Key::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM})
            add_method("TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM");
        else if (method == Key{Key::TONEMAP_METHOD_TONY_MC_MAPFACE})
            add_method("TONEMAP_METHOD_TONY_MC_MAPFACE");
        else if (method == Key{Key::TONEMAP_METHOD_BLENDER_FILMIC})
            add_method("TONEMAP_METHOD_BLENDER_FILMIC");
        if (key.contains(Key::DEBAND_DITHER)) add_method("DEBAND_DITHER");
    }

    const std::array attributes{
        mesh::VertexAttribute().set_format(wgpu::VertexFormat::eFloat32x4).set_offset(0).set_shader_location(0),
        mesh::VertexAttribute().set_format(wgpu::VertexFormat::eFloat32x4).set_offset(16).set_shader_location(1),
        mesh::VertexAttribute().set_format(wgpu::VertexFormat::eFloat32x4).set_offset(32).set_shader_location(2),
        mesh::VertexAttribute().set_format(wgpu::VertexFormat::eFloat32x4).set_offset(48).set_shader_location(3),
        mesh::VertexAttribute().set_format(wgpu::VertexFormat::eFloat32x4).set_offset(64).set_shader_location(4),
    };
    render::VertexState vertex{
        .shader      = shader,
        .shader_defs = shader_defs,
        .entry_point = std::string("vs_main"),
    };
    vertex.set_buffer(0, mesh::VertexBufferLayout()
                             .set_array_stride(sizeof(SpriteInstance))
                             .set_step_mode(wgpu::VertexStepMode::eInstance)
                             .set_attributes(attributes));

    const auto color_blend = wgpu::BlendComponent()
                                 .setOperation(wgpu::BlendOperation::eAdd)
                                 .setSrcFactor(wgpu::BlendFactor::eSrcAlpha)
                                 .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha);
    const auto alpha_blend = wgpu::BlendComponent()
                                 .setOperation(wgpu::BlendOperation::eAdd)
                                 .setSrcFactor(wgpu::BlendFactor::eOne)
                                 .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha);
    render::FragmentState fragment{
        .shader      = shader,
        .shader_defs = std::move(shader_defs),
        .entry_point = std::string("fs_main"),
    };
    fragment.add_target(wgpu::ColorTargetState()
                            .setFormat(key.contains(Key::HDR) ? render::view::ViewTarget::TEXTURE_FORMAT_HDR
                                                              : image::bevy_default<wgpu::TextureFormat>())
                            .setBlend(wgpu::BlendState().setColor(color_blend).setAlpha(alpha_blend))
                            .setWriteMask(wgpu::ColorWriteMask::eAll));

    return render::RenderPipelineDescriptor{
        .label         = "sprite_pipeline",
        .layouts       = {view_layout, material_layout},
        .vertex        = std::move(vertex),
        .primitive     = wgpu::PrimitiveState()
                             .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                             .setFrontFace(wgpu::FrontFace::eCCW)
                             .setCullMode(wgpu::CullMode::eNone),
        .depth_stencil = wgpu::DepthStencilState()
                             .setFormat(core_graph::core_2d::CORE_2D_DEPTH_FORMAT)
                             .setDepthWriteEnabled(wgpu::OptionalBool::eFalse)
                             .setDepthCompare(wgpu::CompareFunction::eGreaterEqual),
        .multisample =
            wgpu::MultisampleState().setCount(key.msaa_samples()).setMask(~0u).setAlphaToCoverageEnabled(false),
        .fragment = std::move(fragment),
    };
}

void sprite_render::extract_sprite_events(ResMut<SpriteAssetEvents> events,
                                          Extract<EventReader<assets::AssetEvent<image::Image>>> image_events) {
    events->images.clear();
    for (const auto& event : image_events.read()) events->images.push_back(event);
}

void sprite_render::extract_sprites(ResMut<ExtractedSprites> extracted_sprites,
                                    ResMut<ExtractedSlices> extracted_slices,
                                    Extract<Query<Item<Entity,
                                                       render::sync_world::RenderEntity,
                                                       const camera::ViewVisibility&,
                                                       const sprite::Sprite&,
                                                       const transform::GlobalTransform&,
                                                       const sprite::Anchor&,
                                                       Opt<const ComputedTextureSlices&>>,
                                                  Filter<Without<render::CustomRendered>>>> sprites,
                                    Extract<Res<assets::Assets<image::TextureAtlasLayout>>> atlases) {
    extracted_sprites->sprites.clear();
    extracted_slices->slices.clear();
    for (auto&& [main_entity, render_entity, visibility, value, transform, anchor, slices] : sprites.iter()) {
        if (!visibility.get()) continue;

        ExtractedSpriteKind kind;
        if (slices) {
            const auto begin = extracted_slices->slices.size();
            for (const auto& slice : slices->get().extract_slices(value, anchor.as_vec())) {
                extracted_slices->slices.push_back(slice);
            }
            kind = ExtractedSpriteSlices{begin, extracted_slices->slices.size()};
        } else {
            std::optional<image::Rect> rect;
            if (value.texture_atlas) {
                rect = value.texture_atlas->texture_rect(*atlases).transform(
                    [](const image::URect& value) { return value.as_rect(); });
            }
            if (value.rect) {
                rect = rect ? image::Rect{rect->min + value.rect->min, rect->min + value.rect->max} : *value.rect;
            }
            kind = ExtractedSpriteSingle{
                .anchor       = anchor.as_vec(),
                .rect         = rect,
                .scaling_mode = value.image_mode.scale(),
                .custom_size  = value.custom_size,
            };
        }

        extracted_sprites->sprites.push_back(ExtractedSprite{
            .main_entity     = main_entity,
            .render_entity   = render_entity.id(),
            .transform       = transform,
            .color           = value.color,
            .image_handle_id = value.image.id(),
            .flip_x          = value.flip_x,
            .flip_y          = value.flip_y,
            .kind            = std::move(kind),
        });
    }
}

void sprite_render::queue_sprites_2d(
    Local<std::unordered_set<std::uint32_t>> view_entities,
    Query<Item<const render::view::RenderVisibleEntities&,
               const render::view::ExtractedView&,
               const render::view::Msaa&,
               Opt<const core_graph::Tonemapping&>,
               Opt<const core_graph::DebandDither&>>> views,
    Res<ExtractedSprites> extracted_sprites,
    Res<render::phase::DrawFunctions<core_graph::core_2d::Transparent2D>> draw_functions,
    Res<SpritePipeline> sprite_pipeline,
    ResMut<render::SpecializedRenderPipelines<SpritePipeline>> pipelines,
    Res<render::PipelineServer> pipeline_server,
    ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>> phases) {
    const auto draw_function = draw_functions->read()->template id<DrawSprite>();
    for (auto&& [visible, view, msaa, tonemapping, dither] : views.iter()) {
        const auto phase = phases->find(view.retained_view_entity);
        if (phase == phases->end()) continue;

        const auto key =
            detail::view_pipeline_key(view.hdr, render::view::samples(msaa),
                                      tonemapping.transform([](const core_graph::Tonemapping& value) { return value; }),
                                      dither.transform([](const core_graph::DebandDither& value) { return value; }));
        const auto pipeline = pipelines->specialize(*pipeline_server, *sprite_pipeline, key);

        view_entities->clear();
        for (const auto& [_, main_entity] : visible.template iter<sprite::Sprite>()) {
            view_entities->insert(main_entity.id().index);
        }
        phase->second.items.reserve(phase->second.items.size() + extracted_sprites->sprites.size());
        for (std::size_t index = 0; index < extracted_sprites->sprites.size(); ++index) {
            const auto& extracted = extracted_sprites->sprites[index];
            if (!view_entities->contains(extracted.main_entity.index)) continue;
            phase->second.add(core_graph::core_2d::Transparent2D{
                .representative_entity = {extracted.render_entity,
                                          render::sync_world::MainEntity{extracted.main_entity}},
                .depth                 = extracted.transform.matrix[3][2],
                .pipeline_id           = pipeline,
                .draw_func             = draw_function,
                .batch_range_value     = {0, 0},
                .extra_index_value     = {},
                .extracted_index       = index,
                .indexed_value         = true,
            });
        }
    }
}

void sprite_render::prepare_sprite_view_bind_groups(
    Commands commands,
    Res<wgpu::Device> device,
    Res<SpritePipeline> sprite_pipeline,
    Res<render::PipelineServer> pipeline_server,
    Res<render::view::ViewUniforms> view_uniforms,
    Query<Item<Entity, const core_graph::Tonemapping&>, With<render::view::ExtractedView>> views,
    Res<core_graph::TonemappingLuts> tonemapping_luts,
    Res<render::RenderAssets<image::Image>> images,
    Res<render::texture::FallbackImage> fallback_image) {
    const auto binding = view_uniforms->uniforms.binding();
    if (!binding) return;
    for (auto&& [entity, tonemapping] : views.iter()) {
        const auto [lut_view, lut_sampler] =
            core_graph::get_lut_bindings(*images, *tonemapping_luts, tonemapping, *fallback_image);
        const auto entries = render::render_resource::BindGroupEntries<>::sequential(
            render::render_resource::BindGroupEntries<>::buffer_binding(binding->get(), 0,
                                                                        sizeof(render::view::ViewUniform)),
            render::render_resource::BindGroupEntries<>::texture_binding(lut_view),
            render::render_resource::BindGroupEntries<>::sampler_binding(lut_sampler));
        commands.entity(entity).insert(SpriteViewBindGroup{
            device->createBindGroup(wgpu::BindGroupDescriptor()
                                        .setLabel("mesh2d_view_bind_group")
                                        .setLayout(pipeline_server->get_bind_group_layout(sprite_pipeline->view_layout))
                                        .setEntries(entries.entries())),
        });
    }
}

void sprite_render::prepare_sprite_image_bind_groups(
    Res<wgpu::Device> device,
    Res<wgpu::Queue> queue,
    ResMut<SpriteMeta> sprite_meta,
    Res<SpritePipeline> sprite_pipeline,
    ResMut<ImageBindGroups> image_bind_groups,
    Res<render::RenderAssets<image::Image>> gpu_images,
    Res<ExtractedSprites> extracted_sprites,
    Res<ExtractedSlices> extracted_slices,
    ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>> phases,
    Res<SpriteAssetEvents> events,
    ResMut<SpriteBatches> batches,
    Res<render::PipelineServer> pipeline_server) {
    for (const auto& event : events->images) {
        using Type = assets::AssetEvent<image::Image>::Type;
        if (event.type == Type::Unused || event.type == Type::Modified || event.type == Type::Removed) {
            image_bind_groups->values.erase(event.id);
        }
    }

    detail::prepare_sprite_batches(
        *sprite_meta, *extracted_sprites, *extracted_slices, *phases, *batches,
        [&](assets::AssetId<image::Image> image_id) -> std::optional<glm::vec2> {
            const auto gpu_image = gpu_images->try_get(image_id);
            if (!gpu_image) return std::nullopt;
            if (!image_bind_groups->values.contains(image_id)) {
                image_bind_groups->values.emplace(
                    image_id, device->createBindGroup(
                                  wgpu::BindGroupDescriptor()
                                      .setLabel("sprite_material_bind_group")
                                      .setLayout(pipeline_server->get_bind_group_layout(sprite_pipeline->material_layout))
                                      .setEntries(std::array{
                                          wgpu::BindGroupEntry().setBinding(0).setTextureView(gpu_image->texture_view),
                                          wgpu::BindGroupEntry().setBinding(1).setSampler(gpu_image->sampler),
                                      })));
            }
            return glm::vec2(gpu_image->size_2d());
        });

    sprite_meta->sprite_instance_buffer.write_buffer(*device, *queue);
    if (detail::prepare_sprite_indices(*sprite_meta)) {
        sprite_meta->sprite_index_buffer.write_buffer(*device, *queue);
    }
}

void sprite_render::detail::prepare_sprite_batches(
    SpriteMeta& sprite_meta,
    const ExtractedSprites& extracted_sprites,
    const ExtractedSlices& extracted_slices,
    render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>& phases,
    SpriteBatches& batches,
    const PrepareSpriteImage& prepare_image) {
    batches.values.clear();
    sprite_meta.sprite_instance_buffer.clear();
    std::uint32_t instance_index = 0;

    for (auto& [retained_view, phase] : phases) {
        std::optional<assets::AssetId<image::Image>> batch_image;
        std::size_t batch_item_index = 0;
        glm::vec2 batch_image_size{0.0f};
        SpriteBatch* current_batch = nullptr;

        for (std::size_t item_index = 0; item_index < phase.items.size(); ++item_index) {
            auto& item                       = phase.items[item_index];
            const ExtractedSprite* extracted = item.extracted_index < extracted_sprites.sprites.size()
                                                   ? &extracted_sprites.sprites[item.extracted_index]
                                                   : nullptr;
            if (!extracted || extracted->render_entity != item.entity()) {
                batch_image.reset();
                current_batch = nullptr;
                continue;
            }

            if (!batch_image || *batch_image != extracted->image_handle_id) {
                const auto image_size = prepare_image(extracted->image_handle_id);
                if (!image_size) continue;
                batch_image_size = *image_size;
                batch_image      = extracted->image_handle_id;
                batch_item_index = item_index;
                auto [it, _]     = batches.values.insert_or_assign(
                    SpriteBatchKey{retained_view, item.entity()},
                    SpriteBatch{.image_handle_id = *batch_image, .range = {instance_index, instance_index}});
                current_batch = &it->second;
            }

            if (const auto* single = std::get_if<ExtractedSpriteSingle>(&extracted->kind)) {
                sprite_meta.sprite_instance_buffer.push(make_single_instance(*extracted, *single, batch_image_size));
                ++current_batch->range.second;
                ++instance_index;
            } else {
                const auto& range = std::get<ExtractedSpriteSlices>(extracted->kind);
                for (auto index = range.begin; index < range.end; ++index) {
                    sprite_meta.sprite_instance_buffer.push(
                        make_slice_instance(*extracted, extracted_slices.slices[index], batch_image_size));
                    ++current_batch->range.second;
                    ++instance_index;
                }
            }
            ++phase.items[batch_item_index].batch_range().second;
        }
    }
}

bool sprite_render::detail::prepare_sprite_indices(SpriteMeta& sprite_meta) {
    if (sprite_meta.sprite_index_buffer.len() == 6) return false;
    sprite_meta.sprite_index_buffer.clear();
    for (const auto index : std::array<std::uint32_t, 6>{2, 0, 1, 1, 3, 2}) {
        sprite_meta.sprite_index_buffer.push(index);
    }
    return true;
}

void sprite_render::compute_slices_on_asset_event(
    Commands commands,
    EventReader<assets::AssetEvent<image::Image>> events,
    Res<assets::Assets<image::Image>> images,
    Res<assets::Assets<image::TextureAtlasLayout>> atlas_layouts,
    Query<Item<Entity, const sprite::Sprite&>> sprites) {
    std::unordered_set<assets::AssetId<image::Image>> changed;
    for (const auto& event : events.read()) {
        if (event.type == assets::AssetEvent<image::Image>::Type::Added ||
            event.type == assets::AssetEvent<image::Image>::Type::Modified) {
            changed.insert(event.id);
        }
    }
    if (changed.empty()) return;
    for (auto&& [entity, value] : sprites.iter()) {
        if (!value.image_mode.uses_slices() || !changed.contains(value.image.id())) continue;
        if (auto slices = compute_sprite_slices(value, *images, *atlas_layouts)) {
            commands.entity(entity).insert(std::move(*slices));
        }
    }
}

void sprite_render::compute_slices_on_sprite_change(
    Commands commands,
    Res<assets::Assets<image::Image>> images,
    Res<assets::Assets<image::TextureAtlasLayout>> atlas_layouts,
    Query<Item<Entity, const sprite::Sprite&>, Filter<Modified<sprite::Sprite>>> changed_sprites) {
    for (auto&& [entity, value] : changed_sprites.iter()) {
        if (!value.image_mode.uses_slices()) continue;
        if (auto slices = compute_sprite_slices(value, *images, *atlas_layouts)) {
            commands.entity(entity).insert(std::move(*slices));
        }
    }
}

void SpriteRenderPlugin::attach(App& app) {
    spdlog::debug("[sprite] Attaching SpriteRenderPlugin.");
    app.configure_sets(sets(SpriteSystems::ExtractSprites, SpriteSystems::ComputeSlices));
    if (!app.get_plugin<image::TextureAtlasPlugin>()) app.add_plugins(image::TextureAtlasPlugin{});
    app.add_plugins(Mesh2dRenderPlugin{}, render::SyncComponentPlugin<sprite::Sprite>{});
    app.add_systems(app::PostUpdate, into(sprite_render::compute_slices_on_asset_event)
                                         .in_set(SpriteSystems::ComputeSlices)
                                         .before(assets::AssetSystems::WriteEvents)
                                         .set_name("compute slices on image event"));
    app.add_systems(app::PostUpdate,
                    into(sprite_render::compute_slices_on_sprite_change)
                        .in_set(SpriteSystems::ComputeSlices)
                        .set_name("compute slices on sprite change"));
    if (!app.world().get_resource<SpriteShaderHandle>()) {
        if (auto handle = load_sprite_shader(app.world_mut())) app.world_mut().insert_resource(std::move(*handle));
    }
}

void SpriteRenderPlugin::ready(App& app) {
    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) {
        spdlog::error("[sprite] SpriteRenderPlugin requires render::RenderPlugin.");
        return;
    }
    auto& render_subapp = render_app->get();
    render_subapp.configure_sets(sets(SpriteSystems::ExtractSprites, SpriteSystems::ComputeSlices));
    auto& world = render_subapp.world_mut();
    world.init_resource<ImageBindGroups>();
    world.init_resource<render::SpecializedRenderPipelines<SpritePipeline>>();
    world.init_resource<SpriteMeta>();
    world.init_resource<ExtractedSprites>();
    world.init_resource<ExtractedSlices>();
    world.init_resource<SpriteAssetEvents>();
    world.init_resource<SpriteBatches>();
    render::phase::app_add_render_commands<core_graph::core_2d::Transparent2D, render::phase::SetItemPipeline,
                                           SetSpriteViewBindGroup<0>::Command, SetSpriteTextureBindGroup<1>::Command,
                                           DrawSpriteBatch>(render_subapp);

    render_subapp.add_systems(
        render::RenderStartup,
        into([](Commands commands, Res<render::PipelineServer> pipeline_server, Res<assets::AssetServer> asset_server) {
            using render::BindGroupLayoutDescriptor;
            using render::render_resource::BindGroupLayoutEntries;
            using render::render_resource::binding_types::sampler;
            using render::render_resource::binding_types::texture_2d;
            using render::render_resource::binding_types::uniform_buffer;
            auto lut                = core_graph::get_lut_bind_group_layout_entries();
            const auto view_entries = BindGroupLayoutEntries<>::sequential(
                wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment,
                uniform_buffer(true, sizeof(render::view::ViewUniform)),
                lut[0].visibility(wgpu::ShaderStage::eFragment), lut[1].visibility(wgpu::ShaderStage::eFragment));
            const auto material_entries = BindGroupLayoutEntries<>::sequential(
                wgpu::ShaderStage::eFragment, texture_2d(wgpu::TextureSampleType::eFloat),
                sampler(wgpu::SamplerBindingType::eFiltering));
            // The layouts are descriptors, as Bevy's are; the pipeline cache
            // interns them, and the bind groups fetch the interned layout.
            commands.insert_resource(SpritePipeline{
                .view_layout =
                    BindGroupLayoutDescriptor().set_label("sprite_view_layout").set_entries(view_entries.entries()),
                .material_layout = BindGroupLayoutDescriptor()
                                       .set_label("sprite_material_layout")
                                       .set_entries(material_entries.entries()),
                .shader = asset_server->load<shader::Shader>("embedded://sprite/sprite.slang"),
            });
        }).set_name("init sprite pipeline"));

    render_subapp
        .add_systems(
            render::ExtractSchedule,
            into(sprite_render::extract_sprites).in_set(SpriteSystems::ExtractSprites).set_name("extract sprites"))
        .add_systems(render::ExtractSchedule,
                     into(sprite_render::extract_sprite_events).set_name("extract sprite image events"))
        .add_systems(
            render::Render,
            into(sprite_render::queue_sprites_2d).in_set(render::RenderSystems::Queue).set_name("queue sprites"))
        .add_systems(
            render::Render,
            into(sprite_render::prepare_sprite_image_bind_groups, sprite_render::prepare_sprite_view_bind_groups)
                .in_set(render::RenderSystems::PrepareBindGroups)
                .set_names(std::array{"prepare sprite image bind groups", "prepare sprite view bind groups"}))
        .add_systems(render::Render, into(render::phase::sort_binned_render_phase<core_graph::core_2d::Opaque2D>,
                                          render::phase::sort_binned_render_phase<core_graph::core_2d::AlphaMask2D>)
                                         .in_set(render::RenderSystems::PhaseSort)
                                         .set_names(std::array{"sort opaque2d bins", "sort alpha-mask2d bins"}));
}
