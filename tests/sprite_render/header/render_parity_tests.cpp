#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/transform.hpp>
#include <ranges>
#include <string_view>

namespace assets        = epix::assets;
namespace camera        = epix::camera;
namespace core_graph    = epix::core_graph;
namespace ecs           = epix::ecs;
namespace image         = epix::image;
namespace render        = epix::render;
namespace shader        = epix::shader;
namespace sprite        = epix::sprite;
namespace sprite_render = epix::sprite_render;
namespace transform     = epix::transform;

namespace {

bool has_shader_def(const std::vector<shader::ShaderDefVal>& defs, std::string_view name) {
    return std::ranges::any_of(defs, [name](const auto& value) { return value.name == name; });
}

sprite_render::SpritePipeline make_pipeline() {
    return sprite_render::SpritePipeline{
        .view_layout     = {},
        .material_layout = {},
        .shader          = assets::Handle<shader::Shader>{assets::AssetId<shader::Shader>::invalid()},
    };
}

}  // namespace

TEST(SpritePipelineKey, MatchesBevyBitLayoutAndMsaaRoundTrip) {
    using K = sprite_render::SpritePipelineKey;
    EXPECT_EQ(K{K::HDR}.bits(), 1u << 0u);
    EXPECT_EQ(K{K::TONEMAP_IN_SHADER}.bits(), 1u << 1u);
    EXPECT_EQ(K{K::DEBAND_DITHER}.bits(), 1u << 2u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_NONE}.bits(), 0u << 26u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_REINHARD}.bits(), 1u << 26u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_REINHARD_LUMINANCE}.bits(), 2u << 26u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_ACES_FITTED}.bits(), 3u << 26u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_AGX}.bits(), 4u << 26u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM}.bits(), 5u << 26u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_TONY_MC_MAPFACE}.bits(), 6u << 26u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_BLENDER_FILMIC}.bits(), 7u << 26u);
    EXPECT_EQ(static_cast<std::uint32_t>(K::TONEMAP_METHOD_RESERVED_BITS), 0b111u << 26u);
    EXPECT_EQ(static_cast<std::uint32_t>(K::MSAA_RESERVED_BITS), 0b111u << 29u);

    for (const std::uint32_t samples : std::array{1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}) {
        const auto key = K::from_msaa_samples(samples);
        EXPECT_EQ(key.msaa_samples(), samples);
        EXPECT_EQ(key.bits(), std::countr_zero(samples) << 29u);
    }
    EXPECT_EQ(K::from_hdr(false).bits(), 0u);
    EXPECT_EQ(K::from_hdr(true).bits(), K{K::HDR}.bits());
}

TEST(SpritePipelineKey, ViewSpecializationMatchesBevyTonemappingRules) {
    using K = sprite_render::SpritePipelineKey;
    using T = core_graph::Tonemapping;
    const std::array methods{
        std::pair{T::None, K::TONEMAP_METHOD_NONE},
        std::pair{T::Reinhard, K::TONEMAP_METHOD_REINHARD},
        std::pair{T::ReinhardLuminance, K::TONEMAP_METHOD_REINHARD_LUMINANCE},
        std::pair{T::AcesFitted, K::TONEMAP_METHOD_ACES_FITTED},
        std::pair{T::AgX, K::TONEMAP_METHOD_AGX},
        std::pair{T::SomewhatBoringDisplayTransform, K::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM},
        std::pair{T::TonyMcMapface, K::TONEMAP_METHOD_TONY_MC_MAPFACE},
        std::pair{T::BlenderFilmic, K::TONEMAP_METHOD_BLENDER_FILMIC},
    };
    for (const auto& [method, method_bit] : methods) {
        const auto key = sprite_render::detail::view_pipeline_key(false, 4, method, core_graph::DebandDither::Enabled);
        EXPECT_TRUE(key.contains(K::TONEMAP_IN_SHADER));
        EXPECT_TRUE(key.contains(K::DEBAND_DITHER));
        EXPECT_EQ(key.intersection(K::TONEMAP_METHOD_RESERVED_BITS), K{method_bit});
        EXPECT_EQ(key.msaa_samples(), 4u);
    }

    const auto no_tonemapping =
        sprite_render::detail::view_pipeline_key(false, 1, std::nullopt, core_graph::DebandDither::Enabled);
    EXPECT_FALSE(no_tonemapping.contains(K::TONEMAP_IN_SHADER));
    EXPECT_TRUE(no_tonemapping.contains(K::DEBAND_DITHER));

    const auto hdr =
        sprite_render::detail::view_pipeline_key(true, 8, T::TonyMcMapface, core_graph::DebandDither::Enabled);
    EXPECT_TRUE(hdr.contains(K::HDR));
    EXPECT_FALSE(hdr.contains(K::TONEMAP_IN_SHADER));
    EXPECT_FALSE(hdr.contains(K::DEBAND_DITHER));
    EXPECT_EQ(hdr.intersection(K::TONEMAP_METHOD_RESERVED_BITS), K{});
    EXPECT_EQ(hdr.msaa_samples(), 8u);
}

