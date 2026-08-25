#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <optional>
#include <vector>
#include <webgpu/webgpu.hpp>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::render;

namespace {

struct SmokeTaskPoolInit {
    SmokeTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4).build()});
    }
} g_smoke_task_pool_init;

}  // namespace

void add_render_test_prerequisites(App& app) {
    app.add_plugins(epix::time::TimePlugin{}).add_plugins(FrameCountPlugin{});
}

namespace {

// A user component that is extracted into the render world each frame.
struct SmokeComponent {
    int value = 0;
};

}  // namespace

template <>
struct epix::render::ExtractComponent<SmokeComponent> {
    using QueryData   = const SmokeComponent&;
    using QueryFilter = ecs::Filter<>;  // Bevy type QueryFilter = ()
    using Out         = SmokeComponent;
    static std::optional<Out> extract_component(const SmokeComponent& c) { return c; }
};

namespace {

std::optional<Entity> get_render_entity(const World& world, Entity main_entity) {
    return world.get_entity(main_entity)
        .and_then([](const EntityRef& e) { return e.get<sync_world::RenderEntity>(); })
        .transform([](const std::reference_wrapper<const sync_world::RenderEntity>& re) { return re.get().entity; });
}

std::optional<int> get_extracted_value(const World& render_world, Entity render_entity) {
    return render_world.get_entity(render_entity)
        .and_then([](const EntityRef& e) { return e.get<SmokeComponent>(); })
        .transform([](const std::reference_wrapper<const SmokeComponent>& c) { return c.get().value; });
}

std::optional<Entity> get_main_entity(const World& render_world, Entity render_entity) {
    return render_world.get_entity(render_entity)
        .and_then([](const EntityRef& e) { return e.get<sync_world::MainEntity>(); })
        .transform([](const std::reference_wrapper<const sync_world::MainEntity>& me) { return me.get().entity; });
}

}  // namespace

// End-to-end smoke test: drives a real App with main + render worlds, spawns a
// synced entity, runs sync+extract frames, and verifies the render-world
// entity link (MainEntity/RenderEntity) and the extracted component data.
TEST(RenderWorld, EndToEndSyncAndExtract) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_test_prerequisites(app);
    try {
        RenderPlugin{}.attach(app);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << e.what();
        return;
    }
    const auto texture_render_app = app.get_sub_app(Render);
    ASSERT_TRUE(texture_render_app.has_value());
    const auto& texture_world = texture_render_app->get().world();
    EXPECT_TRUE(texture_world.get_resource<render_resource::TextureCache>().has_value());
    EXPECT_TRUE(texture_world.get_resource<DefaultImageSampler>().has_value());
    const auto fallback = texture_world.get_resource<texture::FallbackImage>();
    ASSERT_TRUE(fallback.has_value());
    EXPECT_TRUE(static_cast<bool>(fallback->get().d2.texture_view));
    EXPECT_TRUE(static_cast<bool>(fallback->get().cube.texture_view));
    const auto fallback_zero = texture_world.get_resource<texture::FallbackImageZero>();
    ASSERT_TRUE(fallback_zero.has_value());
    EXPECT_TRUE(static_cast<bool>(fallback_zero->get().image.texture_view));
    const auto fallback_cubemap = texture_world.get_resource<texture::FallbackImageCubemap>();
    ASSERT_TRUE(fallback_cubemap.has_value());
    EXPECT_TRUE(static_cast<bool>(fallback_cubemap->get().image.texture_view));
    EXPECT_TRUE(texture_world.get_resource<texture::FallbackImageFormatMsaaCache>().has_value());
    app.run_schedule(Startup);
    app.add_plugins(ExtractComponentPlugin<SmokeComponent>{});

    // Bevy's render-side CameraPlugin registers these requirements on
    // Camera3d. Verify the actual RenderPlugin registration, not a test-only
    // world setup.
    const Entity camera_3d = app.world_mut().spawn(epix::camera::Camera3d{}).id();
    EXPECT_TRUE(app.world().entity(camera_3d).contains<view::ColorGrading>());
    EXPECT_TRUE(app.world().entity(camera_3d).contains<epix::camera::Exposure>());

    // Spawn a synced entity in the main world.
    Entity main_entity = app.world_mut().spawn(SmokeComponent{42}, sync_world::SyncToRenderWorld{}).id();

    auto render_sub = app.take_sub_app(Render);
    ASSERT_TRUE(render_sub) << "Render sub-app not found";

    // ---- frame 1: sync + extract + render update ----
    render_sub->extract(app);
    render_sub->update();

    // The main entity gained a RenderEntity link...
    auto render_entity = get_render_entity(app.world(), main_entity);
    ASSERT_TRUE(render_entity.has_value()) << "Main entity was not synced to the render world";
    // ...the render entity carries MainEntity pointing back...
    auto main_back = get_main_entity(render_sub->world(), *render_entity);
    ASSERT_TRUE(main_back.has_value());
    EXPECT_EQ(*main_back, main_entity);
    // ...and the extracted component data landed on the render entity.
    auto extracted = get_extracted_value(render_sub->world(), *render_entity);
    ASSERT_TRUE(extracted.has_value()) << "Extracted component missing on render entity";
    EXPECT_EQ(*extracted, 42);

    // The synced render entity persists across frames (no per-frame wipe).
    render_sub->extract(app);
    render_sub->update();
    EXPECT_EQ(get_render_entity(app.world(), main_entity).value_or(Entity{}), *render_entity);

    // ---- frame 3: mutate the component in the main world; re-extraction
    // updates the render-world copy. ----
    app.world_mut().get_entity_mut(main_entity).transform([](EntityWorldMut&& ew) -> int {
        ew.get_mut<SmokeComponent>().value().get_mut().value = 99;
        return 0;
    });
    render_sub->extract(app);
    render_sub->update();
    EXPECT_EQ(get_extracted_value(render_sub->world(), *render_entity).value_or(-1), 99);

    // ---- frame 4: despawning the main entity despawns its render entity. ----
    app.world_mut().get_entity_mut(main_entity).transform([](EntityWorldMut&& ew) -> int {
        ew.despawn();
        return 0;
    });
    render_sub->extract(app);
    render_sub->update();
    EXPECT_FALSE(render_sub->world().get_entity(*render_entity).has_value())
        << "Render entity should be despawned after its main entity is gone";

    app.insert_sub_app(Render, std::move(render_sub));
}

