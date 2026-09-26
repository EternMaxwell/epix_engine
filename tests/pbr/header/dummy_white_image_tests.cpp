#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/pbr/dummy_white_image.hpp>
#include <epix/render.hpp>
#include <epix/render/assets.hpp>
#include <epix/render/fallback_image.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <exception>
#include <optional>
#include <utility>

using namespace epix::app;
using namespace epix::assets;

namespace pbr    = epix::pbr;
namespace image  = epix::image;
namespace render = epix::render;

namespace {

// Attaching RenderPlugin loads an embedded shader, which spawns an async load
// task that terminates the process when the IO task pool has not been
// initialized.
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} g_io_task_pool_init;

/** @brief The plugin set `RenderPlugin` needs before it can acquire a device. */
void add_render_prerequisites(App& app) {
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::view::ViewPlugin{})
        .add_plugins(render::GlobalsPlugin{});
}

}  // namespace

/** @brief A material's optional texture binds the uploaded image's own view and
 *  sampler, and the 1x1 white fallback's when the handle is absent (Bevy
 *  `get_image_texture`, `bevy_pbr/src/render/mesh.rs:1964-1978`).
 *
 *  The three cases are the ones Bevy distinguishes: no handle, a handle whose
 *  image has been uploaded, and a handle whose image has not — the last of which
 *  fails rather than silently binding the fallback. */
TEST(DummyWhiteImage, GetImageTextureFallsBackAndLooksUp) {
    render::RenderAssets<render::texture::GpuImage> gpu_images;

    render::texture::GpuImage dummy;
    const auto uploaded_id  = AssetId<image::Image>::invalid();
    const auto missing_id   = AssetId<image::Image>(uuids::uuid::from_string("6ba7b810-9dad-11d1-80b4-00c04fd430c8").value());
    gpu_images.emplace(uploaded_id, render::texture::GpuImage{});
    const auto* uploaded = gpu_images.get(uploaded_id);
    ASSERT_NE(uploaded, nullptr);
    EXPECT_NE(uploaded, &dummy);

    // No handle: the fallback image's own view and sampler.
    const auto fallback = pbr::get_image_texture(dummy, gpu_images, std::nullopt);
    ASSERT_TRUE(fallback.has_value());
    EXPECT_EQ(fallback->first, &dummy.texture_view);
    EXPECT_EQ(fallback->second, &dummy.sampler);

    // A handle whose image is uploaded: that image, not the fallback.
    const auto looked_up = pbr::get_image_texture(dummy, gpu_images, Handle<image::Image>{uploaded_id});
    ASSERT_TRUE(looked_up.has_value());
    EXPECT_EQ(looked_up->first, &uploaded->texture_view);
    EXPECT_EQ(looked_up->second, &uploaded->sampler);
    EXPECT_NE(looked_up->first, &dummy.texture_view);

    // A handle whose image has not been uploaded: no view or sampler, rather
    // than the fallback - an absent handle is fine, a pending upload is not.
    const auto pending = pbr::get_image_texture(dummy, gpu_images, Handle<image::Image>{missing_id});
    EXPECT_FALSE(pending.has_value());
}

/** @brief The builder creates the 1x1 white texture a material binds in place of
 *  an optional texture (Bevy `build_dummy_white_gpu_image`,
 *  `bevy_pbr/src/render/mesh.rs:1923-1962`): the default image's descriptor, its
 *  sampler when it carries one, and the pixel data written through the queue.
 *
 *  Skips without a device, like the other GPU tests. */
TEST(DummyWhiteImage, BuildDummyWhiteGpuImageCreatesTheDefaultImage) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(app);
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    // The device, queue and default sampler live in the render world: the render
    // module's image plugin creates the sampler there during attachment.
    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world = render_app->get().world_mut();

    // The image the builder is derived from, so the assertions follow the source
    // rather than restating literals.
    const image::Image source{};
    const auto built = pbr::build_dummy_white_gpu_image(world.resource<wgpu::Device>(),
                                                        world.resource<render::DefaultImageSampler>().sampler,
                                                        world.resource<wgpu::Queue>());

    EXPECT_EQ(built.size.width, source.texture_descriptor.size.width);
    EXPECT_EQ(built.size.height, source.texture_descriptor.size.height);
    EXPECT_EQ(built.size.depthOrArrayLayers, source.texture_descriptor.size.depthOrArrayLayers);
    EXPECT_EQ(built.texture_format, source.texture_descriptor.format);
    EXPECT_EQ(built.mip_level_count, source.texture_descriptor.mipLevelCount);
    EXPECT_EQ(built.texture_view_format, source.texture_view_descriptor ? source.texture_view_descriptor->format
                                                                       : std::optional<wgpu::TextureFormat>{});
    // The pixels were written, so the texture is not an uninitialised one.
    EXPECT_TRUE(built.had_data);
    // Bevy's default image is the 1x1 opaque white one.
    EXPECT_EQ(built.size.width, 1u);
    EXPECT_EQ(built.size.height, 1u);
    EXPECT_EQ(source.raw_view().size(), 4u);
    EXPECT_EQ(source.raw_view()[0], std::byte{0xff});
    EXPECT_EQ(source.raw_view()[1], std::byte{0xff});
    EXPECT_EQ(source.raw_view()[2], std::byte{0xff});
    EXPECT_EQ(source.raw_view()[3], std::byte{0xff});
}