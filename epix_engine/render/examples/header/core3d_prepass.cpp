// A dedicated visual test for the C14 prepass and C15 deferred subsystems, which
// the gtests cannot reach: no test in `core_graph/tests/header` constructs a
// render pass, so a wrong attachment format, sample count, clear/load op, or a
// missing view texture would pass every test and only fail here.
//
// This example spawns one `Camera3d` carrying all four prepass components
// (`DepthPrepass`, `NormalPrepass`, `MotionVectorPrepass`, `DeferredPrepass`) so
// the real extraction prepares all four binned phases and the real nodes --
// `EarlyPrepassNode`/`LatePrepassNode` and
// `EarlyDeferredGBufferPrepassNode`/`LateDeferredGBufferPrepassNode` -- execute
// their production bodies on the GPU against the real attachments. Because a
// single `DeferredPrepass` also makes `Core3dPlugin`'s `check_msaa` force
// `Msaa::Off`, the prepass textures are single-sampled and can be bound and
// displayed.
//
// A display node inserted between `StartMainPassPostProcessing` and
// `Tonemapping` then shows what the real subsystem actually wrote:
//
//   top-left      depth texture (the view depth copied into the prepass depth)
//   top-right     normals texture
//   bottom-left   motion-vector texture, in R/G
//   bottom-right  left half: deferred G-buffer raw bytes
//                 right half: deferred lighting-pass id
//
// A magenta screen means `ViewPrepassTextures` never reached the view, which is
// exactly what happens if the render-entity prepass markers are not propagated.
//
// The quadrant colours are driven by the animated `Camera3dDepthLoadOp::Clear`
// value, so distinct per-frame captures prove the textures are read back live
// rather than being a one-off.
//
// Nothing queues geometry into the prepass or deferred bins yet -- that producer
// is `MaterialPlugin` in the `pbr` module -- so the passes run with empty bins
// and the tiles show the declared clear values. That is deliberate: the point of
// this example is the pass/attachment contract, and it holds regardless of
// whether anything is drawn. The inline WGSL is example scaffolding, the same
// accepted direct-resource layer `binned_phase_geometry` uses.

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <memory>
#include <optional>
#include <string_view>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

