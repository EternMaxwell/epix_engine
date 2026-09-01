#include <epix/core_graph.hpp>

#include <array>
#include <optional>
#include <unordered_set>
#include <utility>

using namespace epix::app;
using namespace epix::ecs;

namespace epix::core_graph {
namespace {
wgpu::BlendState alpha_blend_state() noexcept {
    return wgpu::BlendState()
        .setColor(wgpu::BlendComponent()
                      .setOperation(wgpu::BlendOperation::eAdd)
                      .setSrcFactor(wgpu::BlendFactor::eSrcAlpha)
                      .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha))
        .setAlpha(wgpu::BlendComponent()
                      .setOperation(wgpu::BlendOperation::eAdd)
                      .setSrcFactor(wgpu::BlendFactor::eOne)
                      .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha));
}

std::optional<wgpu::BlendState> output_blend_for(const render::camera::ExtractedCamera* camera,
                                                  const wgpu::TextureView& output_texture,
                                                  std::unordered_set<wgpu::TextureView>& output_textures) {
    // Mirror Bevy's prepare_view_upscaling_pipelines exactly: this tracks the
    // output views encountered by this query run, rather than using camera
    // sort indices. A skipped view must not count as an output write.
    if (!camera) {
        output_textures.insert(output_texture);
        return std::nullopt;
    }
    const auto* write = std::get_if<camera::CameraOutputMode::Write>(&camera->output_mode);
    if (!write) return std::nullopt;

    const bool already_seen = output_textures.contains(output_texture);
    output_textures.insert(output_texture);
    if (write->blend_state) return write->blend_state;
    return already_seen ? std::optional{alpha_blend_state()} : std::nullopt;
}

std::optional<glm::vec4> output_clear_color(const render::camera::ExtractedCamera* camera,
                                            const ecs::World& world) {
    if (!camera) {
        if (auto clear = world.get_resource<camera::ClearColor>()) return clear->get().to_vec4();
        return std::nullopt;
    }
    const auto* write = std::get_if<camera::CameraOutputMode::Write>(&camera->output_mode);
    if (!write) return std::nullopt;
    if (const auto* custom = std::get_if<camera::ClearColorConfig::Custom>(&write->clear_color)) {
        return custom->color.to_vec4();
    }
    if (!std::holds_alternative<camera::ClearColorConfig::None>(write->clear_color)) {
        if (auto clear = world.get_resource<camera::ClearColor>()) return clear->get().to_vec4();
    }
    return std::nullopt;
}

void prepare_view_upscaling_pipelines(
    Commands commands,
    ResMut<render::PipelineServer> pipeline_server,
    ResMut<render::SpecializedRenderPipelines<BlitPipeline>> pipelines,
    Res<BlitPipeline> blit_pipeline,
    Query<Item<Entity, const render::view::ViewTarget&, Opt<const render::camera::ExtractedCamera&>>> view_targets) {
    std::unordered_set<wgpu::TextureView> output_textures;
    for (auto&& [entity, target, opt_camera] : view_targets.iter()) {
        const auto* camera = opt_camera ? &opt_camera->get() : nullptr;
        if (!target.out_texture()) continue;

        const auto pipeline_id = pipelines->specialize(
            *pipeline_server, *blit_pipeline,
            BlitPipelineKey{.texture_format = target.out_texture_view_format(),
                            .blend_state     = output_blend_for(camera, target.out_texture(), output_textures),
                            .samples         = 1});
        pipeline_server->block_on_render_pipeline(pipeline_id);
        commands.entity(entity).insert(ViewUpscalingPipeline{pipeline_id});
    }
}
}  // namespace

std::expected<void, render::graph::NodeRunError> UpscalingNode::run(
    render::graph::GraphContext&,
    render::graph::RenderContext& render_context,
    typename ecs::QueryData<ViewQuery>::Item view,
    const ecs::World& world) const {
    auto&& [target, upscaling_pipeline, opt_camera] = view;
    const auto* camera = opt_camera ? &opt_camera->get() : nullptr;
    if (camera && std::holds_alternative<camera::CameraOutputMode::Skip>(camera->output_mode)) return {};
    if (!target.out_texture()) return {};

    const auto pipeline_server = world.get_resource<render::PipelineServer>();
    const auto blit_pipeline   = world.get_resource<BlitPipeline>();
    if (!pipeline_server || !blit_pipeline) return {};
    const auto pipeline = pipeline_server->get().get_render_pipeline(upscaling_pipeline.pipeline_id);
    if (!pipeline) return {};

    const auto& source_texture = target.main_texture_view();
    wgpu::BindGroup bind_group;
    {
        const std::lock_guard lock(cached_bind_group->mutex);
        if (!cached_bind_group->value || !(cached_bind_group->value->first == source_texture)) {
            cached_bind_group->value =
                std::pair{source_texture, blit_pipeline->get().create_bind_group(render_context.device(), source_texture)};
        }
        bind_group = cached_bind_group->value->second;
    }

    const auto color_attachment = target.out_texture_color_attachment(output_clear_color(camera, world));
    const auto render_pipeline  = pipeline->get().pipeline();
    const auto viewport         = camera ? camera->viewport : std::optional<camera::Viewport>{};
    render_context.add_command_buffer_generation_task(
        [color_attachment, render_pipeline, bind_group, viewport](wgpu::Device device) {
            auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("upscaling"));
            auto pass = encoder.beginRenderPass(
                wgpu::RenderPassDescriptor().setLabel("upscaling").setColorAttachments(std::array{color_attachment}));
            if (viewport) {
                pass.setScissorRect(viewport->physical_position.x, viewport->physical_position.y,
                                    viewport->physical_size.x, viewport->physical_size.y);
            }
            pass.setPipeline(render_pipeline);
            pass.setBindGroup(0, bind_group, std::span<const std::uint32_t>{});
            pass.draw(3, 1, 0, 0);
            pass.end();
            return encoder.finish();
        });
    return {};
}

void UpscalingPlugin::attach(App& app) {
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().add_systems(
            render::Render,
            into(prepare_view_upscaling_pipelines).in_set(render::RenderSystems::Prepare).set_name(
                "prepare view upscaling pipelines"));
    }
}

}  // namespace epix::core_graph
