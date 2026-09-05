#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <concepts>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/core_graph/core2d.hpp>
#include <epix/core_graph/fullscreen.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::core_graph {

/**
 * @brief Trait concept for a full-screen triangle material (Bevy
 * `FullscreenMaterial`).
 *
 * A material is a render component whose value is uploaded once per entity
 * into a dynamic uniform buffer, drives a pair of HDR / non-HDR full-screen
 * pipelines, and renders through a typed view node. The `ExtractComponent<T>`
 * specialization supplies the extraction; this concept supplies the shader
 * asset, the graph-registration parameters, and the uniform buffer layout.
 */
template <typename T>
concept FullscreenMaterial = render::render_resource::ShaderWritable<T> && std::is_copy_constructible_v<T> &&
                             std::is_copy_assignable_v<T> && requires {
    { T::fragment_shader() } -> std::convertible_to<std::string_view>;
    { T::fragment_shader_source() } -> std::convertible_to<std::string_view>;
    { T::node_edges() } -> std::convertible_to<std::vector<render::graph::NodeLabel>>;
};

/**
 * @brief Dedicated label identifying a full-screen material's graph node (Bevy
 * `FullscreenMaterialLabel`). Bevy keeps this distinct from node labels; Epix
 * models it as a `NodeLabel` subtype so the render graph accepts it directly,
 * with the same "derived from the material type" identity.
 */
EPIX_EXPORT struct FullscreenMaterialLabel : public render::graph::NodeLabel {
    using render::graph::NodeLabel::NodeLabel;
};

/** @brief Default `sub_graph()`: none (the camera's graph is auto-detected). */
template <FullscreenMaterial T>
std::optional<render::graph::GraphLabel> fullscreen_material_sub_graph() {
    if constexpr (requires { T::sub_graph(); }) {
        return T::sub_graph();
    } else {
        return std::nullopt;
    }
}

/** @brief Default `node_label()`: a `FullscreenMaterialLabel` derived from the
 * material type (Bevy `FullscreenMaterialLabel(type_name::<Self>())`). */
template <FullscreenMaterial T>
FullscreenMaterialLabel fullscreen_material_node_label() {
    if constexpr (requires { T::node_label(); }) {
        return T::node_label();
    } else {
        return FullscreenMaterialLabel{render::graph::NodeLabel::from_type<T>()};
    }
}

/** @brief Per-fragment-shader full-screen material pipeline pair (Bevy
 * `FullscreenMaterialPipeline`). Retains the bind-group layout and sampler plus
 * the LDR and HDR cached render pipelines. */
EPIX_EXPORT struct FullscreenMaterialPipeline {
    wgpu::BindGroupLayout layout;
    wgpu::Sampler sampler;
    render::CachedPipelineId pipeline_id;
    render::CachedPipelineId pipeline_id_hdr;
};

/** @brief Typed full-screen triangle render node (Bevy
 * `FullscreenMaterialNode`). Runs the material over the whole view target. */
template <FullscreenMaterial T>
struct FullscreenMaterialNode {
    using ViewQuery = ecs::Item<const render::view::ViewTarget&, const render::DynamicUniformIndex<T>&>;

    void update(ecs::World&) {}
    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& graph,
                                                         render::graph::RenderContext& render_context,
                                                         typename ecs::QueryData<ViewQuery>::Item view,
                                                         const ecs::World& world) const;
};

/** @brief The render node that runs `FullscreenMaterialNode<T>` for one view. */
template <FullscreenMaterial T>
using FullscreenMaterialNodeRunner = render::graph::ViewNodeRunner<FullscreenMaterialNode<T>>;

/** @brief Installs component extraction plus uniform buffering, queues the
 * paired HDR/non-HDR pipelines, and registers the typed view node (Bevy
 * `FullscreenMaterialPlugin`). */
template <FullscreenMaterial T>
struct FullscreenMaterialPlugin {
    void attach(app::App& app);
};

/**
 * @brief Extract-stage system that registers the material's view node into the
 * camera's graph when a material of type `T` is first added (Bevy
 * `extract_on_add`, fullscreen_material.rs:101-139). With no Core3D graph in
 * Epix, a `Camera2d` material lands in the `Core2d` sub-graph.
 */