namespace {

/** @brief The display pipeline, rebuilt whenever the target format or sample
 * count changes. */
struct PrepassDisplayPipeline {
    wgpu::RenderPipeline pipeline;
    wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;
    std::uint32_t sample_count = 0;
    /** @brief 0 = no prepass textures, 1 = prepass only, 2 = prepass + deferred. */
    int mode = -1;
};

constexpr struct PrepassDisplayLabel {
} kPrepassDisplayLabel;

/** @brief One-shot probe state, so the log is itself evidence for the parts the
 * capture cannot show. */
struct PrepassProbe {
    int frames  = 0;
    bool logged = false;
};

/** @brief Which prepass node should own the pass; see `spawn_camera`. */
struct PrepassMode {
    bool deferred = true;
};

constexpr std::string_view kDisplayShader = R"(
@group(0) @binding(0) var prepass_depth: texture_depth_2d;
@group(0) @binding(1) var normals: texture_2d<f32>;
@group(0) @binding(2) var motion_vectors: texture_2d<f32>;
@group(0) @binding(3) var deferred_gbuffer: texture_2d<u32>;
@group(0) @binding(4) var deferred_lighting_id: texture_2d<u32>;

@vertex
fn vertexMain(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
    // Fullscreen triangle covering the whole target.
    let x = f32((index << 1u) & 2u);
    let y = f32(index & 2u);
    return vec4<f32>(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
}

@fragment
fn fragmentMain(@builtin(position) position: vec4<f32>) -> @location(0) vec4<f32> {
    let size = vec2<f32>(textureDimensions(normals));
    let half = size * 0.5;
    let coords = vec2<i32>(position.xy);
    if (position.x < half.x && position.y < half.y) {
        // Top-left: the reverse-Z depth the prepass node copied into the
        // prepass depth texture.
        return vec4<f32>(vec3<f32>(textureLoad(prepass_depth, coords, 0)), 1.0);
    }
    if (position.y < half.y) {
        // Top-right: normals.
        return vec4<f32>(textureLoad(normals, coords, 0).rgb, 1.0);
    }
    if (position.x < half.x) {
        // Bottom-left: motion vectors in R/G with a fixed blue so a black
        // (cleared) tile is still distinguishable from "node did not run".
        let mv = textureLoad(motion_vectors, coords, 0);
        return vec4<f32>(mv.r, mv.g, 0.25, 1.0);
    }
    if (position.x < half.x + half.x * 0.5) {
        // Bottom-right, left half: raw deferred G-buffer bytes.
        let g = textureLoad(deferred_gbuffer, coords, 0);
        return vec4<f32>(f32(g.r) / 255.0, f32(g.g) / 255.0, f32(g.b) / 255.0, 1.0);
    }
    // Bottom-right, right half: the deferred lighting-pass id as greyscale.
    let id = textureLoad(deferred_lighting_id, coords, 0);
    return vec4<f32>(vec3<f32>(f32(id.r) / 255.0), 1.0);
}

@fragment
fn fragmentMainPrepassOnly(@builtin(position) position: vec4<f32>) -> @location(0) vec4<f32> {
    // Without `DeferredPrepass` there is no G-buffer and no lighting-pass id, so
    // the bottom-right quadrant is a fixed dark grey marker instead. The depth
    // tile is still the decisive one.
    let size = vec2<f32>(textureDimensions(normals));
    let half = size * 0.5;
    let coords = vec2<i32>(position.xy);
    if (position.x < half.x && position.y < half.y) {
        return vec4<f32>(vec3<f32>(textureLoad(prepass_depth, coords, 0)), 1.0);
    }
    if (position.y < half.y) {
        return vec4<f32>(textureLoad(normals, coords, 0).rgb, 1.0);
    }
    if (position.x < half.x) {
        let mv = textureLoad(motion_vectors, coords, 0);
        return vec4<f32>(mv.r, mv.g, 0.25, 1.0);
    }
    return vec4<f32>(0.10, 0.10, 0.10, 1.0);
}
)";

constexpr std::string_view kMissingShader = R"(
@vertex
fn vertexMain(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
    let x = f32((index << 1u) & 2u);
    let y = f32(index & 2u);
    return vec4<f32>(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
}

@fragment
fn fragmentMain() -> @location(0) vec4<f32> {
    // Unambiguous "the prepass subsystem produced no view textures".
    return vec4<f32>(1.0, 0.0, 1.0, 1.0);
}
)";

/** @brief Draws the four prepass/deferred textures as quadrants, or a solid
 * magenta screen when the view has no prepass textures at all.
 *
 * The query deliberately treats `ViewPrepassTextures` as optional for exactly
 * that reason. */
struct PrepassDisplayNode {
    using ViewQuery =
        ecs::Item<const render::camera::ExtractedCamera&,
                  const render::view::ViewTarget&,
                  ecs::Opt<const core_graph::core_3d::ViewPrepassTextures&>,
                  ecs::Opt<const render::view::Msaa&>>;

    std::shared_ptr<PrepassDisplayPipeline> display = std::make_shared<PrepassDisplayPipeline>();
    wgpu::ShaderModule display_shader;
    wgpu::ShaderModule missing_shader;
    wgpu::BindGroupLayout display_layout;
    wgpu::BindGroupLayout prepass_only_layout;
    wgpu::BindGroupLayout missing_layout;

