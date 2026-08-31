// Visual verification for AsBindGroupShaderType. The render-graph node uses
// a custom conversion that sees only processed render-world images: it stays
// magenta until the source image reaches RenderAssets<Image>, then clears the
// actual swapchain cyan.

#include <array>
#include <cstdint>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <expected>
#include <optional>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

namespace {

struct ImageAssetClearInput {
    glm::vec4 missing_image_color{0.45f, 0.05f, 0.35f, 1.0f};
};

struct ImageAssetClearShader {
    glm::vec4 color;
};

constexpr struct ImageAssetClearGraphLabel {
} kImageAssetClearGraph;

}  // namespace

template <>
struct epix::render::render_resource::ShaderTypeInfo<ImageAssetClearShader>
    : epix::render::render_resource::RawShaderType<ImageAssetClearShader> {};

template <>
struct epix::render::render_resource::AsBindGroupShaderType<ImageAssetClearInput, ImageAssetClearShader> {
    static ImageAssetClearShader as_bind_group_shader_type(
        const ImageAssetClearInput& input,
        const epix::render::RenderAssets<epix::image::Image>& images) {
        for (const auto& [_, image] : images.iter()) {
            if (image.had_data) return {.color = glm::vec4(0.05f, 0.72f, 0.88f, 1.0f)};
        }
        return {.color = input.missing_image_color};
    }
};

namespace {

struct ImageAssetClearPassNode : render::graph::Node {
    std::optional<QueryState<Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>>> views;

    void update(World& world) override {
        if (!views) {
            views = world.try_query<Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>>();
        } else {
            views->update_archetypes(world);
        }
    }

    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& context,
                                                         render::graph::RenderContext& render_context,
                                                         const World& world) override {
        if (!views) return {};
        const auto view =
            views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(context.view_entity());
        if (!view) return {};
        const auto& target = std::get<1>(*view);

        const auto images = world.get_resource<render::RenderAssets<image::Image>>();
        const glm::vec4 color = images
                                    ? render::render_resource::as_bind_group_shader_type<ImageAssetClearShader>(
                                          ImageAssetClearInput{}, images->get())
                                          .color
                                    : ImageAssetClearInput{}.missing_image_color;

        const bool first_write = !target.needs_present();
        target.output_attachment.mark_as_cleared();
        auto output_view = target.out_texture().clone();
        render_context.add_command_buffer_generation_task(
            [first_write, color, output_view = std::move(output_view)](wgpu::Device device) mutable {
                wgpu::RenderPassColorAttachment attachment;
                attachment.setView(output_view)
                    .setDepthSlice(~0u)
                    .setLoadOp(first_write ? wgpu::LoadOp::eClear : wgpu::LoadOp::eLoad)
                    .setStoreOp(wgpu::StoreOp::eStore)
                    .setClearValue(wgpu::Color(color.r, color.g, color.b, color.a));
                auto encoder = device.createCommandEncoder();
                auto pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor().setColorAttachments(std::array{attachment}));
                pass.end();
                return encoder.finish();
            });
        return {};
    }
};

struct ImageAssetClearGraphPlugin {
    void attach(App& app) {
        if (auto render_app = app.get_sub_app_mut(render::Render)) {
            render::graph::RenderGraph graph;
            constexpr struct ImageAssetClearPassLabel {
            } kImageAssetClearPass;
            graph.add_node(render::graph::NodeLabel(kImageAssetClearPass), ImageAssetClearPassNode{});
            render_app->get().world_mut().resource_mut<render::graph::RenderGraph>().add_sub_graph(
                render::graph::GraphLabel(kImageAssetClearGraph), std::move(graph));
        }
    }
};

struct ImageAssetInputPlugin {
    void ready(App& app) {
        std::array<std::uint8_t, 4> pixels{20, 180, 220, 255};
        auto image_asset = image::Image::create2d(1, 1, image::Format::RGBA8, pixels).value();
        app.world_mut().resource_mut<assets::Assets<image::Image>>().emplace(std::move(image_asset));
    }
};

}  // namespace

int main() {
    App app = App::create();
    window::Window primary_window;
    primary_window.title = "AsBindGroupShaderType Image Assets";
    primary_window.size  = {1280, 720};

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{.primary_window = primary_window,
                                           .exit_condition = window::ExitCondition::OnPrimaryClosed})
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(ImageAssetClearGraphPlugin{})
        .add_plugins(ImageAssetInputPlugin{});

    app.add_systems(Startup, into([](Commands commands) {
                        commands.spawn(::epix::camera::Camera{}, ::epix::camera::Projection{},
                                       render::camera::CameraRenderGraph(kImageAssetClearGraph), transform::Transform{});
                    }));
    app.run();
}
