#include <gtest/gtest.h>
#include <epix/app.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <array>
#include <cmath>
#include <memory>
#include <ranges>
#include <unordered_set>

using namespace epix::ecs;

TEST(CameraMarkers, SupplyCameraRenderTargetRequirements) {
    epix::ecs::World world(2);
    const auto raw_camera = world.spawn(::epix::camera::Camera{}).id();
    const auto camera2d   = world.spawn(::epix::camera::Camera2d{}).id();
    const auto camera3d   = world.spawn(::epix::camera::Camera3d{}).id();

    // Bevy's base Camera deliberately does not require a Projection: custom
    // graph cameras can be target-only.  Camera2d/Camera3d add it themselves.
    EXPECT_FALSE(world.entity(raw_camera).contains<::epix::camera::Projection>());
    EXPECT_TRUE(world.entity(camera2d).contains<::epix::camera::Camera>());
    EXPECT_TRUE(world.entity(camera2d).contains<::epix::camera::RenderTarget>());
    EXPECT_TRUE(world.entity(camera2d).contains<::epix::camera::Projection>());
    EXPECT_TRUE(world.entity(camera3d).contains<::epix::camera::Camera>());
    EXPECT_TRUE(world.entity(camera3d).contains<::epix::camera::RenderTarget>());
    EXPECT_TRUE(world.entity(camera3d).contains<::epix::camera::Projection>());
    EXPECT_TRUE(world.entity(camera2d).get<::epix::camera::Projection>()->get().as_orthographic().has_value());
    EXPECT_TRUE(world.entity(camera3d).get<::epix::camera::Projection>()->get().as_perspective().has_value());
}

TEST(VisibilityPlugin, StandaloneRegistersVisibilityRequirements) {
    auto app = epix::app::App::create();
    ::epix::camera::VisibilityPlugin{}.attach(app);
    const Entity entity = app.world_mut().spawn(::epix::camera::Visibility{}).id();
    EXPECT_TRUE(app.world().entity(entity).contains<::epix::camera::InheritedVisibility>());
    EXPECT_TRUE(app.world().entity(entity).contains<::epix::camera::ViewVisibility>());
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
    static_assert(std::ranges::view<decltype(dynamic.iter())>);
    EXPECT_TRUE(std::ranges::equal(dynamic.iter(), std::array<std::size_t, 3>{0, 64, 130}));
    EXPECT_EQ(dynamic.without(130).without(64).without(0), none);
    EXPECT_EQ(dynamic & ::epix::camera::RenderLayers::layer(64), ::epix::camera::RenderLayers::layer(64));
    EXPECT_EQ(dynamic | layer1, ::epix::camera::RenderLayers::from_layers(std::array<std::size_t, 4>{0, 1, 64, 130}));
}

TEST(RenderTarget, VariantsNormalizeAndKeepDistinctIdentities) {

    const auto window = ::epix::camera::RenderTarget::from_window(epix::ecs::Entity{.uid = 42});
    const auto manual =
        ::epix::camera::RenderTarget::from_manual_texture_view(::epix::camera::ManualTextureViewHandle{42});
    const auto none = ::epix::camera::RenderTarget::none(glm::uvec2{42, 0});

    EXPECT_TRUE(window.normalize(std::nullopt).has_value());
    EXPECT_TRUE(manual.normalize(std::nullopt).has_value());
    EXPECT_TRUE(none.normalize(std::nullopt).has_value());
    EXPECT_NE(window.normalize(std::nullopt)->identity(), manual.normalize(std::nullopt)->identity());
    EXPECT_NE(manual.normalize(std::nullopt)->identity(), none.normalize(std::nullopt)->identity());

}

// VisibilityRange equality/hashing participate in RenderVisibilityRanges
// dedup (bevy_camera visibility ranges).
TEST(VisibilityRange, EqualityAndHash) {
    ::epix::camera::VisibilityRange a;
    a.start_margin_start              = 1.0f;
    a.end_margin_end                  = 50.0f;
    ::epix::camera::VisibilityRange b = a;
    ::epix::camera::VisibilityRange c = a;
    c.end_margin_end                  = 51.0f;
    ::epix::camera::VisibilityRange d = a;
    d.use_aabb                        = true;
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_EQ(std::hash<::epix::camera::VisibilityRange>{}(a), std::hash<::epix::camera::VisibilityRange>{}(b));
}

