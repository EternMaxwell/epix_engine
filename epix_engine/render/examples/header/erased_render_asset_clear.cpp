// Visual verification for Epix's compact ErasedRenderAsset extraction. The
// source keeps editor-only CPU data; only ClearPayload crosses to the render
// world and its prepared color clears the swapchain.

#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/render/erased_render_asset.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <array>
#include <expected>
#include <optional>
#include <string>
#include <tuple>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

namespace {

struct ClearSource {
    glm::vec4 color{0.10f, 0.55f, 0.82f, 1.0f};
    std::array<float, 256> editor_only_data{};
};
struct ClearPayload {
    glm::vec4 color;
};
struct PreparedClearColor {
    glm::vec4 color;
};
struct ClearColorAdapter;

constexpr struct ClearGraphLabel {
} kClearGraph;

struct ClearPassNode : render::graph::Node {
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
        const auto view = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(context.view_entity());
        if (!view) return {};
        const auto& target = std::get<1>(*view);

        // The example deliberately stays black until ErasedRenderAsset has
        // extracted its compact payload and prepared this resource.
        glm::vec4 color{};
        if (auto colors = world.get_resource<render::erased_render_asset::ErasedRenderAssets<PreparedClearColor>>()) {
            for (const auto& [_, prepared] : colors->get().iter()) {
                color = prepared.color;
                break;
            }
        }
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

struct ClearGraphPlugin {
    void attach(App& app) {
        if (auto render_app = app.get_sub_app_mut(render::Render)) {
            render::graph::RenderGraph graph;
            constexpr struct ClearPassLabel {
            } kClearPass;
            graph.add_node(render::graph::NodeLabel(kClearPass), ClearPassNode{});
            render_app->get().world_mut().resource_mut<render::graph::RenderGraph>().add_sub_graph(
                render::graph::GraphLabel(kClearGraph), std::move(graph));
        }
    }
};

}  // namespace

template <>
struct epix::render::erased_render_asset::ErasedRenderAsset<ClearColorAdapter> {
    using SourceAsset    = ClearSource;
    using ExtractedAsset = ClearPayload;
    using ErasedAsset    = PreparedClearColor;
    using ExtractError   = std::string;
    using Param          = std::tuple<>;

    RenderAssetUsages asset_usage(const SourceAsset&) const {
        return static_cast<RenderAssetUsages>(RenderAssetUsages::MAIN_WORLD | RenderAssetUsages::RENDER_WORLD);
    }
    std::expected<ExtractedAsset, ExtractError> extract(const SourceAsset& source,
                                                         assets::AssetId<SourceAsset>,
                                                         RenderAssetExtractionReason,
                                                         const ErasedAsset*) const {
        return ExtractedAsset{source.color};
    }
    std::expected<ErasedAsset, PrepareAssetError<ExtractedAsset>> prepare_asset(
        ExtractedAsset&& payload, const assets::AssetId<SourceAsset>&, Param&, const ErasedAsset*) const {
        return ErasedAsset{payload.color};
    }
};

int main() {
    App app = App::create();
    window::Window primary_window;
    primary_window.title = "Erased Render Asset Compact Extraction";
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
        .add_plugins(render::erased_render_asset::ErasedRenderAssetPlugin<ClearColorAdapter>{})
        .add_plugins(ClearGraphPlugin{});
    assets::app_register_asset<ClearSource>(app);

    app.add_systems(Startup, into([](ResMut<assets::Assets<ClearSource>> colors, Commands commands) {
                        commands.insert_resource(colors->emplace(ClearSource{}));
                        commands.spawn(::epix::camera::Camera{}, ::epix::camera::Projection{},
                                       render::camera::CameraRenderGraph(kClearGraph), transform::Transform{});
                    }));
    app.run();
}