TEST(SpritePipeline, SpecializationMatchesBevyDescriptor) {
    using K             = sprite_render::SpritePipelineKey;
    const auto pipeline = make_pipeline();
    const auto key =
        K::from_msaa_samples(4) | K::TONEMAP_IN_SHADER | K::TONEMAP_METHOD_TONY_MC_MAPFACE | K::DEBAND_DITHER;
    const auto descriptor = pipeline.specialize(key);

    EXPECT_EQ(descriptor.layouts.size(), 2u);
    ASSERT_TRUE(descriptor.vertex.entry_point.has_value());
    EXPECT_EQ(*descriptor.vertex.entry_point, "vs_main");
    ASSERT_EQ(descriptor.vertex.buffers.size(), 1u);
    const auto& buffer = descriptor.vertex.buffers.front();
    EXPECT_EQ(buffer.array_stride, 80u);
    EXPECT_EQ(buffer.step_mode, wgpu::VertexStepMode::eInstance);
    ASSERT_EQ(buffer.attributes.size(), 5u);
    for (std::size_t index = 0; index < buffer.attributes.size(); ++index) {
        EXPECT_EQ(buffer.attributes[index].format, wgpu::VertexFormat::eFloat32x4);
        EXPECT_EQ(buffer.attributes[index].offset, index * 16u);
        EXPECT_EQ(buffer.attributes[index].shader_location, index);
    }

    ASSERT_TRUE(descriptor.fragment.has_value());
    ASSERT_TRUE(descriptor.fragment->entry_point.has_value());
    EXPECT_EQ(*descriptor.fragment->entry_point, "fs_main");
    EXPECT_TRUE(has_shader_def(descriptor.vertex.shader_defs, "TONEMAP_IN_SHADER"));
    EXPECT_TRUE(has_shader_def(descriptor.vertex.shader_defs, "TONEMAP_METHOD_TONY_MC_MAPFACE"));
    EXPECT_TRUE(has_shader_def(descriptor.vertex.shader_defs, "DEBAND_DITHER"));
    EXPECT_EQ(descriptor.vertex.shader_defs, descriptor.fragment->shader_defs);

    ASSERT_EQ(descriptor.fragment->targets.size(), 1u);
    const auto& target = descriptor.fragment->targets.front();
    EXPECT_EQ(target.format, image::bevy_default<wgpu::TextureFormat>());
    ASSERT_TRUE(target.blend.has_value());
    EXPECT_EQ(target.blend->color.operation, wgpu::BlendOperation::eAdd);
    EXPECT_EQ(target.blend->color.srcFactor, wgpu::BlendFactor::eSrcAlpha);
    EXPECT_EQ(target.blend->color.dstFactor, wgpu::BlendFactor::eOneMinusSrcAlpha);
    EXPECT_EQ(target.blend->alpha.srcFactor, wgpu::BlendFactor::eOne);
    EXPECT_EQ(target.blend->alpha.dstFactor, wgpu::BlendFactor::eOneMinusSrcAlpha);
    EXPECT_EQ(target.writeMask, wgpu::ColorWriteMask::eAll);

    ASSERT_TRUE(descriptor.depth_stencil.has_value());
    EXPECT_EQ(descriptor.depth_stencil->format, core_graph::core_2d::CORE_2D_DEPTH_FORMAT);
    EXPECT_EQ(descriptor.depth_stencil->depthWriteEnabled, wgpu::OptionalBool::eFalse);
    EXPECT_EQ(descriptor.depth_stencil->depthCompare, wgpu::CompareFunction::eGreaterEqual);
    EXPECT_EQ(descriptor.multisample.count, 4u);
    EXPECT_EQ(descriptor.multisample.mask, ~0u);
    EXPECT_FALSE(descriptor.multisample.alphaToCoverageEnabled);

    const auto hdr = pipeline.specialize(K::from_hdr(true));
    ASSERT_TRUE(hdr.fragment.has_value());
    EXPECT_EQ(hdr.fragment->targets.front().format, render::view::ViewTarget::TEXTURE_FORMAT_HDR);
    EXPECT_TRUE(hdr.fragment->shader_defs.empty());

    const auto dither_without_tonemapping = pipeline.specialize(K{K::DEBAND_DITHER});
    ASSERT_TRUE(dither_without_tonemapping.fragment.has_value());
    EXPECT_TRUE(dither_without_tonemapping.fragment->shader_defs.empty());
}