TEST(Exposure, BlenderDefaultMatchesBevy) {
    ::epix::camera::Exposure exposure;
    EXPECT_FLOAT_EQ(exposure.exposure(), std::exp2(-9.7f) / 1.2f);
    EXPECT_FLOAT_EQ(::epix::camera::Exposure::SUNLIGHT.ev100, 15.0f);
    const ::epix::camera::PhysicalCameraParameters physical{};
    EXPECT_FLOAT_EQ(::epix::camera::Exposure::from_physical_camera(physical).ev100, physical.ev100());
}

TEST(Viewport, ClampToTargetMatchesBevy) {
    ::epix::camera::Viewport viewport{.physical_position = glm::uvec2(90, 150), .physical_size = glm::uvec2(30, 20)};
    viewport.clamp_to_size(glm::uvec2(100, 100));
    EXPECT_EQ(viewport.physical_position, glm::uvec2(90, 99));
    EXPECT_EQ(viewport.physical_size, glm::uvec2(10, 1));

    viewport = ::epix::camera::Viewport{.physical_position = glm::uvec2(8, 2), .physical_size = glm::uvec2(3, 3)};
    viewport.clamp_to_size(glm::uvec2(0, 0));
    EXPECT_EQ(viewport.physical_position, glm::uvec2(0, 0));
    EXPECT_EQ(viewport.physical_size, glm::uvec2(0, 0));
}

TEST(MainPassResolutionOverride, StoresPhysicalDimensions) {
    ::epix::camera::MainPassResolutionOverride override{glm::uvec2(640, 360)};
    EXPECT_EQ(override.size, glm::uvec2(640, 360));

    const std::optional<::epix::camera::Viewport> viewport =
        ::epix::camera::Viewport{.physical_position = glm::uvec2(3, 4), .physical_size = glm::uvec2(10, 10)};
    auto overridden = ::epix::camera::Viewport::from_viewport_and_override(viewport, glm::uvec2(640, 360));
    ASSERT_TRUE(overridden.has_value());
    EXPECT_EQ(overridden->physical_position, glm::uvec2(3, 4));
    EXPECT_EQ(overridden->physical_size, glm::uvec2(640, 360));
}

TEST(CameraOutput, ModesAndWritebackMatchBevy) {
    ::epix::camera::CameraOutputMode output;
    ASSERT_TRUE(std::holds_alternative<::epix::camera::CameraOutputMode::Write>(output));
    const auto& write = std::get<::epix::camera::CameraOutputMode::Write>(output);
    EXPECT_FALSE(write.blend_state.has_value());
    EXPECT_TRUE(std::holds_alternative<::epix::camera::ClearColorConfig::Default>(write.clear_color));
    EXPECT_TRUE(std::holds_alternative<::epix::camera::ClearColorConfig::Default>(::epix::camera::ClearColorConfig{}));
    EXPECT_TRUE(std::holds_alternative<::epix::camera::ClearColorConfig::Custom>(::epix::camera::ClearColorConfig{
        ::epix::camera::ClearColorConfig::Custom{::epix::camera::ClearColor{1.0f, 0.0f, 0.0f, 1.0f}}}));
    EXPECT_TRUE(std::holds_alternative<::epix::camera::ClearColorConfig::None>(
        ::epix::camera::ClearColorConfig{::epix::camera::ClearColorConfig::None{}}));
    EXPECT_TRUE(std::holds_alternative<::epix::camera::CameraOutputMode::Skip>(
        ::epix::camera::CameraOutputMode{::epix::camera::CameraOutputMode::Skip{}}));
    EXPECT_EQ(::epix::camera::MsaaWriteback::Auto, ::epix::camera::MsaaWriteback::Auto);
}

TEST(ClearColor, DefaultMatchesBevyDarkGray) {
    // bevy_camera::ClearColor::default() is Color::srgb_u8(43, 44, 47),
    // converted to linear RGB before it is written through an sRGB target.
    const ::epix::camera::ClearColor clear;
    EXPECT_NEAR(clear.r, 0.02415763f, 1e-7f);
    EXPECT_NEAR(clear.g, 0.02518686f, 1e-7f);
    EXPECT_NEAR(clear.b, 0.02842604f, 1e-7f);
    EXPECT_FLOAT_EQ(clear.a, 1.0f);
}

