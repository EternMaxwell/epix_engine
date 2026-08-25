#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <epix/ecs.hpp>
#include <epix/render.hpp>

#include <array>

using namespace epix::ecs;
using namespace epix::render;

TEST(VisibilityPlugin, StandaloneRegistersVisibilityRequirements) {
    auto app = epix::app::App::create();
    ::epix::camera::VisibilityPlugin{}.attach(app);
    const Entity entity = app.world_mut().spawn(::epix::camera::Visibility{}).id();
    EXPECT_TRUE(app.world().entity(entity).contains<::epix::camera::InheritedVisibility>());
    EXPECT_TRUE(app.world().entity(entity).contains<::epix::camera::ViewVisibility>());
}

// Mirrors Bevy's ViewRangefinder3d unit test (bevy_render/render_phase/rangefinder.rs).
TEST(ViewRangefinder3d, Distance) {
    const glm::mat4 view_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    const auto rangefinder      = phase::ViewRangefinder3d::from_world_from_view(view_matrix);
    EXPECT_FLOAT_EQ(rangefinder.distance(glm::vec3(0.0f, 0.0f, 0.0f)), 1.0f);
    EXPECT_FLOAT_EQ(rangefinder.distance(glm::vec3(0.0f, 0.0f, 1.0f)), 2.0f);
}