template <FullscreenMaterial T>
void extract_on_add(
    epix::ecs::World& render_world,
    epix::app::Extract<epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, epix::ecs::Has<::epix::camera::Camera2d>>,
                                        ::epix::ecs::Added<T>>> cameras) {
    for (auto&& [entity, has_2d] : cameras.iter()) {
        if (!has_2d) continue;
        (void)render_world.resource_scope([&](render::graph::RenderGraph& render_graph, epix::ecs::World& world) {
            if (auto sub_graph = render_graph.get_sub_graph(::epix::core_graph::core_2d::Core2d)) {
                auto& graph = sub_graph->get();
                graph.add_node(fullscreen_material_node_label<T>(), FullscreenMaterialNodeRunner<T>{
                                                                        FullscreenMaterialNode<T>{}, world});
                const auto edges = T::node_edges();
                for (std::size_t index = 0; index + 1 < edges.size(); ++index) {
                    const auto res = graph.try_add_node_edge(edges[index], edges[index + 1]);
                    if (res) continue;
                    if (const auto* edge_error = std::get_if<render::graph::EdgeError>(&res.error());
                        edge_error && std::holds_alternative<render::graph::EdgeAlreadyExists>(*edge_error)) {
                        continue;
                    }
                    throw std::runtime_error("Failed to add FullscreenMaterial graph edge");
                }
            }
        });
    }
}

template <FullscreenMaterial T>
std::expected<void, render::graph::NodeRunError> FullscreenMaterialNode<T>::run(
    render::graph::GraphContext&,
    render::graph::RenderContext& render_context,
    typename ecs::QueryData<ViewQuery>::Item view,
    const ecs::World& world) const {
    // Bevy FullscreenMaterialNode::run (fullscreen_material.rs:273-328): reads
    // the material's uniforms, selects the HDR or non-HDR pipeline, then draws
    // the full-screen triangle, offsetting the dynamic uniform by the entity's
    // index.
    const auto [target, settings_index] = view;

    const auto pipeline_resource = world.get_resource<FullscreenMaterialPipeline>();
    const auto pipeline_server    = world.get_resource<render::PipelineServer>();
    const auto data_uniforms      = world.get_resource<render::ComponentUniforms<T>>();
    if (!pipeline_resource || !pipeline_server || !data_uniforms) return {};

    const auto pipeline_id = target.is_hdr() ? pipeline_resource->get().pipeline_id_hdr : pipeline_resource->get().pipeline_id;
    const auto pipeline    = pipeline_server->get().get_render_pipeline(pipeline_id);
    if (!pipeline) return {};

    const auto settings_binding = data_uniforms->get().uniforms().binding();
    if (!settings_binding) return {};

    const auto post_process = target.post_process_write();
    const wgpu::BindGroup bind_group = render_context.device().createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("fullscreen_material_bind_group")
            .setLayout(pipeline_resource->get().layout)
            .setEntries(render::render_resource::BindGroupEntries<>::sequential(
                            render::render_resource::BindGroupEntries<>::texture_binding(post_process.source),
                            render::render_resource::BindGroupEntries<>::sampler_binding(pipeline_resource->get().sampler),
                            render::render_resource::BindGroupEntries<>::buffer_binding(
                                settings_binding->get(), 0, render::render_resource::ShaderTypeInfo<T>::shader_size))
                            .entries()));

    const wgpu::RenderPipeline render_pipeline = pipeline->get().pipeline();
    const wgpu::TextureView destination       = post_process.destination;
    const std::uint32_t offset                = settings_index.index();

    render_context.add_command_buffer_generation_task(
        [render_pipeline, bind_group, destination, offset](wgpu::Device device) {
            auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("fullscreen_material"));
            auto color   = wgpu::RenderPassColorAttachment()
                              .setView(destination)
                              .setDepthSlice(~0u)
                              .setLoadOp(wgpu::LoadOp::eClear)
                              .setStoreOp(wgpu::StoreOp::eStore)
                              .setClearValue(wgpu::Color(0.0, 0.0, 0.0, 0.0));
            auto pass = encoder.beginRenderPass(
                wgpu::RenderPassDescriptor()
                    .setLabel("fullscreen_material")
                    .setColorAttachments(std::array{color}));
            pass.setPipeline(render_pipeline);
            pass.setBindGroup(0, bind_group, offset);
            pass.draw(3, 1, 0, 0);
            pass.end();
            return encoder.finish();
        });
    return {};
}