TEST(SubCameraView, DefaultsMatchBevy) {
    ::epix::camera::SubCameraView sub;
    EXPECT_EQ(sub.full_size, glm::uvec2(1, 1));
    EXPECT_EQ(sub.offset, glm::vec2(0.0f));
    EXPECT_EQ(sub.size, glm::uvec2(1, 1));
}

TEST(SubCameraView, FullSizeCropPreservesProjection) {
    ::epix::camera::PerspectiveProjection projection;
    projection.update(1920.0f, 1080.0f);
    const ::epix::camera::SubCameraView whole{
        .full_size = glm::uvec2(1920, 1080), .offset = glm::vec2(0.0f), .size = glm::uvec2(1920, 1080)};
    EXPECT_EQ(projection.get_clip_from_view_for_sub(whole), projection.get_clip_from_view());
}

TEST(CameraProjection, UsesBevyReverseZConventions) {
    // Bevy's perspective projection is right-handed with -Z forward and an
    // infinite reverse-Z depth range: near maps to 1, far tends to 0.
    ::epix::camera::PerspectiveProjection perspective;
    const auto perspective_matrix = perspective.get_clip_from_view();
    const auto near_clip          = perspective_matrix * glm::vec4(0.0f, 0.0f, -perspective.near_plane, 1.0f);
    const auto far_clip           = perspective_matrix * glm::vec4(0.0f, 0.0f, -1000000.0f, 1.0f);
    EXPECT_NEAR(near_clip.z / near_clip.w, 1.0f, 1e-5f);
    EXPECT_NEAR(far_clip.z / far_clip.w, 0.0f, 1e-5f);

    // Projection::default is Bevy's Perspective variant. Camera2d separately
    // supplies OrthographicProjection::default_2d through its requirements.
    EXPECT_TRUE(::epix::camera::Projection{}.as_perspective().has_value());
    EXPECT_EQ(::epix::camera::OrthographicProjection::default_3d().near_plane, 0.0f);
    EXPECT_EQ(::epix::camera::OrthographicProjection::default_2d().near_plane, -1000.0f);

    // The Bevy trait receives cascade-selected view-space depths rather than
    // always using the projection's own near/far fields.
    const auto corners = perspective.get_frustum_corners(-2.0f, -20.0f);
    EXPECT_EQ(corners[0].z, -2.0f);
    EXPECT_EQ(corners[7].z, -20.0f);
    EXPECT_NEAR(corners[4].x / corners[0].x, 10.0f, 1e-5f);
}

TEST(CameraProjection, CustomProjectionRoundTripsConcreteType) {
    auto projection = ::epix::camera::Projection::custom(::epix::camera::OrthographicProjection::default_2d());
    auto* custom    = projection.get_custom<::epix::camera::OrthographicProjection>();
    ASSERT_NE(custom, nullptr);
    custom->scale = 2.0f;
    projection.update(400.0f, 200.0f);
    EXPECT_NE(projection.get_clip_from_view()[0][0], 0.0f);
}

TEST(CameraProjection, MatchesBevyContractAndDefaultFrustumBehavior) {
    // Bevy's trait has no near-plane accessors or setters. This custom
    // projection intentionally supplies only the required trait operations.
    struct MinimalProjection {
        glm::mat4 get_clip_from_view() const { return glm::mat4(1.0f); }
        glm::mat4 get_clip_from_view_for_sub(const ::epix::camera::SubCameraView&) const {
            return get_clip_from_view();
        }
        void update(float, float) {}
        float get_far() const { return 42.0f; }
        std::array<glm::vec3, 8> get_frustum_corners(float z_near, float z_far) const {
            return {glm::vec3(1.0f, -1.0f, z_near), glm::vec3(1.0f, 1.0f, z_near),
                    glm::vec3(-1.0f, 1.0f, z_near), glm::vec3(-1.0f, -1.0f, z_near),
                    glm::vec3(1.0f, -1.0f, z_far),  glm::vec3(1.0f, 1.0f, z_far),
                    glm::vec3(-1.0f, 1.0f, z_far),  glm::vec3(-1.0f, -1.0f, z_far)};
        }
    };
    static_assert(::epix::camera::CameraProjection<MinimalProjection>);

    const ::epix::transform::GlobalTransform transform{};
    const ::epix::camera::PerspectiveProjection perspective{};
    const auto expected = ::epix::camera::Frustum::from_clip_from_world_custom_far(
        perspective.get_clip_from_view(), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), perspective.get_far());
    const auto actual = perspective.compute_frustum(transform);
    EXPECT_EQ(actual.planes, expected.planes);

    EXPECT_TRUE(::epix::camera::Projection::perspective().is_perspective());
    EXPECT_FALSE(::epix::camera::Projection::orthographic().is_perspective());
    EXPECT_FALSE(::epix::camera::Projection::custom(MinimalProjection{}).is_perspective());
    EXPECT_TRUE(::epix::camera::Projection::custom(::epix::camera::PerspectiveProjection{}).is_perspective());
}

