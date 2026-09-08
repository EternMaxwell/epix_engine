// Direct tonemapping-pipeline visual comparison: one HDR Core2D camera renders every
// production tonemapping pipeline into a 4x2 grid. Every tile samples the same
// source, which continuously cycles between several real HDR environments.
// This makes bypasses and method-specific
// regressions visible in one rendered frame without depending on a scene
// renderer such as sprite or mesh.

#include <spdlog/spdlog.h>

#include <array>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <optional>
#include <string>
#include <string_view>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

namespace {

// The source HDRIs are 2:1 equirectangular images. A 4x2 grid in a 4:1 window
// gives every result tile the same 2:1 aspect ratio without distortion.
constexpr glm::uvec2 kWindowSize{1280, 320};
constexpr glm::uvec2 kTileSize{kWindowSize.x / 4, kWindowSize.y / 2};
constexpr float kBackgroundDurationSeconds = 5.0f;

constexpr std::string_view kGridDescription =
    "top left->right: None, Reinhard, ReinhardLuminance, AcesFitted; "
    "bottom left->right: AgX, SomewhatBoringDisplayTransform, TonyMcMapface, BlenderFilmic";

constexpr std::string_view kAlgorithmTitle =
    "top: None | Reinhard | ReinhardLuminance | AcesFitted || "
    "bottom: AgX | SomewhatBoringDisplayTransform | TonyMcMapface | BlenderFilmic";

struct TonemappingComparisonCamera {};

struct BackgroundSource {
    std::string_view name;
    std::string_view path;
};

constexpr std::array kBackgroundSources{
    BackgroundSource{"Studio Small 01 (Poly Haven CC0)", "examples/tonemapping/studio_small_01_1k.hdr"},
    BackgroundSource{"Lythwood Lounge (Poly Haven CC0)", "examples/tonemapping/lythwood_lounge_1k.hdr"},
    BackgroundSource{"Venice Sunset (Poly Haven CC0)", "examples/tonemapping/venice_sunset_1k.hdr"},
};

struct BackgroundCycle {
    std::array<assets::Handle<image::Image>, kBackgroundSources.size()> textures;
    std::size_t active = 0;
};

struct ExtractedBackground {
    std::optional<assets::Handle<image::Image>> texture;
};

}  // namespace

template <>
struct epix::render::ExtractComponent<BackgroundCycle> {
    using QueryData   = const BackgroundCycle&;
    using QueryFilter = epix::ecs::With<epix::camera::Camera>;
    using Out         = ExtractedBackground;

    static std::optional<Out> extract_component(QueryData source) {
        return Out{source.textures[source.active]};
    }
};

namespace {

struct Tile {
    core_graph::Tonemapping method;
    glm::uvec2 position;
};

constexpr std::array kTiles{
    Tile{core_graph::Tonemapping::None, {0 * kTileSize.x, 0 * kTileSize.y}},
    Tile{core_graph::Tonemapping::Reinhard, {1 * kTileSize.x, 0 * kTileSize.y}},
    Tile{core_graph::Tonemapping::ReinhardLuminance, {2 * kTileSize.x, 0 * kTileSize.y}},
    Tile{core_graph::Tonemapping::AcesFitted, {3 * kTileSize.x, 0 * kTileSize.y}},
    Tile{core_graph::Tonemapping::AgX, {0 * kTileSize.x, 1 * kTileSize.y}},
    Tile{core_graph::Tonemapping::SomewhatBoringDisplayTransform, {1 * kTileSize.x, 1 * kTileSize.y}},
    Tile{core_graph::Tonemapping::TonyMcMapface, {2 * kTileSize.x, 1 * kTileSize.y}},
    Tile{core_graph::Tonemapping::BlenderFilmic, {3 * kTileSize.x, 1 * kTileSize.y}},
};

struct TonemappingComparisonPipelines {
    std::array<render::CachedPipelineId, kTiles.size()> ids;
};

constexpr struct TonemappingComparisonNodeLabel {
} kTonemappingComparisonNode;

struct TonemappingComparisonNode {
    using ViewQuery = Item<const render::view::ViewUniformOffset&,
                           const render::view::ViewTarget&,
                           const ExtractedBackground&>;