TEST(SpriteInstance, MatchesBevyEightyByteTransposedAffineAbi) {
    static_assert(sizeof(sprite_render::SpriteInstance) == 80u);
    static_assert(offsetof(sprite_render::SpriteInstance, i_model_transpose_col0) == 0u);
    static_assert(offsetof(sprite_render::SpriteInstance, i_model_transpose_col1) == 16u);
    static_assert(offsetof(sprite_render::SpriteInstance, i_model_transpose_col2) == 32u);
    static_assert(offsetof(sprite_render::SpriteInstance, i_color) == 48u);
    static_assert(offsetof(sprite_render::SpriteInstance, i_uv_offset_scale) == 64u);

    glm::mat4 matrix{0.0f};
    float value = 1.0f;
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) matrix[column][row] = value++;
    }
    const glm::vec4 color{21.0f, 22.0f, 23.0f, 24.0f};
    const glm::vec4 uv{31.0f, 32.0f, 33.0f, 34.0f};
    const auto instance = sprite_render::detail::make_instance(matrix, color, uv);
    EXPECT_EQ(instance.i_model_transpose_col0, (glm::vec4{1.0f, 5.0f, 9.0f, 13.0f}));
    EXPECT_EQ(instance.i_model_transpose_col1, (glm::vec4{2.0f, 6.0f, 10.0f, 14.0f}));
    EXPECT_EQ(instance.i_model_transpose_col2, (glm::vec4{3.0f, 7.0f, 11.0f, 15.0f}));
    EXPECT_EQ(instance.i_color, color);
    EXPECT_EQ(instance.i_uv_offset_scale, uv);
}