TEST(CameraSystem, MatchesBevyChangeSensitiveLogicalAndDpiUpdates) {
    struct CountingProjection {
        std::shared_ptr<int> updates;

        glm::mat4 get_clip_from_view() const { return glm::mat4(1.0f); }
        glm::mat4 get_clip_from_view_for_sub(const ::epix::camera::SubCameraView&) const {
            return get_clip_from_view();
        }
        void update(float, float) { ++*updates; }
        float get_far() const { return 1000.0f; }
        std::array<glm::vec3, 8> get_frustum_corners(float near_depth, float far_depth) const {
            return {glm::vec3(1.0f, -1.0f, near_depth), glm::vec3(1.0f, 1.0f, near_depth),
                    glm::vec3(-1.0f, 1.0f, near_depth), glm::vec3(-1.0f, -1.0f, near_depth),
                    glm::vec3(1.0f, -1.0f, far_depth),  glm::vec3(1.0f, 1.0f, far_depth),
                    glm::vec3(-1.0f, 1.0f, far_depth),  glm::vec3(-1.0f, -1.0f, far_depth)};
        }
    };

    auto app = epix::app::App::create();
    ::epix::camera::CameraPlugin{}.attach(app);
    const Entity window = app.world_mut()
                              .spawn(::epix::window::Window{.physical_size = {400, 200}, .scale_factor = 1.0f},
                                     ::epix::window::PrimaryWindow{})
                              .id();
    auto updates = std::make_shared<int>(0);
    ::epix::camera::Camera camera;
    camera.viewport = ::epix::camera::Viewport{.physical_position = {10, 5}, .physical_size = {40, 20}};
    const Entity camera_entity = app.world_mut()
                                     .spawn(std::move(camera), ::epix::camera::RenderTarget::from_primary(),
                                            ::epix::camera::Projection::custom(CountingProjection{updates}))
                                     .id();

    app.update();
    EXPECT_EQ(*updates, 1);
    const auto& initial_camera = app.world().entity(camera_entity).get<::epix::camera::Camera>()->get();
    ASSERT_TRUE(initial_camera.logical_viewport_size());
    EXPECT_EQ(*initial_camera.logical_viewport_size(), glm::vec2(40.0f, 20.0f));

    // No target/projection/viewport/sub-view change means no redundant
    // projection update on the following frame.
    app.update();
    EXPECT_EQ(*updates, 1);

    app.world_mut().entity_mut(window).get_mut<::epix::window::Window>()->get_mut().physical_size = {800, 400};
    app.world_mut().entity_mut(window).get_mut<::epix::window::Window>()->get_mut().scale_factor = 2.0f;
    app.world_mut().resource_mut<Events<::epix::window::WindowScaleFactorChanged>>().push({window, 2.0f});
    app.update();

    EXPECT_EQ(*updates, 2);
    const auto& dpi_camera = app.world().entity(camera_entity).get<::epix::camera::Camera>()->get();
    ASSERT_TRUE(dpi_camera.viewport);
    EXPECT_EQ(dpi_camera.viewport->physical_position, glm::uvec2(20, 10));
    EXPECT_EQ(dpi_camera.viewport->physical_size, glm::uvec2(80, 40));
    ASSERT_TRUE(dpi_camera.logical_viewport_size());
    EXPECT_EQ(*dpi_camera.logical_viewport_size(), glm::vec2(40.0f, 20.0f));

    // A normalized explicit window target now fails through the fallible
    // system result instead of silently clearing camera target information.
    auto failing_app = epix::app::App::create();
    failing_app.add_events<::epix::window::WindowResized, ::epix::window::WindowCreated,
                           ::epix::window::WindowScaleFactorChanged>();
    failing_app.world_mut().spawn(::epix::camera::Camera{},
                                  ::epix::camera::RenderTarget::from_window(Entity{.uid = 999}),
                                  ::epix::camera::Projection::perspective());
    auto failing_system = make_system_unique(::epix::camera::camera_system);
    failing_system->initialize(failing_app.world_mut());
    const auto result = failing_system->run({}, failing_app.world_mut());
    ASSERT_FALSE(result);
    ASSERT_TRUE(std::holds_alternative<SystemResultError>(result.error()));
    EXPECT_NE(std::get<SystemResultError>(result.error()).message.find("Camera render target window"),
              std::string::npos);
}