    void update(World&) {}

    std::expected<void, render::graph::NodeRunError> run(
        render::graph::GraphContext&,
        render::graph::RenderContext& render_context,
        typename QueryData<ViewQuery>::Item view,
        const World& world) const {
        const auto [view_uniform_offset, target, background] = view;
        if (!target.is_hdr()) return {};

        const auto& pipeline_server = world.resource<render::PipelineServer>();
        const auto& pipeline_ids    = world.resource<TonemappingComparisonPipelines>();
        std::array<wgpu::RenderPipeline, kTiles.size()> render_pipelines;
        for (std::size_t index = 0; index < kTiles.size(); ++index) {
            const auto pipeline = pipeline_server.get_render_pipeline(pipeline_ids.ids[index]);
            if (!pipeline) return {};
            render_pipelines[index] = pipeline->get().pipeline();
        }

        const auto& tonemapping_pipeline = world.resource<core_graph::TonemappingPipeline>();
        const auto& tonemapping_luts     = world.resource<core_graph::TonemappingLuts>();
        const auto& gpu_images           = world.resource<render::RenderAssets<image::Image>>();
        const auto& fallback_image       = world.resource<render::texture::FallbackImage>();
        const auto& view_uniforms        = world.resource<render::view::ViewUniforms>();
        const auto* uniform_buffer       = view_uniforms.uniforms.buffer();
        if (!uniform_buffer) return {};

        if (!background.texture) return {};
        const auto* source_image = gpu_images.get(background.texture->id());
        if (!source_image) return {};
        const auto post_process = target.post_process_write();
        const auto& source = source_image->texture_view;

        std::array<wgpu::BindGroup, kTiles.size()> bind_groups;
        for (std::size_t index = 0; index < kTiles.size(); ++index) {
            const auto [lut_view, lut_sampler] = core_graph::get_lut_bindings(
                gpu_images, tonemapping_luts, kTiles[index].method, fallback_image);
            bind_groups[index] = tonemapping_pipeline.create_bind_group(
                render_context.device(), *uniform_buffer, source, lut_view, lut_sampler);
        }

        auto attachment = wgpu::RenderPassColorAttachment()
                              .setView(post_process.destination)
                              .setDepthSlice(~0u)
                              .setLoadOp(wgpu::LoadOp::eClear)
                              .setStoreOp(wgpu::StoreOp::eStore)
                              .setClearValue(wgpu::Color(0.0, 0.0, 0.0, 1.0));
        auto pass = render_context.command_encoder().beginRenderPass(
            wgpu::RenderPassDescriptor()
                .setLabel("tonemapping comparison grid")
                .setColorAttachments(std::array{attachment}));
        for (std::size_t index = 0; index < kTiles.size(); ++index) {
            const auto& tile = kTiles[index];
            pass.setViewport(static_cast<float>(tile.position.x), static_cast<float>(tile.position.y),
                             static_cast<float>(kTileSize.x), static_cast<float>(kTileSize.y), 0.0f, 1.0f);
            pass.setScissorRect(tile.position.x, tile.position.y, kTileSize.x, kTileSize.y);
            pass.setPipeline(render_pipelines[index]);
            pass.setBindGroup(0, bind_groups[index], view_uniform_offset.offset);
            pass.draw(3, 1, 0, 0);
        }
        pass.end();
        return {};
    }
};

std::string window_title(std::string_view background_name) {
    return std::string("Tonemapping HDR Sources | background: ") + std::string(background_name) + " | " +
           std::string(kAlgorithmTitle);
}

void log_mapping(std::string_view background_name) {
    spdlog::info("Tonemapping background: {}; {}", background_name, kGridDescription);
}

void setup(Commands commands, Res<assets::AssetServer> asset_server) {
    BackgroundCycle backgrounds{
        .textures = {
            asset_server->load<image::Image>(kBackgroundSources[0].path),
            asset_server->load<image::Image>(kBackgroundSources[1].path),
            asset_server->load<image::Image>(kBackgroundSources[2].path),
        },
    };

    log_mapping(kBackgroundSources.front().name);

    commands.spawn(TonemappingComparisonCamera{}, camera::Camera2d{}, camera::Camera{}, render::view::Hdr{},
                   core_graph::Tonemapping::None, core_graph::DebandDither::Disabled,
                   std::move(backgrounds), transform::Transform{});
}

void update_shared_source(
    Res<time::Time<>> time,
    Query<Item<BackgroundCycle&>, With<TonemappingComparisonCamera>> cameras,
    Query<Item<window::Window&>, With<window::PrimaryWindow>> primary_windows) {
    for (auto&& [backgrounds] : cameras.iter()) {
        const auto active = static_cast<std::size_t>(time->elapsed_secs() / kBackgroundDurationSeconds) %
                            kBackgroundSources.size();
        if (active != backgrounds.active) {
            backgrounds.active = active;
            for (auto&& [window] : primary_windows.iter()) {
                window.title = window_title(kBackgroundSources[active].name);
            }
            log_mapping(kBackgroundSources[active].name);
        }
    }
}

void prepare_comparison_pipelines(
    ResMut<TonemappingComparisonPipelines> comparison_pipelines,
    Res<render::PipelineServer> pipeline_server,
    Res<core_graph::TonemappingPipeline> tonemapping_pipeline,
    ResMut<render::SpecializedRenderPipelines<core_graph::TonemappingPipeline>> specialized_pipelines) {
    for (std::size_t index = 0; index < kTiles.size(); ++index) {
        comparison_pipelines->ids[index] = specialized_pipelines->specialize(
            *pipeline_server, *tonemapping_pipeline,
            core_graph::TonemappingPipelineKey{
                .deband_dither = core_graph::DebandDither::Disabled,
                .tonemapping   = kTiles[index].method,
                .flags         = core_graph::TonemappingPipelineKeyFlags::None,
            });
    }
}

struct TonemappingComparisonPlugin {
    void attach(App& app) {
        app.add_plugins(render::ExtractComponentPlugin<BackgroundCycle>{});
        if (auto render_app = app.get_sub_app_mut(render::Render)) {
            auto& render_world = render_app->get().world_mut();
            TonemappingComparisonPipelines pipelines;
            pipelines.ids.fill(render::INVALID_RENDER_PIPELINE_ID);
            render_world.insert_resource(std::move(pipelines));

            if (auto render_graph = render_world.get_resource_mut<render::graph::RenderGraph>()) {
                if (auto core2d = render_graph->get().get_sub_graph(core_graph::core_2d::Core2d)) {
                    core2d->get().add_node(kTonemappingComparisonNode,
                                           render::graph::ViewNodeRunner{TonemappingComparisonNode{}, render_world});
                    core2d->get().add_node_edges(core_graph::core_2d::Core2dNodes::StartMainPassPostProcessing,
                                                 kTonemappingComparisonNode,
                                                 core_graph::core_2d::Core2dNodes::Tonemapping);
                }
            }

            render_app->get().add_systems(
                render::Render,
                into(prepare_comparison_pipelines)
                    .in_set(render::RenderSystems::Prepare)
                    .set_name("prepare tonemapping comparison pipelines"));
        }
    }
};

}  // namespace

int main() {
    App app = App::create();

    window::Window primary_window;
    primary_window.title = window_title(kBackgroundSources.front().name);
    primary_window.size  = {static_cast<int>(kWindowSize.x), static_cast<int>(kWindowSize.y)};

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{
            .primary_window = primary_window,
            .exit_condition = window::ExitCondition::OnPrimaryClosed,
        })
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(TonemappingComparisonPlugin{});

    app.add_systems(Startup, into(setup).set_name("set up tonemapping comparison"));
    app.add_systems(Update, into(update_shared_source).set_name("cycle shared HDR tonemapping source"));

    app.run();
}