TEST(SpriteBatching, KeepsRangesIndependentAcrossTwoViewsAndUsesBevyIndices) {
    assets::Assets<image::Image> image_assets;
    const auto image_id = image_assets.emplace(image::Image::create2d(32, 16, wgpu::TextureFormat::eRGBA8Unorm)).id();
    const auto render_entity = ecs::Entity::from_index(20);
    const auto main_entity   = ecs::Entity::from_index(10);
    const sprite_render::ExtractedSprites extracted{
        .sprites = {sprite_render::ExtractedSprite{
            .main_entity     = main_entity,
            .render_entity   = render_entity,
            .transform       = transform::GlobalTransform{},
            .color           = glm::vec4(1.0f),
            .image_handle_id = image_id,
            .kind =
                sprite_render::ExtractedSpriteSingle{
                    .anchor      = glm::vec2(0.0f),
                    .custom_size = glm::vec2(8.0f, 4.0f),
                },
        }},
    };
    const sprite_render::ExtractedSlices slices;
    sprite_render::SpriteMeta meta;
    sprite_render::SpriteBatches batches;
    render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D> phases;
    const render::view::RetainedViewEntity view_a{render::sync_world::MainEntity{ecs::Entity::from_index(1)}};
    const render::view::RetainedViewEntity view_b{render::sync_world::MainEntity{ecs::Entity::from_index(2)}};
    phases.insert_or_clear(view_a);
    phases.insert_or_clear(view_b);

    const auto item = core_graph::core_2d::Transparent2D{
        .representative_entity = {render_entity, render::sync_world::MainEntity{main_entity}},
        .depth                 = 0.0f,
        .pipeline_id           = render::CachedPipelineId{0},
        .draw_func             = render::phase::DrawFunctionId{0},
        .batch_range_value     = {0, 0},
        .extra_index_value     = {},
        .extracted_index       = 0,
        .indexed_value         = true,
    };
    phases.at(view_a).add(item);
    phases.at(view_b).add(item);

    sprite_render::detail::prepare_sprite_batches(
        meta, extracted, slices, phases, batches,
        [image_id](assets::AssetId<image::Image> requested) -> std::optional<glm::vec2> {
            EXPECT_EQ(requested, image_id);
            return glm::vec2{32.0f, 16.0f};
        });

    ASSERT_EQ(meta.sprite_instance_buffer.len(), 2u);
    ASSERT_EQ(batches.values.size(), 2u);
    const auto batch_a = batches.values.find(sprite_render::SpriteBatchKey{view_a, render_entity});
    const auto batch_b = batches.values.find(sprite_render::SpriteBatchKey{view_b, render_entity});
    ASSERT_NE(batch_a, batches.values.end());
    ASSERT_NE(batch_b, batches.values.end());
    EXPECT_EQ(batch_a->second.image_handle_id, image_id);
    EXPECT_EQ(batch_b->second.image_handle_id, image_id);
    EXPECT_EQ(batch_a->second.range.second - batch_a->second.range.first, 1u);
    EXPECT_EQ(batch_b->second.range.second - batch_b->second.range.first, 1u);
    EXPECT_NE(batch_a->second.range, batch_b->second.range);
    EXPECT_EQ(phases.at(view_a).items.front().batch_range(), (std::pair<std::uint32_t, std::uint32_t>{0, 1}));
    EXPECT_EQ(phases.at(view_b).items.front().batch_range(), (std::pair<std::uint32_t, std::uint32_t>{0, 1}));

    EXPECT_TRUE(sprite_render::detail::prepare_sprite_indices(meta));
    const std::array expected{2u, 0u, 1u, 1u, 3u, 2u};
    ASSERT_EQ(meta.sprite_index_buffer.len(), expected.size());
    EXPECT_TRUE(std::ranges::equal(meta.sprite_index_buffer.values(), expected));
    EXPECT_FALSE(sprite_render::detail::prepare_sprite_indices(meta));
}