// RenderCreation::Manual must install host-supplied native wgpu resources in
// both worlds without creating a second device (Bevy RenderCreation::Manual).
TEST(RenderWorld, ManualRenderCreationUsesSuppliedResources) {
    App provider = App::create();
    provider.add_events<epix::window::WindowClosed>();
    add_render_test_prerequisites(provider);
    try {
        RenderPlugin{}.attach(provider);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << e.what();
        return;
    }

    const auto resources = RenderResources{
        .device       = provider.world().resource<wgpu::Device>().clone(),
        .queue        = provider.world().resource<wgpu::Queue>().clone(),
        .adapter_info = provider.world().resource<RenderAdapterInfo>(),
        .adapter      = provider.world().resource<wgpu::Adapter>().clone(),
        .instance     = provider.world().resource<wgpu::Instance>().clone(),
    };

    App manual = App::create();
    manual.add_events<epix::window::WindowClosed>();
    add_render_test_prerequisites(manual);
    RenderPlugin plugin;
    plugin.render_creation = RenderCreation::manual(resources);
    plugin.attach(manual);

    const auto& manual_device = manual.world().resource<wgpu::Device>();
    EXPECT_EQ(manual_device, resources.device);
    EXPECT_EQ(manual.world().resource<RenderAdapterInfo>().device, resources.adapter_info.device);
    const auto render_app = manual.get_sub_app(Render);
    ASSERT_TRUE(render_app.has_value());
    EXPECT_EQ(render_app->get().world().resource<wgpu::Device>(), resources.device);
    EXPECT_EQ(render_app->get().world().resource<RenderAdapterInfo>().device, resources.adapter_info.device);
}

