#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <epix/ecs.hpp>
#include <epix/render.hpp>

using namespace epix::ecs;
using namespace epix::render;

// Mirrors Bevy's ViewRangefinder3d unit test (bevy_render/render_phase/rangefinder.rs).
TEST(ViewRangefinder3d, Distance) {
    const glm::mat4 view_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    const auto rangefinder = phase::ViewRangefinder3d::from_world_from_view(view_matrix);
    EXPECT_FLOAT_EQ(rangefinder.distance(glm::vec3(0.0f, 0.0f, 0.0f)), 1.0f);
    EXPECT_FLOAT_EQ(rangefinder.distance(glm::vec3(0.0f, 0.0f, 1.0f)), 2.0f);
}

// RenderLayers truth table matches Bevy RenderLayers::intersects.
TEST(RenderLayers, Intersects) {
    auto layer0 = camera::RenderLayers::layer(0);
    auto layer1 = camera::RenderLayers::layer(1);
    auto all    = camera::RenderLayers::all();
    auto none   = camera::RenderLayers::none();
    EXPECT_TRUE(layer0.intersects(layer0));
    EXPECT_FALSE(layer0.intersects(layer1));
    EXPECT_TRUE(all.intersects(layer0));
    EXPECT_TRUE(layer0.intersects(all));
    EXPECT_FALSE(none.intersects(layer0));
    EXPECT_FALSE(layer0.intersects(none));
    EXPECT_TRUE(all.intersects(all));  // inverted x inverted always overlaps
    EXPECT_TRUE(layer0.contains(0));
    EXPECT_FALSE(layer0.contains(1));
}

TEST(Msaa, FromSamples) {
    EXPECT_EQ(view::samples(view::Msaa::Off), 1u);
    EXPECT_EQ(view::samples(view::Msaa::Sample4), 4u);
    EXPECT_EQ(view::msaa_from_samples(8), view::Msaa::Sample8);
    EXPECT_THROW(view::msaa_from_samples(3), std::runtime_error);
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
    key.width  = 4;
    key.height = 4;
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
    tex.order   = 1;
    tex.target  = camera::RenderTarget::from_texture(wgpu::Texture{});
    camera::SortedCamera win;
    win.order   = 1;
    win.target  = camera::RenderTarget::from_primary();
    EXPECT_TRUE(tex.sort_key() < win.sort_key());
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

TEST(RetainedViewEntity, Equality) {
    view::RetainedViewEntity a{sync_world::MainEntity{Entity{1}}, std::nullopt, 0};
    view::RetainedViewEntity b{sync_world::MainEntity{Entity{1}}, std::nullopt, 0};
    view::RetainedViewEntity c{sync_world::MainEntity{Entity{2}}, std::nullopt, 0};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