TEST(SpriteScaling, PreservesBevyFiniteAndIeeeZeroDimensionArithmetic) {
    glm::vec2 quad_size{100.0f, 100.0f};
    glm::vec2 translation{0.0f};
    glm::vec4 uv{0.0f, 1.0f, 1.0f, -1.0f};
    sprite_render::detail::apply_scaling(sprite::SpriteScalingMode::FitCenter, {200.0f, 100.0f}, quad_size, translation,
                                         uv);
    EXPECT_EQ(quad_size, (glm::vec2{100.0f, 50.0f}));
    EXPECT_EQ(translation, glm::vec2(0.0f));
    EXPECT_EQ(uv, (glm::vec4{0.0f, 1.0f, 1.0f, -1.0f}));

    quad_size   = {0.0f, 0.0f};
    translation = {0.0f, 0.0f};
    uv          = {0.0f, 1.0f, 1.0f, -1.0f};
    sprite_render::detail::apply_scaling(sprite::SpriteScalingMode::FillCenter, {0.0f, 0.0f}, quad_size, translation,
                                         uv);
    EXPECT_TRUE(std::isnan(uv.x));
    EXPECT_TRUE(std::isnan(uv.z));
}

TEST(SpriteExtraction, RebuildsResourcesAndPreservesMainRenderIdentity) {
    ecs::World main_world(7);
    ecs::World render_world(7);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});
    render_world.insert_resource(sprite_render::ExtractedSprites{});
    render_world.insert_resource(sprite_render::ExtractedSlices{});
    main_world.insert_resource(assets::Assets<image::TextureAtlasLayout>{});

    const auto render_entity = render_world.spawn().id();
    camera::ViewVisibility visible;
    visible.set_visible();
    sprite::Sprite visible_sprite = sprite::Sprite::from_color({0.25f, 0.5f, 0.75f, 1.0f}, {32.0f, 16.0f});
    visible_sprite.flip_x         = true;
    const transform::GlobalTransform global{
        .matrix = glm::mat4{1.0f},
    };
    const auto main_entity = main_world
                                 .spawn(render::sync_world::RenderEntity{render_entity}, visible, visible_sprite,
                                        global, sprite::Anchor{{0.25f, -0.5f}})
                                 .id();
    // A second visible sprite, extracted exactly like the first: Epix used to skip
    // entities carrying an Epix-only `CustomRendered` marker, which Bevy has no
    // counterpart for (SRD-195).
    camera::ViewVisibility second_visible;
    second_visible.set_visible();
    const auto second_render_entity = render_world.spawn().id();
    const auto second_entity        = main_world
                                          .spawn(render::sync_world::RenderEntity{second_render_entity}, second_visible,
                                                 sprite::Sprite{}, global, sprite::Anchor{})
                                          .id();

    auto all_render_entities              = render_world.query<ecs::Entity>();
    const auto render_entity_count_before = std::ranges::distance(all_render_entities.iter(render_world));

    auto system = ecs::make_system_unique(sprite_render::extract_sprites);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());

    const auto& extracted = render_world.resource<sprite_render::ExtractedSprites>().sprites;
    ASSERT_EQ(extracted.size(), 2u) << "every visible sprite is extracted, marked or not";
    const auto found = std::ranges::find_if(extracted, [main_entity](const auto& sprite) {
        return sprite.main_entity == main_entity;
    });
    ASSERT_NE(found, extracted.end());
    EXPECT_EQ(found->render_entity, render_entity);
    EXPECT_EQ(found->color, visible_sprite.color);
    EXPECT_TRUE(found->flip_x);
    ASSERT_TRUE(std::holds_alternative<sprite_render::ExtractedSpriteSingle>(found->kind));
    const auto& single = std::get<sprite_render::ExtractedSpriteSingle>(found->kind);
    EXPECT_EQ(single.anchor, (glm::vec2{0.25f, -0.5f}));
    EXPECT_EQ(single.custom_size, visible_sprite.custom_size);
    EXPECT_NE(std::ranges::find_if(extracted,
                                   [second_entity](const auto& sprite) { return sprite.main_entity == second_entity; }),
              extracted.end());
    EXPECT_EQ(std::ranges::distance(all_render_entities.iter(render_world)), render_entity_count_before)
        << "extraction must not spawn temporary render entities";

    main_world.entity_mut(main_entity).get_mut<camera::ViewVisibility>().value().get_mut() =
        camera::ViewVisibility::hidden();
    ASSERT_TRUE(system->run({}, render_world).has_value());
    const auto& remaining = render_world.resource<sprite_render::ExtractedSprites>().sprites;
    ASSERT_EQ(remaining.size(), 1u) << "hidden sprites drop out";
    EXPECT_EQ(remaining.front().main_entity, second_entity);
    EXPECT_TRUE(render_world.resource<sprite_render::ExtractedSlices>().slices.empty());
}