    void update(World& world) {
        auto device = world.get_resource<wgpu::Device>();
        if (!device) throw std::runtime_error("render device is unavailable");

        display_shader =
            device->get().createShaderModule(wgpu::ShaderModuleDescriptor()
                                                 .setLabel("core3d-prepass-display-shader")
                                                 .setNextInChain(wgpu::ShaderSourceWGSL().setCode(kDisplayShader)));
        missing_shader =
            device->get().createShaderModule(wgpu::ShaderModuleDescriptor()
                                                 .setLabel("core3d-prepass-missing-shader")
                                                 .setNextInChain(wgpu::ShaderSourceWGSL().setCode(kMissingShader)));
        if (!display_shader || !missing_shader) {
            throw std::runtime_error("failed to create the core3d prepass display shader modules");
        }

        // The depth tile is sampled as a depth texture; the normals tile is
        // `Rgb10a2Unorm`, which is not filterable, so it must be declared as
        // unfilterable-float. The two deferred tiles are integer formats and can
        // only be read with `textureLoad`, hence `Uint`.
        const auto all_entries = render::render_resource::BindGroupLayoutEntries<5>::sequential(
            wgpu::ShaderStage::eFragment,
            render::render_resource::binding_types::texture_depth_2d(),
            render::render_resource::binding_types::texture_2d(wgpu::TextureSampleType::eUnfilterableFloat),
            render::render_resource::binding_types::texture_2d(wgpu::TextureSampleType::eFloat),
            render::render_resource::binding_types::texture_2d(wgpu::TextureSampleType::eUint),
            render::render_resource::binding_types::texture_2d(wgpu::TextureSampleType::eUint));
        const auto prepass_only_entries = render::render_resource::BindGroupLayoutEntries<3>::sequential(
            wgpu::ShaderStage::eFragment,
            render::render_resource::binding_types::texture_depth_2d(),
            render::render_resource::binding_types::texture_2d(wgpu::TextureSampleType::eUnfilterableFloat),
            render::render_resource::binding_types::texture_2d(wgpu::TextureSampleType::eFloat));
        display_layout = device->get().createBindGroupLayout(wgpu::BindGroupLayoutDescriptor()
                                                                 .setLabel("core3d-prepass-display-layout")
                                                                 .setEntries(all_entries.entries()));
        prepass_only_layout = device->get().createBindGroupLayout(wgpu::BindGroupLayoutDescriptor()
                                                                     .setLabel("core3d-prepass-only-layout")
                                                                     .setEntries(prepass_only_entries.entries()));
        missing_layout = device->get().createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor().setLabel("core3d-prepass-missing-layout").setEntries(
                std::array<wgpu::BindGroupLayoutEntry, 0>{}));
    }

    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext&,
                                                         render::graph::RenderContext& render_context,
                                                         typename ecs::QueryData<ViewQuery>::Item view,
                                                         const World& world) const {
        if (!display_shader || !missing_shader) return {};
        auto&& [camera, target, prepass, msaa] = view;
        static_cast<void>(camera);
        static_cast<void>(msaa);

        const auto format       = target.main_texture_format();
        const auto sample_count = target.color_attachment_sample_count();
        const auto attachment   = target.get_color_attachment();

        // Bevy exposes accessors for the four textures; the lighting-pass id is
        // read through its field, as Bevy's own deferred node does.
        const auto lighting_id_view = [&prepass]() -> std::optional<wgpu::TextureView> {
            if (!prepass || !prepass->get().deferred_lighting_pass_id) return std::nullopt;
            return prepass->get().deferred_lighting_pass_id->texture.default_view;
        };
        const auto depth_view    = prepass ? prepass->get().depth_view() : std::nullopt;
        const auto normal_view   = prepass ? prepass->get().normal_view() : std::nullopt;
        const auto motion_view   = prepass ? prepass->get().motion_vectors_view() : std::nullopt;
        const auto gbuffer_view  = prepass ? prepass->get().deferred_view() : std::nullopt;
        const auto lighting_view = lighting_id_view();

        // The depth/normals/motion-vector trio is what both prepass paths
        // produce; the G-buffer pair only exists for a deferred view.
        const bool has_base     = depth_view && normal_view && motion_view;
        const bool has_deferred = has_base && gbuffer_view && lighting_view;
        const bool missing      = !has_base;

        auto device = world.get_resource<wgpu::Device>();
        if (!device) return {};

        const auto& layout = missing ? missing_layout : (has_deferred ? display_layout : prepass_only_layout);
        if (!display->pipeline || display->format != format || display->sample_count != sample_count ||
            display->mode != (has_deferred ? 2 : (missing ? 0 : 1))) {
            const auto pipeline_layout =
                device->get().createPipelineLayout(wgpu::PipelineLayoutDescriptor()
                                                       .setLabel("core3d-prepass-display-pipeline-layout")
                                                       .setBindGroupLayouts(std::array{layout}));
            const char* fragment_entry = has_deferred ? "fragmentMain" : "fragmentMainPrepassOnly";
            display->pipeline          = device->get().createRenderPipeline(
                wgpu::RenderPipelineDescriptor()
                    .setLabel("core3d-prepass-display-pipeline")
                    .setLayout(pipeline_layout)
                    .setVertex(wgpu::VertexState()
                                   .setModule(missing ? missing_shader : display_shader)
                                   .setEntryPoint("vertexMain"))
                    .setFragment(wgpu::FragmentState()
                                     .setModule(missing ? missing_shader : display_shader)
                                     .setEntryPoint(missing ? "fragmentMain" : fragment_entry)
                                     .setTargets(std::array{wgpu::ColorTargetState()
                                                                .setFormat(format)
                                                                .setWriteMask(wgpu::ColorWriteMask::eAll)}))
                    .setPrimitive(wgpu::PrimitiveState()
                                      .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                                      .setFrontFace(wgpu::FrontFace::eCCW)
                                      .setCullMode(wgpu::CullMode::eNone))
                    .setMultisample(wgpu::MultisampleState().setCount(sample_count).setMask(~0u)));
            display->format       = format;
            display->sample_count = sample_count;
            display->mode         = has_deferred ? 2 : (missing ? 0 : 1);
        }
        if (!display->pipeline) return {};

        wgpu::BindGroup bind_group;
        if (missing) {
            bind_group = device->get().createBindGroup(wgpu::BindGroupDescriptor()
                                                          .setLabel("core3d-prepass-missing-bind-group")
                                                          .setLayout(missing_layout)
                                                          .setEntries(std::array<wgpu::BindGroupEntry, 0>{}));
        } else if (has_deferred) {
            bind_group = device->get().createBindGroup(
                wgpu::BindGroupDescriptor()
                    .setLabel("core3d-prepass-display-bind-group")
                    .setLayout(display_layout)
                    .setEntries(std::array{
                        wgpu::BindGroupEntry().setBinding(0).setTextureView(*depth_view),
                        wgpu::BindGroupEntry().setBinding(1).setTextureView(*normal_view),
                        wgpu::BindGroupEntry().setBinding(2).setTextureView(*motion_view),
                        wgpu::BindGroupEntry().setBinding(3).setTextureView(*gbuffer_view),
                        wgpu::BindGroupEntry().setBinding(4).setTextureView(*lighting_view),
                    }));
        } else {
            bind_group = device->get().createBindGroup(
                wgpu::BindGroupDescriptor()
                    .setLabel("core3d-prepass-only-bind-group")
                    .setLayout(prepass_only_layout)
                    .setEntries(std::array{
                        wgpu::BindGroupEntry().setBinding(0).setTextureView(*depth_view),
                        wgpu::BindGroupEntry().setBinding(1).setTextureView(*normal_view),
                        wgpu::BindGroupEntry().setBinding(2).setTextureView(*motion_view),
                    }));
        }

        const auto pipeline = display->pipeline;
        const auto color    = [attachment] {
            auto value = attachment;
            value.setLoadOp(wgpu::LoadOp::eLoad);
            value.setStoreOp(wgpu::StoreOp::eStore);
            return value;
        }();

        render_context.add_command_buffer_generation_task(
            [pipeline, bind_group, color, has_bindings = !missing](wgpu::Device device) {
                auto encoder = device.createCommandEncoder(
                    wgpu::CommandEncoderDescriptor().setLabel("core3d_prepass_display_encoder"));
                {
                    auto pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor()
                                                            .setLabel("core3d prepass display")
                                                            .setColorAttachments(std::array{color}));
                    pass.setPipeline(pipeline);
                    if (has_bindings) pass.setBindGroup(0, bind_group, std::span<const std::uint32_t>{});
                    pass.draw(3, 1, 0, 0);
                    pass.end();
                }
                return encoder.finish();
            });
        return {};
    }
};