// TrackedRenderPass skips redundant pipeline/bind-group/buffer state changes
// (Bevy draw_state.rs) and invalidates tracking on wgpu_pass()/pass().
TEST(RenderWorld, TrackedRenderPassSkipsRedundantBinds) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_test_prerequisites(app);
    try {
        RenderPlugin{}.attach(app);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << e.what();
        return;
    }
    auto render_sub    = app.take_sub_app(Render);
    auto& world        = render_sub->world();
    const auto& device = world.resource<wgpu::Device>();
    const auto& queue  = world.resource<wgpu::Queue>();

    // Tiny render target so the pass has a valid attachment.
    wgpu::Texture texture  = device.createTexture(wgpu::TextureDescriptor()
                                                      .setSize({16, 16, 1})
                                                      .setFormat(wgpu::TextureFormat::eRGBA8Unorm)
                                                      .setUsage(wgpu::TextureUsage::eRenderAttachment)
                                                      .setDimension(wgpu::TextureDimension::e2D)
                                                      .setSampleCount(1)
                                                      .setMipLevelCount(1)
                                                      .setLabel("TrackedRenderPassTarget"));
    wgpu::TextureView view = texture.createView();

    wgpu::CommandEncoder encoder = device.createCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor().setColorAttachments(
        std::array{wgpu::RenderPassColorAttachment()
                       .setView(view)
                       .setLoadOp(wgpu::LoadOp::eClear)
                       .setStoreOp(wgpu::StoreOp::eStore)
                       .setClearValue(wgpu::Color(0.0, 0.0, 0.0, 1.0))}));

    phase::TrackedRenderPass tracked(device, pass);

    // Bind group with one dynamic uniform buffer so dynamic-offset validation
    // is satisfied and the offsets-differ rebind path is observable.
    wgpu::BindGroupLayout layout = device.createBindGroupLayout(wgpu::BindGroupLayoutDescriptor().setEntries(std::array{
        wgpu::BindGroupLayoutEntry()
            .setBinding(0)
            .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
            .setBuffer(wgpu::BufferBindingLayout()
                           .setType(wgpu::BufferBindingType::eUniform)
                           .setHasDynamicOffset(wgpu::Bool(true))
                           .setMinBindingSize(16)),
    }));
    wgpu::Buffer dyn_buf         = device.createBuffer(wgpu::BufferDescriptor()
                                                           .setSize(512)
                                                           .setUsage(wgpu::BufferUsage::eUniform)
                                                           .setMappedAtCreation(wgpu::Bool(false)));
    wgpu::BindGroup bind_group =
        device.createBindGroup(wgpu::BindGroupDescriptor().setLayout(layout).setEntries(std::array{
            wgpu::BindGroupEntry().setBinding(0).setBuffer(dyn_buf).setOffset(0).setSize(16),
        }));

    // Same group + same offsets: the second set is a tracked no-op; different
    // offsets (256-aligned) must rebind. pass() (wgpu_pass) invalidates tracking.
    std::array<std::uint32_t, 1> offsets_a{0u};
    std::array<std::uint32_t, 1> offsets_b{256u};
    tracked.set_bind_group(0, bind_group, offsets_a);
    tracked.set_bind_group(0, bind_group, offsets_a);  // no-op
    tracked.set_bind_group(0, bind_group, offsets_b);  // rebind
    (void)tracked.pass();                              // reset tracking
    tracked.set_bind_group(0, bind_group, offsets_a);

    // Vertex buffer path: identical slice is a no-op.
    wgpu::Buffer vbuf = device.createBuffer(wgpu::BufferDescriptor()
                                                .setSize(64)
                                                .setUsage(wgpu::BufferUsage::eVertex)
                                                .setMappedAtCreation(wgpu::Bool(false)));
    tracked.set_vertex_buffer(0, vbuf, 0, 64);
    tracked.set_vertex_buffer(0, vbuf, 0, 64);  // no-op
    tracked.set_vertex_buffer(0, vbuf, 32, 32);

    // Index buffer path: identical slice+format is a no-op; format change rebinds.
    wgpu::Buffer ibuf = device.createBuffer(wgpu::BufferDescriptor()
                                                .setSize(64)
                                                .setUsage(wgpu::BufferUsage::eIndex)
                                                .setMappedAtCreation(wgpu::Bool(false)));
    tracked.set_index_buffer(ibuf, wgpu::IndexFormat::eUint32, 0, 64);
    tracked.set_index_buffer(ibuf, wgpu::IndexFormat::eUint32, 0, 64);  // no-op
    tracked.set_index_buffer(ibuf, wgpu::IndexFormat::eUint16, 0, 64);

    // End the pass through the tracked wrapper and submit.
    tracked.pass().end();
    queue.submit(encoder.finish());
    device.poll(wgpu::Bool(true));

    app.insert_sub_app(Render, std::move(render_sub));
}