TEST(CameraPlugin, PreservesPreconfiguredClearColor) {
    auto app = epix::app::App::create();
    const ::epix::camera::ClearColor configured{0.25f, 0.5f, 0.75f, 1.0f};
    app.world_mut().insert_resource(configured);

    ::epix::camera::CameraPlugin{}.attach(app);

    const auto& clear_color = app.world().resource<::epix::camera::ClearColor>();
    EXPECT_EQ(clear_color.to_vec4(), configured.to_vec4());
}

TEST(CameraMainTextureUsages, WithReturnsAugmentedCopy) {
    const ::epix::camera::CameraMainTextureUsages defaults;
    const auto augmented = defaults.with(wgpu::TextureUsage::eCopyDst);

    constexpr auto copy_dst = static_cast<std::uint64_t>(wgpu::TextureUsage::eCopyDst);
    EXPECT_EQ(static_cast<std::uint64_t>(defaults.usage) & copy_dst, 0u);
    EXPECT_EQ(static_cast<std::uint64_t>(augmented.usage) & copy_dst, copy_dst);
}

TEST(ScalingMode, VariantsAndProjectionSizingMatchBevy) {
    using ScalingMode = ::epix::camera::ScalingMode;
    ::epix::camera::OrthographicProjection projection;
    EXPECT_TRUE(std::holds_alternative<ScalingMode::WindowSize>(projection.scaling_mode));

    projection.scaling_mode = ScalingMode::Fixed{100.0f, 50.0f};
    projection.update(800.0f, 400.0f);
    EXPECT_FLOAT_EQ(projection.rect.right - projection.rect.left, 100.0f);
    EXPECT_FLOAT_EQ(projection.rect.top - projection.rect.bottom, 50.0f);

    projection.scaling_mode = ScalingMode::AutoMin{200.0f, 100.0f};
    projection.update(1000.0f, 100.0f);
    EXPECT_FLOAT_EQ(projection.rect.right - projection.rect.left, 1000.0f);
    EXPECT_FLOAT_EQ(projection.rect.top - projection.rect.bottom, 100.0f);

    projection.scaling_mode = ScalingMode::AutoMax{1000.0f, 500.0f};
    projection.update(100.0f, 1000.0f);
    EXPECT_FLOAT_EQ(projection.rect.right - projection.rect.left, 50.0f);
    EXPECT_FLOAT_EQ(projection.rect.top - projection.rect.bottom, 500.0f);

    projection.scaling_mode = ScalingMode::FixedVertical{20.0f};
    projection.update(400.0f, 100.0f);
    EXPECT_FLOAT_EQ(projection.rect.right - projection.rect.left, 80.0f);
    EXPECT_FLOAT_EQ(projection.rect.top - projection.rect.bottom, 20.0f);

    projection.scaling_mode = ScalingMode::FixedHorizontal{20.0f};
    projection.update(400.0f, 100.0f);
    EXPECT_FLOAT_EQ(projection.rect.right - projection.rect.left, 20.0f);
    EXPECT_FLOAT_EQ(projection.rect.top - projection.rect.bottom, 5.0f);
}

TEST(Camera3d, DefaultsMatchBevyCameraComponents) {
    const ::epix::camera::Camera3d camera3d;
    ASSERT_TRUE(std::holds_alternative<::epix::camera::Camera3dDepthLoadOp::Clear>(camera3d.depth_load_op));
    EXPECT_EQ(std::get<::epix::camera::Camera3dDepthLoadOp::Clear>(camera3d.depth_load_op).value, 0.0f);
    EXPECT_TRUE(std::holds_alternative<::epix::camera::Camera3dDepthLoadOp::Load>(
        ::epix::camera::Camera3dDepthLoadOp{::epix::camera::Camera3dDepthLoadOp::Load{}}));
    EXPECT_EQ(camera3d.depth_texture_usages.usage(), wgpu::TextureUsage::eRenderAttachment);
    EXPECT_EQ(camera3d.screen_space_specular_transmission_steps, 1u);
    EXPECT_EQ(camera3d.screen_space_specular_transmission_quality,
              ::epix::camera::ScreenSpaceTransmissionQuality::Medium);
}