TEST(SpriteExtraction, CopiesEveryImageEventAndClearsEachFrame) {
    ecs::World main_world(7);
    ecs::World render_world(7);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});
    render_world.insert_resource(sprite_render::SpriteAssetEvents{});
    main_world.insert_resource(ecs::Events<assets::AssetEvent<image::Image>>{});

    assets::Assets<image::Image> images;
    const auto id = images.emplace(image::Image::create2d(1, 1, wgpu::TextureFormat::eRGBA8Unorm)).id();
    auto& source  = main_world.resource_mut<ecs::Events<assets::AssetEvent<image::Image>>>();
    source.push(assets::AssetEvent<image::Image>::added(id));
    source.push(assets::AssetEvent<image::Image>::modified(id));
    source.push(assets::AssetEvent<image::Image>::removed(id));
    source.push(assets::AssetEvent<image::Image>::unused(id));
    source.push(assets::AssetEvent<image::Image>::loaded_with_dependencies(id));

    auto system = ecs::make_system_unique(sprite_render::extract_sprite_events);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());
    const auto& events = render_world.resource<sprite_render::SpriteAssetEvents>().images;
    ASSERT_EQ(events.size(), 5u);
    EXPECT_EQ(events[0].type, assets::AssetEvent<image::Image>::Type::Added);
    EXPECT_EQ(events[1].type, assets::AssetEvent<image::Image>::Type::Modified);
    EXPECT_EQ(events[2].type, assets::AssetEvent<image::Image>::Type::Removed);
    EXPECT_EQ(events[3].type, assets::AssetEvent<image::Image>::Type::Unused);
    EXPECT_EQ(events[4].type, assets::AssetEvent<image::Image>::Type::LoadedWithDependencies);

    ASSERT_TRUE(system->run({}, render_world).has_value());
    EXPECT_TRUE(render_world.resource<sprite_render::SpriteAssetEvents>().images.empty());
}

namespace {
/** @brief The six accessors `PhaseItem` asks for, and nothing else
 *  (`render_phase.hpp`). */
struct SkipTestPhaseItem {
    ecs::Entity render_entity = ecs::Entity::from_index(1);
    render::sync_world::MainEntity main{ecs::Entity::from_index(1)};
    render::phase::DrawFunctionId draw_id{};
    std::pair<std::uint32_t, std::uint32_t> batch_range_value{0, 1};
    render::phase::PhaseItemExtraIndex extra_index_value{};

    ecs::Entity entity() const noexcept { return render_entity; }
    render::sync_world::MainEntity main_entity() const noexcept { return main; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_id; }
    const std::pair<std::uint32_t, std::uint32_t>& batch_range() const noexcept { return batch_range_value; }
    std::pair<std::uint32_t, std::uint32_t>& batch_range() noexcept { return batch_range_value; }
    const render::phase::PhaseItemExtraIndex& extra_index() const noexcept { return extra_index_value; }
    render::phase::PhaseItemExtraIndex& extra_index() noexcept { return extra_index_value; }
};
}  // namespace

/** @brief Bevy returns `RenderCommandResult::Skip` from both of
 *  `SetMaterial2dBindGroup`'s missing arms (`material.rs:534-539`). Reporting
 *  success there would continue the draw function with no material bind group set
 *  (or the previous item's), which is what the skip exists to prevent - and the
 *  phase treats a skip as a normal outcome, not an error. */