template <FullscreenMaterial T>
void FullscreenMaterialPlugin<T>::attach(app::App& app) {
    // Bevy FullscreenMaterialPlugin::build (fullscreen_material.rs:53-99):
    // component extraction + uniform buffering, then the render-world pipeline
    // and typed view node.
    const auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
    const auto server   = app.world_mut().get_resource<assets::AssetServer>();
    if (!registry || !server) return;
    // Bevy embedded assets: the material's fragment_shader() is the
    // source-relative path; the loader adds the "embedded://" source prefix.
    // Register via the relative path (EmbeddedAssetRegistry) and load via the
    // fully-qualified path, exactly like the tonemapping plugin.
    const auto source = T::fragment_shader_source();
    registry->get().insert_asset_static(
        T::fragment_shader(), std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()),
                                                         source.size()));
    const auto fragment_shader =
        server->get().load<shader::Shader>(std::string("embedded://") + std::string(T::fragment_shader()));

    app.add_plugins(render::ExtractComponentPlugin<T>{}, render::UniformComponentPlugin<T>{});

    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) return;
    auto& render_app_ref = render_app->get();
    render_app_ref.add_systems(
        render::RenderStartup,
        ecs::into([fragment_shader](ecs::Commands commands, ecs::Res<wgpu::Device> device,
                                    ecs::Res<render::PipelineServer> pipeline_server,
                                    ecs::Res<FullscreenShader> fullscreen_shader) {
            using render::render_resource::BindGroupLayoutEntries;
            using namespace render::render_resource::binding_types;
            const auto entries = BindGroupLayoutEntries<>::with_indices(
                wgpu::ShaderStage::eFragment,
                std::pair{0u, texture_2d(wgpu::TextureSampleType::eFloat)},
                std::pair{1u, sampler(wgpu::SamplerBindingType::eFiltering)},
                std::pair{2u, uniform_buffer(true, render::render_resource::ShaderTypeInfo<T>::shader_size)});
            const auto layout = device->createBindGroupLayout(
                wgpu::BindGroupLayoutDescriptor()
                    .setLabel("fullscreen_material_bind_group_layout")
                    .setEntries(entries.entries()));
            const auto sampler = device->createSampler(
                wgpu::SamplerDescriptor().setLabel("fullscreen_material_sampler").setMaxAnisotropy(1));
            const auto vertex_state = fullscreen_shader->to_vertex_state();
            const auto build         = [&](wgpu::TextureFormat format) {
                render::FragmentState fragment{.shader = fragment_shader, .entry_point = std::string("fs_main")};
                wgpu::ColorTargetState target;
                target.setFormat(format).setWriteMask(wgpu::ColorWriteMask::eAll);
                fragment.add_target(std::move(target));
                render::RenderPipelineDescriptor desc{
                    .label       = "fullscreen_material_pipeline",
                    .layouts     = {layout},
                    .vertex      = vertex_state,
                    .primitive   = wgpu::PrimitiveState()
                                       .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                                       .setFrontFace(wgpu::FrontFace::eCCW)
                                       .setCullMode(wgpu::CullMode::eNone)
                                       .setUnclippedDepth(false),
                    .multisample = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
                    .fragment    = std::move(fragment),
                };
                return pipeline_server->queue_render_pipeline(std::move(desc));
            };
            // Bevy: non-HDR target uses TextureFormat::bevy_default() and HDR
            // uses ViewTarget::TEXTURE_FORMAT_HDR.
            const auto pipeline_id     = build(wgpu::TextureFormat::eRGBA8Unorm);
            const auto pipeline_id_hdr = build(render::view::ViewTarget::TEXTURE_FORMAT_HDR);
            commands.insert_resource(
                FullscreenMaterialPipeline{layout, sampler, pipeline_id, pipeline_id_hdr});
        }).set_name(std::format("init fullscreen material pipeline '{}'", meta::type_id<T>().short_name())));

    const auto sub_graph = fullscreen_material_sub_graph<T>();
    if (sub_graph) {
        // Bevy render.add_render_graph_node (render_graph/app.rs): register the
        // typed view node into the declared sub-graph. Epix uses the underlying
        // RenderGraph::add_node directly so the node type is deduced from the
        // construction rather than spelled as a nested template argument.
        auto& render_world = render_app_ref.world_mut();
        if (auto render_graph = render_world.get_resource_mut<render::graph::RenderGraph>()) {
            if (auto graph = render_graph->get().get_sub_graph(*sub_graph)) {
                graph->get().add_node(fullscreen_material_node_label<T>(),
                                      FullscreenMaterialNodeRunner<T>{FullscreenMaterialNode<T>{}, render_world});

                // Bevy add_render_graph_node_edges with duplicate-edge tolerance
                // (fullscreen_material.rs:72-92).
                const auto edges = T::node_edges();
                for (std::size_t index = 0; index + 1 < edges.size(); ++index) {
                    const auto res = graph->get().try_add_node_edge(edges[index], edges[index + 1]);
                    if (res) continue;
                    if (const auto* edge_error = std::get_if<render::graph::EdgeError>(&res.error());
                        edge_error && std::holds_alternative<render::graph::EdgeAlreadyExists>(*edge_error)) {
                        continue;
                    }
                    throw std::runtime_error("Failed to add FullscreenMaterial graph edge");
                }
            }
        }
    } else {
        // Bevy extract_on_add (fullscreen_material.rs:101-139): a material
        // attached to a camera adds its node to the camera's graph dynamically.
        render_app_ref.add_systems(render::ExtractSchedule, ecs::into(extract_on_add<T>));
    }
}

}  // namespace epix::core_graph