TEST(CameraCoordinates, ViewportAndNdcConversionsMatchBevy) {
    ::epix::camera::Camera camera;
    camera.computed.target_info =
        ::epix::camera::RenderTargetInfo{.physical_size = glm::uvec2(200, 100), .scale_factor = 2.0f};
    camera.computed.clip_from_view = glm::orthoRH_ZO(-100.0f, 100.0f, -50.0f, 50.0f, 1000.0f, -1000.0f);
    const ::epix::transform::GlobalTransform identity{};

    ASSERT_TRUE(camera.logical_viewport_size().has_value());
    EXPECT_EQ(*camera.logical_viewport_size(), glm::vec2(100.0f, 50.0f));
    EXPECT_EQ(*camera.target_scaling_factor(), 2.0f);

    auto screen = camera.world_to_viewport(identity, glm::vec3(0.0f));
    ASSERT_TRUE(screen.has_value());
    EXPECT_NEAR(screen->x, 50.0f, 1e-5f);
    EXPECT_NEAR(screen->y, 25.0f, 1e-5f);

    auto ndc = camera.viewport_to_ndc(glm::vec2(50.0f, 25.0f));
    ASSERT_TRUE(ndc.has_value());
    EXPECT_NEAR(ndc->x, 0.0f, 1e-5f);
    EXPECT_NEAR(ndc->y, 0.0f, 1e-5f);
    auto world = camera.viewport_to_world_2d(identity, glm::vec2(50.0f, 25.0f));
    ASSERT_TRUE(world.has_value());
    EXPECT_NEAR(world->x, 0.0f, 1e-5f);
    EXPECT_NEAR(world->y, 0.0f, 1e-5f);
}

TEST(CameraCoordinates, ReportsMissingViewportSize) {
    ::epix::camera::Camera camera;
    EXPECT_EQ(camera.viewport_to_ndc(glm::vec2(1.0f, 1.0f)).error(),
              ::epix::camera::ViewportConversionError::NoViewportSize);
}

// ViewVisibility matches Bevy bevy_camera::visibility: it stores only current
// and previous aggregate visibility, with no fixed per-camera bit budget.
TEST(ViewVisibility, Flags) {
    ::epix::camera::ViewVisibility vv;
    EXPECT_FALSE(vv.get());  // Bevy ViewVisibility::HIDDEN
    vv.set_visible();
    EXPECT_TRUE(vv.get());
}

// RenderLayers is the Bevy name for epix's RenderLayers (bevy_camera
// render_layers.rs) - the alias is interchangeable.
TEST(RenderLayers, AliasIsRenderLayers) {
    ::epix::camera::RenderLayers layers = ::epix::camera::RenderLayers::layer(0);
    EXPECT_TRUE(layers.intersects(::epix::camera::RenderLayers::layer(0)));
    EXPECT_FALSE(layers.intersects(::epix::camera::RenderLayers::layer(1)));
    layers = layers.with(3);
    EXPECT_TRUE(layers.intersects(::epix::camera::RenderLayers::layer(3)));
}

// Visibility marker matches Bevy bevy_camera::Visibility (visibility__mod.rs:39-88):
// default Inherited, three kinds, toggle helpers.
TEST(Visibility, MarkersAndToggles) {
    epix::camera::Visibility v;
    EXPECT_EQ(v.type, epix::camera::Visibility::Type::Inherited);
    EXPECT_EQ(epix::camera::Visibility::hidden().type, epix::camera::Visibility::Type::Hidden);
    EXPECT_EQ(epix::camera::Visibility::visible().type, epix::camera::Visibility::Type::Visible);

    // toggle_inherited_visible: Inherited<->Visible, Hidden unaffected.
    v.toggle_inherited_visible();
    EXPECT_EQ(v.type, epix::camera::Visibility::Type::Visible);
    v.toggle_inherited_visible();
    EXPECT_EQ(v.type, epix::camera::Visibility::Type::Inherited);
    v = epix::camera::Visibility::hidden();
    v.toggle_inherited_visible();
    EXPECT_EQ(v.type, epix::camera::Visibility::Type::Hidden);

    // toggle_inherited_hidden: Inherited<->Hidden, Visible unaffected.
    v.toggle_inherited_hidden();
    EXPECT_EQ(v.type, epix::camera::Visibility::Type::Inherited);
    v.toggle_inherited_hidden();
    EXPECT_EQ(v.type, epix::camera::Visibility::Type::Hidden);
    v = epix::camera::Visibility::visible();
    v.toggle_inherited_hidden();
    EXPECT_EQ(v.type, epix::camera::Visibility::Type::Visible);

    // toggle_visible_hidden: Visible<->Hidden, Inherited unaffected.
    v.toggle_visible_hidden();
    EXPECT_EQ(v.type, epix::camera::Visibility::Type::Hidden);
    v.toggle_visible_hidden();
    EXPECT_EQ(v.type, epix::camera::Visibility::Type::Visible);
    v = epix::camera::Visibility::inherited();
    v.toggle_visible_hidden();
    EXPECT_EQ(v.type, epix::camera::Visibility::Type::Inherited);
}