TEST(Material2dRenderCommand, SkipsWhenTheInstanceOrPreparedMaterialIsMissing) {
    using Material  = sprite_render::ColorMaterial;
    using Prepared  = sprite_render::PreparedMaterial2d<Material>;
    using Instances = sprite_render::RenderMaterial2dInstances<Material>;
    using Assets    = render::RenderAssets<Prepared>;
    using Command =
        render::phase::RenderCommand<sprite_render::SetMaterial2dBindGroup<Material, 2>, SkipTestPhaseItem>;

    ecs::World world(1);
    world.insert_resource(Assets{});
    world.insert_resource(Instances{});

    std::optional<render::phase::RenderCommandError> observed;
    // The framework builds the two `Res<>` parameters; the command's skip arms run
    // before the encoder is touched, so a null one is never dereferenced.
    auto probe = ecs::make_system_unique([&observed](ecs::Res<Assets> assets, ecs::Res<Instances> instances) {
        const SkipTestPhaseItem item{};
        auto result = Command{}.render(item, std::tuple<>{}, std::optional<ecs::Item<>>{},
                                       std::make_tuple(assets, instances), wgpu::RenderPassEncoder{});
        if (!result) observed = result.error();
    });
    probe->initialize(world);
    ASSERT_TRUE(probe->run({}, world).has_value());

    ASSERT_TRUE(observed.has_value()) << "a missing material instance must skip the draw";
    EXPECT_EQ(observed->type, render::phase::RenderCommandError::Type::Skip);

    // The same for an instance whose prepared material is missing: a real asset id
    // that no `PreparedMaterial2d` was registered for.
    assets::Assets<Material> materials;
    const auto unprepared =
        materials.emplace(sprite_render::ColorMaterial::from_color(glm::vec4{1.0f})).id();
    world.resource_mut<Instances>().instances.insert_or_assign(
        render::sync_world::MainEntity{ecs::Entity::from_index(1)}, unprepared);
    observed.reset();
    ASSERT_TRUE(probe->run({}, world).has_value());
    ASSERT_TRUE(observed.has_value()) << "an unprepared material must skip the draw";
    EXPECT_EQ(observed->type, render::phase::RenderCommandError::Type::Skip);
}

/** @brief The default `ColorMaterial` is reachable at a well-known handle (Bevy
 *  inserts it at `Handle::<ColorMaterial>::default()`,
 *  `bevy_sprite_render/src/mesh2d/color_material.rs:21-32`).
 *
 *  Bevy's plugin inserts its magenta default *at the default-asset handle*, which
 *  is what makes that handle resolve. Epix names the handle instead, because its
 *  `Handle` deletes the default constructor - so the property to check is that the
 *  named handle resolves the material, which is what this does through the same
 *  `Assets::insert` the plugin calls. A material stored at a freshly allocated
 *  index would fail this: it would exist with nothing able to name it. */
TEST(Material2dPlugin, DefaultColorMaterialResolvesAtItsWellKnownHandle) {
    using Material = sprite_render::ColorMaterial;

    epix::assets::Assets<Material> materials;
    EXPECT_FALSE(materials.get(sprite_render::DEFAULT_COLOR_MATERIAL_HANDLE).has_value())
        << "nothing is there before the plugin inserts it";

    const auto inserted = materials.insert(sprite_render::DEFAULT_COLOR_MATERIAL_HANDLE.id(),
                                           Material{.color = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f)});
    ASSERT_TRUE(inserted.has_value()) << "the plugin inserts rather than re-allocating";

    const auto resolved = materials.get(sprite_render::DEFAULT_COLOR_MATERIAL_HANDLE);
    ASSERT_TRUE(resolved.has_value()) << "and the well-known handle resolves it";
    const glm::vec4 expected_magenta{1.0f, 0.0f, 1.0f, 1.0f};
    EXPECT_EQ(resolved->get().color, expected_magenta) << "the default is Bevy's magenta";
}