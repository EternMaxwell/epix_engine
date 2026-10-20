#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <exception>
#include <optional>
#include <thread>

TEST(RenderShaderLibraries, EveryAppRetainsItsLibrariesWithoutConsumerHandles) {
    namespace assets = epix::assets;
    namespace render = epix::render;
    epix::task::IoTaskPool::get_or_init(epix::task::TaskPoolBuilder{}.num_threads(4).build());
    constexpr std::array paths{"embedded://epix/shaders/maths.slang", "embedded://epix/shaders/view.wgsl",
                               "embedded://epix/shaders/view.slang", "embedded://epix/shaders/globals.slang"};
    for (int generation = 0; generation < 3; ++generation) {
        SCOPED_TRACE(generation);
        auto app = epix::app::App::create();
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{}, epix::camera::CameraPlugin{}, assets::AssetPlugin{},
                        epix::image::ImagePlugin{}, epix::mesh::MeshPlugin{}, epix::transform::TransformPlugin{},
                        epix::shader::ShaderPlugin{}, render::FrameCountPlugin{});
        try {
            app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
        } catch (const std::exception& error) {
            GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
        }
        app.resource_scope([&](epix::app::internal::Plugins& plugins) { plugins.ready_all(app); });
        const auto& server = app.resource<assets::AssetServer>();
        std::array<std::optional<assets::UntypedAssetId>, paths.size()> ids;
        for (std::size_t index = 0; index < paths.size(); ++index) {
            // Query weak identities only. Loading/retaining a test-owned handle
            // here would hide the plugin's missing lifetime ownership.
            ids[index] = server.get_path_id(assets::AssetPath{paths[index]});
            EXPECT_TRUE(ids[index]) << paths[index];
        }
        const auto pump = [&] {
            app.run_schedule(epix::app::PreUpdate);
            app.run_schedule(epix::app::PostUpdate);
            app.run_schedule(epix::app::Last);
            std::this_thread::yield();
        };
        const auto loaded = [&] {
            return std::ranges::all_of(ids,
                                       [&](const auto& id) { return !id || server.is_loaded_with_dependencies(*id); });
        };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!loaded() && std::chrono::steady_clock::now() < deadline) pump();
        // Exercise unused-asset tracking after load too, not only a transient
        // Loaded state before an unowned shader is removed.
        for (int frame = 0; frame < 64; ++frame) pump();
        const auto& shaders = app.resource<assets::Assets<epix::shader::Shader>>();
        for (std::size_t index = 0; index < paths.size(); ++index) {
            SCOPED_TRACE(paths[index]);
            if (!ids[index]) continue;
            EXPECT_TRUE(server.is_loaded_with_dependencies(*ids[index]));
            EXPECT_TRUE(shaders.get(ids[index]->typed<epix::shader::Shader>()));
        }
    }
}