// InheritedVisibility matches Bevy (visibility__mod.rs:108-131): HIDDEN/VISIBLE
// consts and get().
TEST(InheritedVisibility, ConstsAndGet) {
    EXPECT_FALSE(epix::camera::InheritedVisibility::hidden().get());
    EXPECT_TRUE(epix::camera::InheritedVisibility::visible().get());
    epix::camera::InheritedVisibility def;
    EXPECT_TRUE(def.get());
}

// Bevy primitives::Frustum performs a sphere rejection before the OBB test
// in check_visibility.  Exercise both tests with a unit clip volume.
TEST(CameraFrustum, CullsSphereAndOrientedBounds) {
    epix::camera::Frustum frustum;
    frustum.planes = {
        glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},  glm::vec4{-1.0f, 0.0f, 0.0f, 1.0f}, glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
        glm::vec4{0.0f, -1.0f, 0.0f, 1.0f}, glm::vec4{0.0f, 0.0f, 1.0f, 0.0f},  glm::vec4{0.0f, 0.0f, -1.0f, 1.0f},
    };
    EXPECT_TRUE(frustum.intersects_sphere({.center = {0.0f, 0.0f, 0.5f}, .radius = 0.25f}));
    EXPECT_FALSE(frustum.intersects_sphere({.center = {3.0f, 0.0f, 0.5f}, .radius = 0.25f}));

    const epix::camera::Aabb aabb{.center = glm::vec3(0.0f), .half_extents = glm::vec3(0.25f)};
    EXPECT_TRUE(frustum.intersects_obb(aabb, glm::mat4(1.0f)));
    EXPECT_FALSE(frustum.intersects_obb(aabb, glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f))));
}

TEST(CameraPrimitives, MatchBevyClipPlanesAndHelpers) {
    const auto frustum = epix::camera::Frustum::from_clip_from_world(glm::mat4(1.0f));
    // Bevy's zero-to-one-depth extraction uses row2 as the far plane.
    EXPECT_EQ(frustum.planes[5], glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    EXPECT_EQ(epix::camera::face_index_to_name(4), "+z");
    EXPECT_EQ(epix::camera::face_index_to_name(5), "-z");
    EXPECT_EQ(epix::camera::face_index_to_name(6), "invalid");

    const std::array points{glm::vec3(-2.0f, 1.0f, 4.0f), glm::vec3(4.0f, -3.0f, 2.0f)};
    const auto aabb = epix::camera::Aabb::enclosing(points);
    ASSERT_TRUE(aabb);
    EXPECT_EQ(aabb->min(), glm::vec3(-2.0f, -3.0f, 2.0f));
    EXPECT_EQ(aabb->max(), glm::vec3(4.0f, 1.0f, 4.0f));
}

TEST(CameraFrustum, CustomFarMatchesProjectionFrustumConstruction) {
    const auto projection = epix::camera::PerspectiveProjection{};
    const auto frustum    = epix::camera::Frustum::from_clip_from_world_custom_far(
        projection.get_clip_from_view(), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), projection.get_far());
    // The camera faces -Z, so a point beyond the declared finite culling far
    // distance is rejected even though the reverse-Z projection is infinite.
    EXPECT_TRUE(frustum.intersects_sphere({.center = {0.0f, 0.0f, -999.0f}, .radius = 0.1f}));
    EXPECT_FALSE(frustum.intersects_sphere({.center = {0.0f, 0.0f, -1001.0f}, .radius = 0.1f}));
}