// Bevy RenderLayers uses an unbounded positive bit mask. Epix retains its
// intentional inverted-mask helpers on the same dynamically growing storage.
TEST(RenderLayers, Intersects) {
    auto layer0 = ::epix::camera::RenderLayers::layer(0);
    auto layer1 = ::epix::camera::RenderLayers::layer(1);
    auto none   = ::epix::camera::RenderLayers::none();
    EXPECT_TRUE(layer0.intersects(layer0));
    EXPECT_FALSE(layer0.intersects(layer1));
    EXPECT_FALSE(none.intersects(layer0));
    EXPECT_FALSE(layer0.intersects(none));
    EXPECT_TRUE(layer0.contains(0));
    EXPECT_FALSE(layer0.contains(1));

    // Epix's complement helpers are intentional extensions and must remain
    // dynamically extensible too.
    const auto all_except_130 = ::epix::camera::RenderLayers::all_except(std::array<std::size_t, 1>{130});
    EXPECT_TRUE(::epix::camera::RenderLayers::all().contains(10'000));
    EXPECT_FALSE(all_except_130.contains(130));
    EXPECT_TRUE(all_except_130.contains(131));

    const auto dynamic = ::epix::camera::RenderLayers::from_layers(std::array<std::size_t, 3>{0, 64, 130});
    EXPECT_TRUE(dynamic.contains(130));
    EXPECT_TRUE(dynamic.intersects(::epix::camera::RenderLayers::layer(130)));
    EXPECT_EQ(dynamic.iter(), (std::vector<std::size_t>{0, 64, 130}));
    EXPECT_EQ(dynamic.without(130).without(64).without(0), none);
    EXPECT_EQ(dynamic & ::epix::camera::RenderLayers::layer(64), ::epix::camera::RenderLayers::layer(64));
    EXPECT_EQ(dynamic | layer1, ::epix::camera::RenderLayers::from_layers(std::array<std::size_t, 4>{0, 1, 64, 130}));
}

TEST(Msaa, FromSamples) {
    EXPECT_EQ(::epix::render::view::samples(::epix::render::view::Msaa::Off), 1u);
    EXPECT_EQ(::epix::render::view::samples(::epix::render::view::Msaa::Sample4), 4u);
    EXPECT_EQ(::epix::render::view::msaa_from_samples(8), ::epix::render::view::Msaa::Sample8);
    EXPECT_THROW(::epix::render::view::msaa_from_samples(3), std::runtime_error);
}

TEST(AlphaMode, Factories) {
    EXPECT_EQ(AlphaMode::opaque().type, AlphaMode::Type::Opaque);
    EXPECT_EQ(AlphaMode::blend().type, AlphaMode::Type::Blend);
    EXPECT_EQ(AlphaMode::add().type, AlphaMode::Type::Add);
    EXPECT_EQ(AlphaMode::multiply().type, AlphaMode::Type::Multiply);
    EXPECT_EQ(AlphaMode::mask(0.25f).mask_threshold, 0.25f);
    EXPECT_EQ(AlphaMode::premultiplied(), AlphaMode::premultiplied());
}

// TextureCache eviction matches Bevy: entries unused for >= 3 updates are removed.
TEST(TextureCache, EvictsAfterThreeFrames) {
    render_resource::TextureCache cache;
    // manually seed one entry (device creation is not needed for eviction)
    render_resource::TextureCacheKey key;
    key.width           = 4;
    key.height          = 4;
    cache.textures[key] = {render_resource::detail::CachedTextureMeta{}};
    // frame 1..3: unused entry ages and is released
    cache.update();  // frames_since_last_use = 1, taken = false
    EXPECT_EQ(cache.textures.at(key).size(), 1u);
    cache.update();  // = 2
    EXPECT_EQ(cache.textures.at(key).size(), 1u);
    cache.update();  // = 3 -> evicted (>= 3)
    EXPECT_EQ(cache.textures.count(key), 0u);
    EXPECT_TRUE(cache.is_empty());
}

TEST(DynamicUniformBuffer, AlignmentAndPush) {
    render_resource::DynamicUniformBuffer<view::ViewUniform> buf;
    buf.dynamic_offset_alignment = 256;
    // Bevy 0.18 ViewUniform is 768 bytes (view.wgsl std140 layout).
    EXPECT_EQ(buf.element_stride(), 768u);  // align_up(768, 256)
    view::ViewUniform u;
    std::size_t idx = buf.push(u);
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(buf.len(), 1u);
    EXPECT_EQ(buf.values.size(), 768u);
    buf.clear();
    EXPECT_TRUE(buf.is_empty());
}

TEST(SortedCamera, SortKey) {
    camera::SortedCamera a;
    a.order = 2;
    camera::SortedCamera b;
    b.order = 1;
    EXPECT_TRUE(b.sort_key() < a.sort_key());
    // same order, different target types: texture (key 1) sorts before window (key 2+)
    camera::SortedCamera tex;
    tex.order  = 1;
    tex.target = ::epix::camera::NormalizedRenderTarget{::epix::camera::ImageRenderTarget{wgpu::Texture{}}};
    camera::SortedCamera win;
    win.order  = 1;
    win.target = ::epix::camera::NormalizedRenderTarget{::epix::camera::WindowRef{false, epix::ecs::Entity{.uid = 1}}};
    EXPECT_TRUE(tex.sort_key() < win.sort_key());
}

TEST(NormalizedRenderTargetExt, ResolvesManualAndNoColorTargets) {
    const ::epix::camera::ManualTextureViewHandle handle{7};
    texture::ManualTextureViews manual_views;
    manual_views.views.emplace(handle, texture::ManualTextureView{
                                           .size = glm::uvec2(320, 180),
                                           .view_format = wgpu::TextureFormat::eRGBA8UnormSrgb,
                                       });
    window::ExtractedWindows windows;
    const ::epix::camera::NormalizedRenderTarget manual{handle};
    EXPECT_FALSE(camera::NormalizedRenderTargetExt::get_texture_view(manual, windows, manual_views).has_value());
    EXPECT_EQ(camera::NormalizedRenderTargetExt::get_texture_view_format(manual, windows, manual_views),
              wgpu::TextureFormat::eRGBA8UnormSrgb);
    auto manual_info = camera::NormalizedRenderTargetExt::get_render_target_info(manual, {}, manual_views);
    ASSERT_TRUE(manual_info.has_value());
    EXPECT_EQ(manual_info->physical_size, glm::uvec2(320, 180));
    EXPECT_FLOAT_EQ(manual_info->scale_factor, 1.0f);
    EXPECT_TRUE(camera::NormalizedRenderTargetExt::is_changed(manual, {}, {}));

    const ::epix::camera::NormalizedRenderTarget none{::epix::camera::NoColorTarget{glm::uvec2(12, 34)}};
    auto none_info = camera::NormalizedRenderTargetExt::get_render_target_info(none, {}, manual_views);
    ASSERT_TRUE(none_info.has_value());
    EXPECT_EQ(none_info->physical_size, glm::uvec2(12, 34));
    EXPECT_FALSE(camera::NormalizedRenderTargetExt::is_changed(none, {}, {}));

    const ::epix::camera::NormalizedRenderTarget missing{::epix::camera::ManualTextureViewHandle{8}};
    auto missing_info = camera::NormalizedRenderTargetExt::get_render_target_info(missing, {}, manual_views);
    ASSERT_FALSE(missing_info.has_value());
    EXPECT_TRUE(std::holds_alternative<camera::MissingRenderTargetInfoError::TextureView>(missing_info.error().value));
}

TEST(GlobalsUniform, CpuFields) {
    GlobalsUniform g;
    g.time        = 1.5f;
    g.delta_time  = 0.016f;
    g.frame_count = 42;
    EXPECT_FLOAT_EQ(g.time, 1.5f);
    EXPECT_FLOAT_EQ(g.delta_time, 0.016f);
    EXPECT_EQ(g.frame_count, 42u);
    static_assert(render_resource::ShaderType<GlobalsUniform>);
    static_assert(sizeof(GlobalsUniform) == 12);
}

TEST(RenderAdapterInfo, AndroidWorkaroundHelpersArePlatformGated) {
    RenderAdapterInfo adreno{.device = "Adreno (TM) 642L"};
    RenderAdapterInfo mali{.device = "Mali-G715", .description = "driver v1.r43p0"};
#if defined(__ANDROID__)
    EXPECT_EQ(get_adreno_model(adreno), 642u);
    EXPECT_EQ(get_mali_driver_version(mali), 43u);
#else
    EXPECT_FALSE(get_adreno_model(adreno).has_value());
    EXPECT_FALSE(get_mali_driver_version(mali).has_value());
#endif
}

TEST(RetainedViewEntity, Equality) {
    view::RetainedViewEntity a{sync_world::MainEntity{Entity{1}}, std::nullopt, 0};
    view::RetainedViewEntity b{sync_world::MainEntity{Entity{1}}, std::nullopt, 0};
    view::RetainedViewEntity c{sync_world::MainEntity{Entity{2}}, std::nullopt, 0};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