/** @brief Animate the view's reverse-Z depth clear so consecutive captures read
 * back different values from the real prepass depth texture. */
void animate_depth_clear(Query<Item<Mut<::epix::camera::Camera3d>>, With<::epix::camera::Camera3d>> cameras,
                         Res<time::Time<>> time) {
    const float t = time->elapsed_secs();
    for (auto&& [camera_3d] : cameras.iter()) {
        camera_3d.get_mut().depth_load_op =
            ::epix::camera::Camera3dDepthLoadOp(::epix::camera::Camera3dDepthLoadOp::Clear{0.35f + 0.3f * std::sin(t)});
    }
}

/** @brief Log the prepass/deferred state once, a few frames in. Everything here
 * is invisible in a screenshot, so the process log is the evidence. */
void probe_prepass(
    ResMut<PrepassProbe> probe,
    Res<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::Opaque3dPrepass>> opaque_prepass,
    Res<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::AlphaMask3dPrepass>> alpha_mask_prepass,
    Res<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::Opaque3dDeferred>> opaque_deferred,
    Res<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::AlphaMask3dDeferred>> alpha_mask_deferred,
    Query<Item<const render::view::ExtractedView&,
               const render::view::Msaa&,
               const ::epix::camera::Camera3d&,
               Has<core_graph::core_3d::DepthPrepass>,
               Has<core_graph::core_3d::NormalPrepass>,
               Has<core_graph::core_3d::MotionVectorPrepass>,
               Has<core_graph::core_3d::DeferredPrepass>,
               Opt<const core_graph::core_3d::ViewPrepassTextures&>,
               Opt<const render::view::ViewDepthTexture&>>> views) {
    if (probe->logged) return;
    probe->frames += 1;
    if (probe->frames < 4) return;

    for (auto&& [extracted_view, msaa, camera_3d, depth_prepass, normal_prepass, motion_vectors, deferred_prepass,
                 textures, view_depth] : views.iter()) {
        probe->logged        = true;
        const auto& retained = extracted_view.retained_view_entity;
        // Bevy's extract_camera_prepass_phase inserts these markers onto the
        // synced render entity; without that, no view texture is ever prepared.
        spdlog::info("[core3d_prepass] render-entity markers: depth={} normal={} motion_vectors={} deferred={}",
                     depth_prepass, normal_prepass, motion_vectors, deferred_prepass);
        spdlog::info("[core3d_prepass] msaa samples={} (check_msaa must have forced 1 for the deferred view)",
                     render::view::samples(msaa));
        // The depth tile reads the prepass depth texture, which is a copy of the
        // view depth texture, whose clear value is the animated load op. Logging
        // the extracted value tells apart "the animation never reached the
        // render world" from "the texture content is wrong".
        if (const auto* clear = std::get_if<::epix::camera::Camera3dDepthLoadOp::Clear>(&camera_3d.depth_load_op)) {
            spdlog::info("[core3d_prepass] extracted Camera3d depth load op = Clear({})", clear->value);
        } else {
            spdlog::info("[core3d_prepass] extracted Camera3d depth load op = Load");
        }
        if (view_depth) {
            const auto has_clear = view_depth->get().attachment.clear_value.has_value();
            spdlog::info("[core3d_prepass] ViewDepthTexture clear_value={}",
                         has_clear ? std::to_string(*view_depth->get().attachment.clear_value) : std::string("none"));
        } else {
            spdlog::warn("[core3d_prepass] the view has NO ViewDepthTexture");
        }
        spdlog::info("[core3d_prepass] phases: opaque_prepass={} alpha_mask_prepass={} opaque_deferred={} "
                     "alpha_mask_deferred={}",
                     opaque_prepass->phases.contains(retained), alpha_mask_prepass->phases.contains(retained),
                     opaque_deferred->phases.contains(retained), alpha_mask_deferred->phases.contains(retained));
        if (!textures) {
            spdlog::error("[core3d_prepass] the view has NO ViewPrepassTextures; the display node draws magenta.");
            return;
        }
        const auto describe = [](std::string_view name, const auto& texture) {
            if (!texture) {
                spdlog::warn("[core3d_prepass] {}: absent", name);
                return;
            }
            const auto& value = texture->texture.texture;
            spdlog::info("[core3d_prepass] {}: {}x{} format={} samples={} usage={}", name, value.getWidth(),
                         value.getHeight(), static_cast<int>(value.getFormat()), value.getSampleCount(),
                         static_cast<int>(value.getUsage()));
        };
        spdlog::info("[core3d_prepass] prepass textures {}x{}", textures->get().size.width,
                     textures->get().size.height);
        describe("depth", textures->get().depth);
        describe("normal", textures->get().normal);
        describe("motion_vectors", textures->get().motion_vectors);
        describe("deferred", textures->get().deferred);
        describe("deferred_lighting_pass_id", textures->get().deferred_lighting_pass_id);
        return;
    }
    spdlog::warn("[core3d_prepass] no extracted Camera3d view after {} frames.", probe->frames);
}