TEST(ViewVisibility, TracksVisibleToHiddenTransition) {
    ::epix::app::App app = ::epix::app::App::create();
    ::epix::camera::CameraPlugin{}.attach(app);
    const Entity entity = app.world_mut().spawn(epix::camera::ViewVisibility::hidden()).id();
    app.world_mut().get_entity_mut(entity).transform([](EntityWorldMut&& world_entity) -> int {
        world_entity.get_mut<epix::camera::ViewVisibility>().value().get_mut().set_visible();
        return 0;
    });

    app.update();
    const auto visibility = app.world().entity(entity).get<epix::camera::ViewVisibility>();
    ASSERT_TRUE(visibility.has_value());
    EXPECT_FALSE(visibility->get().get());
}

// World-side VisibleEntities is per-visibility-class (Bevy bevy_camera
// visibility__mod.rs:279-316): type-id-keyed lists with get/get_mut/iter/
// len/is_empty/clear/clear_all.
TEST(VisibleEntities, PerClassAccessors) {
    ::epix::camera::VisibleEntities visible;
    const auto class_a = epix::meta::type_index(epix::meta::type_id<int>());
    const auto class_b = epix::meta::type_index(epix::meta::type_id<float>());

    // get for an absent class returns an empty list.
    EXPECT_TRUE(visible.get(class_a).empty());
    EXPECT_TRUE(visible.is_empty(class_a));
    EXPECT_EQ(visible.len(class_a), 0u);

    visible.push(epix::ecs::Entity::from_index(42), class_a);
    ASSERT_EQ(visible.len(class_a), 1u);
    EXPECT_EQ(visible.get(class_a).front(), epix::ecs::Entity::from_index(42));
    visible.clear(class_a);

    // get_mut inserts an empty class and exposes the mutable owned list, as
    // Bevy's `&mut Vec<Entity>` API does.
    EXPECT_TRUE(visible.get_mut(class_a).empty());
    visible.push(epix::ecs::Entity::from_index(1), class_a);
    visible.push(epix::ecs::Entity::from_index(2), class_a);
    visible.push(epix::ecs::Entity::from_index(3), class_b);
    EXPECT_EQ(visible.len(class_a), 2u);
    EXPECT_EQ(visible.len(class_b), 1u);
    EXPECT_FALSE(visible.is_empty(class_a));
    EXPECT_EQ(visible.iter(class_a).size(), 2u);
    EXPECT_EQ(visible.iter(class_a)[0], epix::ecs::Entity::from_index(1));

    // clear removes one class's entities; clear_all empties every list.
    visible.clear(class_a);
    EXPECT_EQ(visible.len(class_a), 0u);
    EXPECT_EQ(visible.len(class_b), 1u);
    visible.clear_all();
    EXPECT_TRUE(visible.is_empty(class_a));
    EXPECT_TRUE(visible.is_empty(class_b));
}

TEST(CubemapVisibleEntities, IteratorsAreLazyViews) {
    ::epix::camera::CubemapVisibleEntities cubemap;
    static_assert(std::ranges::view<decltype(cubemap.iter())>);
    static_assert(std::ranges::view<decltype(cubemap.iter_mut())>);
    EXPECT_EQ(std::ranges::distance(cubemap.iter()), 6);
    cubemap.get_mut(2).entities.push_back(epix::ecs::Entity::from_index(9));
    EXPECT_EQ(cubemap.get(2).entities.front(), epix::ecs::Entity::from_index(9));
}

TEST(VisibilityClass, AddHookAppendsTheComponentType) {
    struct CustomRenderable {};
    epix::ecs::World world(epix::ecs::WorldId(0));
    const auto component_id = world.registrator().register_component<CustomRenderable>();
    const auto entity       = world.spawn(::epix::camera::VisibilityClass{}).id();

    ::epix::camera::add_visibility_class<CustomRenderable>(
        world, epix::ecs::HookContext{.entity = entity, .component_id = component_id});

    auto visibility_class = world.get_entity(entity)->get<::epix::camera::VisibilityClass>();
    ASSERT_TRUE(visibility_class.has_value());
    ASSERT_EQ(visibility_class->get().classes.size(), 1u);
    EXPECT_EQ(visibility_class->get().classes.front(), epix::meta::type_index(epix::meta::type_id<CustomRenderable>()));
}