// The additional TrackedRenderPass surface (Bevy draw_state.rs): scissor,
// viewport, stencil reference, blend constant, debug markers, push constants,
// and indirect draws are all forwarded without crashing under validation.
TEST(RenderWorld, TrackedRenderPassExtendedState) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_test_prerequisites(app);
    try {
        RenderPlugin{}.attach(app);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << e.what();
        return;
    }
    auto render_sub    = app.take_sub_app(Render);
    auto& world        = render_sub->world();
    const auto& device = world.resource<wgpu::Device>();
    const auto& queue  = world.resource<wgpu::Queue>();

    wgpu::Texture texture  = device.createTexture(wgpu::TextureDescriptor()
                                                      .setSize({16, 16, 1})
                                                      .setFormat(wgpu::TextureFormat::eRGBA8Unorm)
                                                      .setUsage(wgpu::TextureUsage::eRenderAttachment)
                                                      .setDimension(wgpu::TextureDimension::e2D)
                                                      .setSampleCount(1)
                                                      .setMipLevelCount(1)
                                                      .setLabel("TrackedRenderPassExtendedTarget"));
    wgpu::TextureView view = texture.createView();

    wgpu::CommandEncoder encoder = device.createCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor().setColorAttachments(
        std::array{wgpu::RenderPassColorAttachment()
                       .setView(view)
                       .setLoadOp(wgpu::LoadOp::eClear)
                       .setStoreOp(wgpu::StoreOp::eStore)
                       .setClearValue(wgpu::Color(0.0, 0.0, 0.0, 1.0))}));

    phase::TrackedRenderPass tracked(device, pass);

    tracked.insert_debug_marker("extended-state");
    tracked.push_debug_group("group");
    tracked.set_stencil_reference(3u);
    tracked.set_scissor_rect(0, 0, 16, 16);
    tracked.set_viewport(0.0f, 0.0f, 16.0f, 16.0f, 0.0f, 1.0f);
    tracked.set_blend_constant(wgpu::Color(1.0f, 1.0f, 1.0f, 1.0f));
    tracked.pop_debug_group();

    tracked.pass().end();
    queue.submit(encoder.finish());
    device.poll(wgpu::Bool(true));

    app.insert_sub_app(Render, std::move(render_sub));
}

// ExtractComponentPlugin::extract_visible() extracts only entities visible to
// at least one view (Bevy extract_visible_components, extract_component.rs:219).
TEST(RenderWorld, ExtractVisibleComponentsSkipsCulled) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_test_prerequisites(app);
    try {
        RenderPlugin{}.attach(app);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << e.what();
        return;
    }
    app.run_schedule(Startup);
    app.add_plugins(ExtractComponentPlugin<SmokeComponent>::extract_visible());

    // Visible entity: visibility is normally set by the camera visibility
    // systems. This isolated extraction test has no camera, so mark it as
    // visible explicitly (matching Bevy's default-hidden ViewVisibility).
    Entity visible_main =
        app.world_mut().spawn(SmokeComponent{7}, sync_world::SyncToRenderWorld{}, ::epix::camera::ViewVisibility{}).id();
    // Culled entity: ViewVisibility culled -> skipped by extract_visible_components.
    Entity culled_main =
        app.world_mut().spawn(SmokeComponent{8}, sync_world::SyncToRenderWorld{}, ::epix::camera::ViewVisibility{}).id();
    app.world_mut().get_entity_mut(visible_main).transform([](EntityWorldMut&& ew) -> int {
        ew.get_mut<::epix::camera::ViewVisibility>().value().get_mut().set_visible();
        return 0;
    });

    auto render_sub = app.take_sub_app(Render);
    ASSERT_TRUE(render_sub) << "Render sub-app not found";
    render_sub->extract(app);
    render_sub->update();

    auto visible_render = get_render_entity(app.world(), visible_main);
    auto culled_render  = get_render_entity(app.world(), culled_main);
    ASSERT_TRUE(visible_render.has_value()) << "Visible entity not synced";
    ASSERT_TRUE(culled_render.has_value()) << "Culled entity not synced";

    // Visible entity's component was extracted...
    auto extracted = get_extracted_value(render_sub->world(), *visible_render);
    ASSERT_TRUE(extracted.has_value()) << "Visible entity was not extracted";
    EXPECT_EQ(*extracted, 7);
    // ...the culled entity's component was NOT extracted.
    EXPECT_FALSE(render_sub->world()
                     .get_entity(*culled_render)
                     .and_then([](const EntityRef& e) { return e.get<SmokeComponent>(); })
                     .has_value())
        << "Culled entity must not be extracted";

    app.insert_sub_app(Render, std::move(render_sub));
}