/** @brief Spawn the 3D camera that drives the prepass and deferred subsystems.
 *
 * `deferred` selects which of the two prepass nodes owns the pass: with
 * `DeferredPrepass` the deferred G-buffer node runs (and the plain prepass node
 * bails out, exactly as Bevy does), without it the plain prepass node runs. Both
 * nodes perform their own view-depth -> prepass-depth copy, so running the
 * example both ways verifies both copies. */
void spawn_camera(Commands cmd, Res<PrepassMode> mode) {
    if (mode->deferred) {
        cmd.spawn(::epix::camera::Camera3d{}, transform::Transform{}, core_graph::core_3d::DepthPrepass{},
                  core_graph::core_3d::NormalPrepass{}, core_graph::core_3d::MotionVectorPrepass{},
                  core_graph::core_3d::DeferredPrepass{});
        return;
    }
    // No `DeferredPrepass`, so `check_msaa` leaves MSAA alone; the prepass
    // textures must still be single-sampled to be bound, so ask for `Off`.
    cmd.spawn(::epix::camera::Camera3d{}, transform::Transform{}, core_graph::core_3d::DepthPrepass{},
              core_graph::core_3d::NormalPrepass{}, core_graph::core_3d::MotionVectorPrepass{},
              render::view::Msaa::Off);
}

/** @brief Insert the display node into the real Core3D sub-graph, immediately
 * before `Tonemapping`, so the normal post-processing chain presents it. */
struct PrepassDisplayPlugin {
    void attach(App& app) {
        auto render_app = app.get_sub_app_mut(render::Render);
        if (!render_app) return;
        auto& render_world = render_app->get().world_mut();

        if (auto render_graph = render_world.get_resource_mut<render::graph::RenderGraph>()) {
            if (auto core3d = render_graph->get().get_sub_graph(core_graph::core_3d::Core3d)) {
                core3d->get().add_node(kPrepassDisplayLabel,
                                       render::graph::ViewNodeRunner{PrepassDisplayNode{}, render_world});
                core3d->get().add_node_edges(core_graph::core_3d::Core3dNodes::StartMainPassPostProcessing,
                                             kPrepassDisplayLabel, core_graph::core_3d::Core3dNodes::Tonemapping);
            }
        }
        render_world.init_resource<PrepassProbe>();
        render_app->get().add_systems(render::Render, into(probe_prepass).set_name("probe prepass"));
    }
};

}  // namespace

int main(int argc, char** argv) {
    App app = App::create();

    // `core3d_prepass no-deferred` runs the plain prepass node instead of the
    // deferred G-buffer node, so both depth copies can be verified from this one
    // example. The default (no argument) is the deferred path.
    const bool deferred = !(argc > 1 && std::string_view(argv[1]) == "no-deferred");

    window::Window primary_window;
    primary_window.title = deferred ? "Core3D Prepass + Deferred (all four prepass components)"
                                    : "Core3D Prepass only (no DeferredPrepass)";
    primary_window.size  = {1280, 720};

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
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(mesh::MeshPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(PrepassDisplayPlugin{});

    app.add_systems(Startup, into(spawn_camera).set_name("spawn 3d prepass camera"));
    app.add_systems(Update, into(animate_depth_clear).set_name("animate depth clear"));

    app.world_mut().insert_resource(PrepassMode{.deferred = deferred});
    app.run();
}
