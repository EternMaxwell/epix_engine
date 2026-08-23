#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/transform.hpp>
#include <epix/utils.hpp>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/camera.hpp>
#include <epix/render/color_grading.hpp>
#include <epix/render/graph.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/render_resource.hpp>
#include <epix/render/sync_world.hpp>
#include <epix/render/texture_attachment.hpp>
#include <epix/render/window.hpp>

namespace epix::render::camera {
// Camera-module types imported for render-side use (mirrors how bevy_render imports bevy_camera); the definitions live in the epix::camera module.
using ::epix::camera::Camera;
using ::epix::camera::Viewport;
using ::epix::camera::WindowRef;
using ::epix::camera::RenderTarget;
using ::epix::camera::RenderTargetId;
using ::epix::camera::RenderTargetIdHash;
using ::epix::camera::ComputedCameraValues;
using ::epix::camera::ClearColor;
using ::epix::camera::ClearColorConfig;
using ::epix::camera::Exposure;
using ::epix::camera::MainPassResolutionOverride;
using ::epix::camera::CameraOutputMode;
using ::epix::camera::CameraMainTextureUsages;
using ::epix::camera::MsaaWriteback;
using ::epix::camera::SubCameraView;
using ::epix::camera::RenderLayers;
using ::epix::camera::Visibility;
using ::epix::camera::InheritedVisibility;
using ::epix::camera::ViewVisibility;
using ::epix::camera::Projection;
using ::epix::camera::OrthographicProjection;
using ::epix::camera::PerspectiveProjection;
using ::epix::camera::ScalingMode;
using ::epix::camera::CameraProjection;
using ::epix::camera::CameraUpdateSystems;
using ::epix::camera::CameraPlugin;
using ::epix::camera::CameraProjectionPlugin;
using ::epix::camera::camera_system;

/** @brief Label identifying the render graph assigned to a camera. */
EPIX_EXPORT struct CameraRenderGraph : public graph::GraphLabel {
    using graph::GraphLabel::GraphLabel;
};
EPIX_EXPORT struct ExtractedCamera {
    // this render target is a normalized one, which means if it is a WindowRef and is primary, the entity field will
    // point to the actual primary window entity.
    RenderTarget render_target;
    glm::uvec2 viewport_size;
    glm::uvec2 target_size;
    std::optional<Viewport> viewport;
    CameraRenderGraph render_graph;
    std::ptrdiff_t order;
    std::optional<ClearColor> clear_color;
    /** @brief Whether the camera renders through an HDR intermediate texture
     * (Bevy ExtractedCamera::hdr). */
    bool hdr = false;
    /** @brief Index of this camera among cameras targeting the same render
     * target at the same order (Bevy
     * ExtractedCamera::sorted_camera_index_for_target). */
    std::optional<std::size_t> sorted_camera_index_for_target;
    /** @brief Linear exposure multiplier (Bevy ExtractedCamera::exposure). */
    float exposure = Exposure{}.exposure();
    /** @brief Final output policy (Bevy ExtractedCamera::output_mode). */
    CameraOutputMode output_mode{};
    /** @brief MSAA writeback policy (Bevy ExtractedCamera::msaa_writeback). */
    MsaaWriteback msaa_writeback = MsaaWriteback::Auto;
    /** @brief Requested usages for intermediate main textures (Bevy
     * CameraMainTextureUsages, extracted with the camera). */
    wgpu::TextureUsage main_texture_usage = wgpu::TextureUsage::eRenderAttachment |
                                             wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopySrc;
    /** @brief Which render layers this camera renders. Default: all layers. */
    RenderLayers render_layer = RenderLayers::all();
};
}  // namespace epix::render::camera
namespace epix::render::view {
// Camera-module types imported for render-side use (bevy_render imports bevy_camera).
using ::epix::camera::VisibleEntities;
using ::epix::camera::Frustum;
using ::epix::camera::Msaa;
using ::epix::camera::samples;
using ::epix::camera::msaa_from_samples;
/** @brief Forward declaration; the Hdr marker is defined below after the
 * camera extraction declarations. */
EPIX_EXPORT struct Hdr;
/** @brief Forward declaration (defined below). */
EPIX_EXPORT struct ViewTargetAttachments;
/** @brief Forward declaration (defined below; used by extract_cameras and
 * prepare_view_target). */

/**
 * @brief Stable cross-frame identifier for a render-world view (Bevy
 * RetainedViewEntity).
 */
EPIX_EXPORT struct RetainedViewEntity {
    /** @brief Main-world entity this view corresponds to. */
    sync_world::MainEntity main_entity;
    /** @brief Auxiliary entity (e.g. shadow-casting camera), or std::nullopt. */
    std::optional<sync_world::MainEntity> auxiliary_entity;
    /** @brief Subview index (0 for cameras; cascade/face index for shadow views). */
    std::uint32_t subview_index = 0;

    bool operator==(const RetainedViewEntity&) const = default;

    /** @brief Create from main entity, optional auxiliary entity, and subview
     * index (Bevy RetainedViewEntity::new, view/mod.rs:243-253). */
    static RetainedViewEntity create(sync_world::MainEntity main_entity,
                                     std::optional<sync_world::MainEntity> auxiliary_entity,
                                     std::uint32_t subview_index) {
        return RetainedViewEntity{main_entity, auxiliary_entity, subview_index};
    }
};

/** @brief Extracted view data: projection, transform, viewport and HDR
 * state for a single camera (Bevy ExtractedView).
 *
 * Field-name notes: Bevy names the projection clip_from_view and the
 * transform world_from_view; epix keeps the legacy field names projection
 * / transform for compatibility, with clip_from_view() /
 * world_from_view() accessors for the Bevy names. */
EPIX_EXPORT struct ExtractedView {
    /** @brief Stable identifier of the main-world view this render view
     * corresponds to (Bevy retained_view_entity). */
    RetainedViewEntity retained_view_entity;
    /** @brief Clip-from-view (projection) matrix (Bevy clip_from_view). */
    glm::mat4 projection;
    /** @brief World-from-view transform (Bevy world_from_view). */
    transform::GlobalTransform transform;
    /** @brief Optional pre-computed clip-from-world matrix; overrides the
     * derived value when set (Bevy clip_from_world). */
    std::optional<glm::mat4> clip_from_world;
    /** @brief Whether this view renders through an HDR intermediate texture
     * (Bevy hdr). */
    bool hdr = false;
    /** @brief Viewport as (origin.x, origin.y, width, height) (Bevy viewport). */
    glm::uvec4 viewport = glm::uvec4(0, 0, 0, 0);
    /** @brief Invert culling for mirrored views (Bevy invert_culling). */
    bool invert_culling = false;
    /** @brief Filmic grading parameters for this view (Bevy color_grading). */
    ColorGrading color_grading{};

    /** @brief Bevy name for the projection matrix. */
    const glm::mat4& clip_from_view() const noexcept { return projection; }
    /** @brief Bevy name for the world-from-view transform matrix. */
    glm::mat4 world_from_view() const noexcept { return transform.matrix; }
    /** @brief The clip-from-world matrix, either the cached one or derived. */
    glm::mat4 clip_from_world_or_derived() const noexcept {
        return clip_from_world.value_or(projection * glm::inverse(transform.matrix));
    }
    /** @brief Create a 3D rangefinder for this view (Bevy ExtractedView::rangefinder3d). */
    phase::ViewRangefinder3d rangefinder3d() const noexcept {
        return phase::ViewRangefinder3d::from_world_from_view(transform.matrix);
    }
};
/** @brief A wrapper around a texture view used as the final output color
 * attachment of a view target (Bevy `OutputColorAttachment`).
 */

EPIX_EXPORT struct OutputColorAttachment {
    /** @brief The output texture view (e.g. the swapchain view). */
    wgpu::TextureView view;
    /** @brief Format of the output texture. */
    wgpu::TextureFormat view_format = wgpu::TextureFormat::eUndefined;
    /** @brief True until the first get_attachment call of the frame. */
    std::shared_ptr<std::atomic<bool>> is_first_call;

    OutputColorAttachment() : is_first_call(std::make_shared<std::atomic<bool>>(true)) {}

    /** @brief Create from a view + format (Bevy OutputColorAttachment::new). */
    static OutputColorAttachment create(wgpu::TextureView view, wgpu::TextureFormat view_format) {
        OutputColorAttachment attachment;
        attachment.view         = std::move(view);
        attachment.view_format  = view_format;
        attachment.is_first_call = std::make_shared<std::atomic<bool>>(true);
        return attachment;
    }
    /** @brief The attachment; clears with the given color on first call
     * (Bevy OutputColorAttachment::get_attachment). */
    wgpu::RenderPassColorAttachment get_attachment(std::optional<glm::vec4> clear_color) const {
        const bool first_call = is_first_call->exchange(false, std::memory_order_seq_cst);
        wgpu::RenderPassColorAttachment attachment;
        attachment.setView(view)
            .setDepthSlice(~0u)
            .setLoadOp(first_call && clear_color ? wgpu::LoadOp::eClear : wgpu::LoadOp::eLoad)
            .setStoreOp(wgpu::StoreOp::eStore);
        if (first_call && clear_color) {
            attachment.setClearValue(wgpu::Color(clear_color->r, clear_color->g, clear_color->b, clear_color->a));
        }
        return attachment;
    }
    /** @brief True once a render pass has written to the output (Bevy
     * OutputColorAttachment::needs_present). */
    bool needs_present() const noexcept { return !is_first_call->load(std::memory_order_seq_cst); }
    /** @brief Mark the output as written (Bevy mark_as_cleared). */
    void mark_as_cleared() const noexcept { is_first_call->store(false, std::memory_order_seq_cst); }
};

/** @brief The double-buffered main textures of a view target plus the shared
 * A/B toggle (Bevy MainTargetTextures). The attachment type is the
 * render_resource::ColorAttachment (Bevy ColorAttachment). */
EPIX_EXPORT struct MainTargetTextures {
    /** @brief Main texture A. */
    render_resource::ColorAttachment a;
    /** @brief Main texture B. */
    render_resource::ColorAttachment b;
    /** @brief Shared toggle: 0 -> a, 1 -> b (Bevy Arc<AtomicUsize>). */
    std::shared_ptr<std::atomic<std::uint32_t>> main_texture;

    MainTargetTextures() : main_texture(std::make_shared<std::atomic<std::uint32_t>>(0)) {}
};

/**
 * @brief Component holding the render target for a camera view: the
 * double-buffered main textures, their format and the final output
 * attachment (Bevy 0.18 `ViewTarget`).
 *
 * The legacy `texture_view`/`format` fields alias the CURRENT main texture
 * view and its format, so existing nodes that bind `target.texture_view`
 * render into the main texture; the camera driver presents the output.
 */
EPIX_EXPORT struct ViewTarget {
    /** @brief Double-buffered main textures (Bevy main_textures). */
    MainTargetTextures main_textures;
    /** @brief Format of the main textures (Bevy main_texture_format). */
    wgpu::TextureFormat main_texture_format = wgpu::TextureFormat::eUndefined;
    /** @brief Shared A/B toggle (Bevy main_texture: Arc<AtomicUsize>). */
    std::shared_ptr<std::atomic<std::uint32_t>> main_texture;
    /** @brief Final output attachment (Bevy out_texture). */
    OutputColorAttachment out_texture;

    /** @brief Create a view target with a fresh A/B toggle and empty output. */
    ViewTarget() : main_texture(std::make_shared<std::atomic<std::uint32_t>>(0)) {}

    /** @brief Legacy alias: texture view of the current main texture (epix
     * extension so existing render nodes keep binding `target.texture_view`). */
    wgpu::TextureView texture_view;
    /** @brief Legacy alias: format of the current main texture (epix
     * extension; equals `main_texture_format`). */
    wgpu::TextureFormat format;

    /** @brief The color attachment of the current main texture (Bevy
     * get_color_attachment). */
    wgpu::RenderPassColorAttachment get_color_attachment() const {
        return current_index() == 0 ? main_textures.a.get_attachment() : main_textures.b.get_attachment();
    }
    /** @brief Sample count of the current render-pass color attachment. This
     * is the multisampled attachment when MSAA is enabled, not the
     * single-sample resolve texture exposed by main_texture_view(). */
    std::uint32_t color_attachment_sample_count() const {
        const auto& attachment = current_index() == 0 ? main_textures.a : main_textures.b;
        return attachment.resolve_target ? attachment.resolve_target->texture.getSampleCount()
                                         : attachment.texture.texture.getSampleCount();
    }
    /** @brief The unsampled attachment of the current main texture. */
    wgpu::RenderPassColorAttachment get_unsampled_color_attachment() const {
        return current_index() == 0 ? main_textures.a.get_unsampled_attachment() : main_textures.b.get_unsampled_attachment();
    }
    /** @brief The current main texture view (Bevy main_texture_view). */
    const wgpu::TextureView& main_texture_view() const {
        return current_index() == 0 ? main_textures.a.texture.default_view : main_textures.b.texture.default_view;
    }
    /** @brief The other (non-current) main texture view (Bevy
     * main_texture_other_view). */
    const wgpu::TextureView& main_texture_other_view() const {
        return current_index() == 0 ? main_textures.b.texture.default_view : main_textures.a.texture.default_view;
    }
    /** @brief The current main texture (Bevy main_texture; renamed because
     * C++ cannot share the name with the A/B toggle field). */
    const wgpu::Texture& current_main_texture() const {
        return current_index() == 0 ? main_textures.a.texture.texture : main_textures.b.texture.texture;
    }
    /** @brief Whether the main texture is HDR (Bevy is_hdr). */
    bool is_hdr() const noexcept { return main_texture_format == wgpu::TextureFormat::eRGBA16Float; }
    /** @brief Whether the output needs to be presented (Bevy needs_present). */
    bool needs_present() const noexcept { return out_texture.needs_present(); }

    /** @brief Flip the A/B toggle, returning the source view/texture that the
     * caller must copy to the returned destination (Bevy
     * post_process_write). The destination is marked as cleared. */
    struct PostProcessWrite {
        wgpu::TextureView source;
        wgpu::Texture source_texture;
        wgpu::TextureView destination;
        wgpu::Texture destination_texture;
    };
    PostProcessWrite post_process_write() const {
        // Mutates only the shared A/B toggle and the attachments' first-call
        // flags (both atomic), so it is safe on a const view (Bevy takes
        // &self).
        const std::uint32_t old_is_a = main_texture->fetch_xor(1, std::memory_order_seq_cst);
        if (old_is_a == 0) {
            main_textures.b.mark_as_cleared();
            return PostProcessWrite{main_textures.a.texture.default_view, main_textures.a.texture.texture,
                                    main_textures.b.texture.default_view, main_textures.b.texture.texture};
        }
        main_textures.a.mark_as_cleared();
        return PostProcessWrite{main_textures.b.texture.default_view, main_textures.b.texture.texture,
                                main_textures.a.texture.default_view, main_textures.a.texture.texture};
    }

   private:
    std::uint32_t current_index() const noexcept {
        return main_texture ? main_texture->load(std::memory_order_seq_cst) : 0;
    }
};
/** @brief Component holding the depth texture and attachment for a camera
 * (Bevy ViewDepthTexture, view/mod.rs:887-904). */
EPIX_EXPORT struct ViewDepthTexture {
    /** @brief The depth texture. */
    wgpu::Texture texture;
    /** @brief The depth attachment (view + first-call clear). */
    render_resource::DepthAttachment attachment;

    /** @brief Create from a texture and view (legacy constructor; the
     * attachment gets no clear value). */
    static ViewDepthTexture create(wgpu::Texture tex, wgpu::TextureView view) {
        return ViewDepthTexture{std::move(tex), render_resource::DepthAttachment(std::move(view), std::nullopt)};
    }
};

EPIX_EXPORT struct UVec2Hash {
    std::size_t operator()(const glm::uvec2& v) const noexcept {
        std::size_t h = (static_cast<std::size_t>(v.x) << 32) | v.y;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return h;
    }
};
/** @brief Key for a reusable view-depth texture. Multisample count is part of
 * the key because a 1x depth attachment cannot be paired with a 4x main
 * color attachment. */
EPIX_EXPORT struct ViewDepthCacheKey {
    glm::uvec2 size{};
    std::uint32_t sample_count = 1;
    bool operator==(const ViewDepthCacheKey&) const noexcept = default;
};
EPIX_EXPORT struct ViewDepthCacheKeyHash {
    std::size_t operator()(const ViewDepthCacheKey& key) const noexcept {
        std::size_t h = UVec2Hash{}(key.size);
        return h ^ (static_cast<std::size_t>(key.sample_count) + 0x9e3779b9 + (h << 6) + (h >> 2));
    }
};
/** @brief Cache of depth textures keyed by size and sample count to avoid
 * re-creation each frame. */
EPIX_EXPORT struct ViewDepthCache {
    /** @brief Map from compatible attachment descriptors to cached depth textures. */
    std::unordered_map<ViewDepthCacheKey, wgpu::Texture, ViewDepthCacheKeyHash> cache;
};

/** @brief Plugin that registers view extraction, target preparation, and
 * depth buffer creation systems. */
EPIX_EXPORT struct ViewPlugin {
    void attach(epix::app::App& app);
};

void prepare_view_target(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                              const camera::ExtractedCamera&,
                                              const ExtractedView&,
                                              const Msaa&>> views,
    epix::ecs::Commands cmd,
    epix::ecs::Res<window::ExtractedWindows> extracted_windows,
    epix::ecs::Res<wgpu::Device> device,
    epix::ecs::ResMut<ViewTargetAttachments> view_target_attachments);
void create_view_depth(epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                                        const camera::ExtractedCamera&,
                                                        const Msaa&>> views,
                       epix::ecs::Res<wgpu::Device> device,
                       epix::ecs::ResMut<ViewDepthCache> depth_cache,
                       epix::ecs::Commands cmd);

/** @brief Uniform buffer data for a view (Bevy 0.18 ViewUniform).
 *
 * Field order and sizes match the WGSL std140 layout emitted by Bevy's
 * ShaderType derive (view.wgsl): 7 mat4s, world_position + exposure,
 * viewport, main_pass_viewport, 6 frustum half-spaces, the packed
 * ColorGradingUniform, mip_bias and frame_count. sizeof == 768. */
EPIX_EXPORT struct ViewUniform {
    /** @brief Clip-from-world matrix (Bevy clip_from_world). */
    glm::mat4 clip_from_world;
    /** @brief Clip-from-world without temporal jitter (Bevy unjittered_clip_from_world). */
    glm::mat4 unjittered_clip_from_world;
    /** @brief World-from-clip matrix (Bevy world_from_clip). */
    glm::mat4 world_from_clip;
    /** @brief World-from-view matrix (Bevy world_from_view). */
    glm::mat4 world_from_view;
    /** @brief View-from-world matrix (Bevy view_from_world). */
    glm::mat4 view_from_world;
    /** @brief Clip-from-view (projection) matrix (Bevy clip_from_view). */
    glm::mat4 clip_from_view;
    /** @brief View-from-clip matrix (Bevy view_from_clip). */
    glm::mat4 view_from_clip;
    /** @brief Camera position in world space (Bevy world_position). */
    glm::vec3 world_position;
    /** @brief Exposure (Bevy exposure, Exposure::default() == 1.0). */
    float exposure = 1.0f;
    /** @brief Viewport (x_origin, y_origin, width, height). */
    glm::vec4 viewport = glm::vec4(0.0f);
    /** @brief Main-pass viewport (Bevy main_pass_viewport). */
    glm::vec4 main_pass_viewport = glm::vec4(0.0f);
    /** @brief 6 world-space half spaces: left, right, top, bottom, near, far. */
    std::array<glm::vec4, 6> frustum{};
    /** @brief Packed color grading values (Bevy color_grading). */
    ColorGradingUniform color_grading{};
    /** @brief Manual mip bias for the camera's textures (Bevy mip_bias). */
    float mip_bias = 0.0f;
    /** @brief Frame count since app start (Bevy frame_count). */
    std::uint32_t frame_count = 0;
    /** @brief Pad to WGSL std140 struct size 768. */
    float _pad_tail[2]{};
};
static_assert(sizeof(ViewUniform) == 768);
struct UniformBuffer {
    wgpu::Buffer buffer;
};
/** @brief Component holding the bind group for the view uniform buffer. */
EPIX_EXPORT struct ViewBindGroup {
    /** @brief Bind group exposing the ViewUniform to shaders. */
    wgpu::BindGroup bind_group;
};
/** @brief Resource holding the bind group layout for view uniform
 * binding. */
EPIX_EXPORT struct ViewUniformBindingLayout {
    wgpu::BindGroupLayout layout;
    ViewUniformBindingLayout(epix::ecs::World& world)
        : layout(world.resource<wgpu::Device>().createBindGroupLayout(
              wgpu::BindGroupLayoutDescriptor().setEntries(std::array{
                  wgpu::BindGroupLayoutEntry()
                      .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
                      .setBinding(0)
                      .setBuffer(wgpu::BufferBindingLayout()
                                     .setType(wgpu::BufferBindingType::eUniform)
                                     .setHasDynamicOffset(false)
                                     .setMinBindingSize(sizeof(ViewUniform))),
              }))) {}
};
/** @brief Render command template that binds the view uniform buffer at
 * the specified bind group slot.
 * @tparam Slot Bind group index. */
EPIX_EXPORT template <std::size_t Slot>
struct BindViewUniform {
    template <render::phase::PhaseItem P>
    struct Command {
        void prepare(const epix::ecs::World&) {}

        std::expected<void, render::phase::RenderCommandError> render(
            const P&,
            epix::ecs::Item<const ViewBindGroup&> view_bind_group,
            std::optional<epix::ecs::Item<>> entity_item,
            epix::ecs::ParamSet<>,
            const wgpu::RenderPassEncoder& encoder) {
            encoder.setBindGroup(Slot, std::get<0>(*view_bind_group).bind_group, std::span<const std::uint32_t>{});
            return {};
        }
    };
};
}  // namespace epix::render::view
namespace epix::render::camera {
/** @brief Manual mip bias for the camera's textures (Bevy MipBias). Defined below; forward-declared for extract_cameras. */
struct MipBias;
/** @brief Per-frame temporal jitter (Bevy TemporalJitter). Defined below. */
struct TemporalJitter;

/** @brief System that extracts camera data into the render world. */
EPIX_EXPORT void extract_cameras(
    epix::ecs::Commands cmd,
    epix::ecs::Res<ClearColor> global_clear_color,
    epix::app::Extract<epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                                        const Camera&,
                                                        const CameraRenderGraph&,
                                                        const transform::GlobalTransform&,
                                                        const view::VisibleEntities&,
                                                        const view::Frustum&,
                                                        epix::ecs::Opt<const RenderLayers&>,
                                                        epix::ecs::Opt<const camera::MipBias&>,
                                                        epix::ecs::Opt<const camera::TemporalJitter&>,
                                                        epix::ecs::Opt<const view::Hdr&>,
                                                        epix::ecs::Opt<const view::ColorGrading&>,
                                                        epix::ecs::Opt<const Exposure&>,
                                                        epix::ecs::Opt<const MainPassResolutionOverride&>,
                                                        const view::Msaa&,
                                                        epix::ecs::Opt<const CameraMainTextureUsages&>>>> cameras,
    epix::app::Extract<
        epix::ecs::Query<epix::ecs::Entity, epix::ecs::With<::epix::window::PrimaryWindow, ::epix::window::Window>>>
        primary_window);

/** @brief Label for the camera driver node in the render graph. */
EPIX_EXPORT inline constexpr struct CameraDriverNodeLabelT {
} CameraDriverNodeLabel;

/** @brief Node that drives each camera's render graph in sorted order (Bevy
 * CameraDriverNode, renderer/camera_driver_node.rs). Defined in view.cpp. */
EPIX_EXPORT struct CameraDriverNode : graph::Node {
    void run(graph::GraphContext& graph, graph::RenderContext& render_ctx, const epix::ecs::World& world) override;
};

// Bevy has no camera bundles: spawn Camera (with a CameraRenderGraph for a
// specific graph) and the required components (Projection, Transform,
// VisibleEntities, RenderLayers, Msaa, Frustum) are added automatically.
}  // namespace epix::render::camera

namespace epix::render::view {
EPIX_EXPORT struct Hdr {};

/** @brief Marker component: the view does not support indirect drawing (Bevy NoIndirectDrawing). */
EPIX_EXPORT struct NoIndirectDrawing {};

/** @brief Marker component: the view does not support CPU culling (Bevy NoCpuCulling). */
EPIX_EXPORT struct NoCpuCulling {};

/**
 * @brief Resource holding the dynamic uniform buffer for all view uniforms and
 * the per-view offsets (Bevy ViewUniforms).
 */
EPIX_EXPORT struct ViewUniforms {
    /** @brief Dynamic uniform buffer containing all ViewUniforms. */
    render_resource::DynamicUniformBuffer<ViewUniform> uniforms;
    /** @brief Per-view dynamic offsets into uniforms. */
    std::vector<std::uint32_t> offsets;
};

/** @brief Component storing the offset of a view's uniform inside ViewUniforms (Bevy ViewUniformOffset). */
EPIX_EXPORT struct ViewUniformOffset {
    /** @brief Dynamic buffer offset in bytes. */
    std::uint32_t offset = 0;
};


/**
 * @brief Render-world counterpart of VisibleEntities (Bevy RenderVisibleEntities).
 */
EPIX_EXPORT struct RenderVisibleEntities {
    /** @brief Visible (render-entity, main-entity) pairs per visibility class. */
    std::unordered_map<meta::type_index, std::vector<std::pair<epix::ecs::Entity, sync_world::MainEntity>>> entities;

    /** @brief Entities visible to the view for the given query-filter type
     * (Bevy RenderVisibleEntities::get<QF>, empty slice when absent). */
    template <typename QF>
    const std::vector<std::pair<epix::ecs::Entity, sync_world::MainEntity>>& get() const {
        static const std::vector<std::pair<epix::ecs::Entity, sync_world::MainEntity>> kEmpty;
        if (auto it = entities.find(meta::type_index(meta::type_id<QF>())); it != entities.end()) return it->second;
        return kEmpty;
    }
    /** @brief Span over the visible entities for the given type (Bevy
     * RenderVisibleEntities::iter<QF>, DoubleEndedIterator). */
    template <typename QF>
    std::span<const std::pair<epix::ecs::Entity, sync_world::MainEntity>> iter() const {
        return get<QF>();
    }
    /** @brief Number of visible entities for the given type (Bevy
     * RenderVisibleEntities::len<QF>). */
    template <typename QF>
    std::size_t len() const {
        return get<QF>().size();
    }
    /** @brief Whether any entity of the given type is visible (Bevy
     * RenderVisibleEntities::is_empty<QF>). */
    template <typename QF>
    bool is_empty() const {
        return get<QF>().empty();
    }
};

/**
 * @brief Per-view render targets keyed by normalized render target (Bevy ViewTargetAttachments).
 */
EPIX_EXPORT struct ViewTargetAttachments {
    /** @brief One shared output attachment per render target, so the output is
     * cleared at most once per frame and later cameras composite over it
     * (Bevy ViewTargetAttachments). */
    std::unordered_map<camera::RenderTargetId, OutputColorAttachment, camera::RenderTargetIdHash> attachments;
};

/** @brief Clears the per-frame view target attachments (Bevy
 * clear_view_attachments, view/mod.rs:1042-1044). Registered in
 * RenderSystems::ManageViews before create_surfaces. */
void clear_view_attachments(epix::ecs::ResMut<ViewTargetAttachments> view_target_attachments);
/** @brief Removes the ViewTarget of cameras targeting a window that was
 * resized or changed present mode, so prepare_view_target recreates them at
 * the new size (Bevy cleanup_view_targets_for_resize, view/mod.rs:1046-1059).
 * Registered in RenderSystems::ManageViews before create_surfaces. */
void cleanup_view_targets_for_resize(
    epix::ecs::Commands cmd,
    epix::ecs::Res<window::ExtractedWindows> windows,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, const camera::ExtractedCamera&>> cameras);

}  // namespace epix::render::view

namespace epix::render::camera {
/** @brief Error returned when render target information cannot be resolved (Bevy MissingRenderTargetInfoError). */
EPIX_EXPORT struct MissingRenderTargetInfoError {
    /** @brief Description of the unresolvable target. */
    std::string message;
    std::string to_string() const { return "missing render target info: " + message; }
};

/** @brief Per-camera sort entry (Bevy SortedCamera). */
EPIX_EXPORT struct SortedCamera {
    /** @brief Render-world camera entity. */
    epix::ecs::Entity entity;
    /** @brief Camera order; lower renders first (behind). */
    std::ptrdiff_t order = 0;
    /** @brief The normalized render target this camera renders to (Bevy SortedCamera::target). */
    std::optional<RenderTarget> target;
    /** @brief Whether this camera uses an HDR intermediate texture. */
    bool hdr = false;

    /** @brief Comparable key used to group same-order cameras by target type
     * and identity (Bevy sorts by (order, target)). */
    std::pair<std::ptrdiff_t, std::size_t> sort_key() const noexcept {
        std::size_t target_key = 0;
        if (target) {
            target_key = std::visit(
                utils::visitor{
                    [](const wgpu::Texture&) -> std::size_t { return 1; },
                    [](const WindowRef& w) -> std::size_t {
                        return 2 + static_cast<std::size_t>(w.window_entity.index);
                    },
                },
                *target);
        }
        return {order, target_key};
    }
};

/** @brief Resource holding cameras sorted by order (Bevy SortedCameras). */
EPIX_EXPORT struct SortedCameras {
    /** @brief Sorted camera list. */
    std::vector<SortedCamera> cameras;
};

/**
 * @brief System that sorts all extracted cameras by order, packing cameras
 * targeting the same render target together (Bevy sort_cameras). Also
 * assigns each camera its per-target index.
 */
EPIX_EXPORT inline void sort_cameras(epix::ecs::ResMut<SortedCameras> sorted_cameras,
                                     epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, epix::ecs::Mut<ExtractedCamera>>> cameras) {
    sorted_cameras->cameras.clear();
    for (auto&& [entity, camera] : cameras.iter()) {
        sorted_cameras->cameras.push_back(
            SortedCamera{entity, camera.get().order, camera.get().render_target, camera.get().hdr});
    }
    // Bevy uses a stable sort (sort_by): cameras with equal (order, target)
    // keep their extraction order.
    std::ranges::stable_sort(sorted_cameras->cameras,
                             [](const SortedCamera& a, const SortedCamera& b) { return a.sort_key() < b.sort_key(); });
    // Assign per-target indices in sorted order, keyed by (target, hdr)
    // (Bevy camera.rs:744-763). Textures have a distinct identity per handle,
    // so cameras targeting different textures never share a counter.
    std::unordered_map<std::uint64_t, std::size_t> counts;
    std::unordered_map<epix::ecs::Entity, std::size_t> index_for_entity;
    for (const auto& cam : sorted_cameras->cameras) {
        if (!cam.target) continue;
        const std::uint64_t key = (cam.target->identity().value << 1) | static_cast<std::uint64_t>(cam.hdr);
        index_for_entity[cam.entity] = counts[key]++;
    }
    for (auto&& [entity, camera] : cameras.iter()) {
        if (auto it = index_for_entity.find(entity); it != index_for_entity.end()) {
            camera.get_mut().sorted_camera_index_for_target = it->second;
        }
    }
}

/** @brief Per-frame temporal jitter in texels (Bevy TemporalJitter). */
EPIX_EXPORT struct TemporalJitter {
    /** @brief Jitter offset in texels. */
    glm::vec2 offset = glm::vec2(0.0f);
    /** @brief Applies Bevy's temporal sub-pixel projection adjustment. */
    void jitter_projection(glm::mat4& clip_from_view, glm::vec2 view_size) const noexcept {
        if (view_size.x == 0.0f || view_size.y == 0.0f) return;
        glm::vec2 jitter = (offset * glm::vec2(2.0f, -2.0f)) / view_size;
        if (clip_from_view[3][3] == 1.0f) {
            jitter *= glm::vec2(clip_from_view[0][0], clip_from_view[1][1]) * 0.5f;
        }
        clip_from_view[2][0] += jitter.x;
        clip_from_view[2][1] += jitter.y;
    }
};

/** @brief Manual mip bias for the camera's textures (Bevy MipBias). */
EPIX_EXPORT struct MipBias {
    /** @brief Bias applied to mip selection (Bevy default -1.0, camera.rs:698-702). */
    float bias = -1.0f;
};

}  // namespace epix::render::camera

/** @brief Hash for `RetainedViewEntity` (Bevy derives Hash). */
template <>
struct std::hash<::epix::render::view::RetainedViewEntity> {
    std::size_t operator()(const ::epix::render::view::RetainedViewEntity& r) const noexcept {
        std::size_t h = std::hash<::epix::render::sync_world::MainEntity>{}(r.main_entity);
        h ^= r.auxiliary_entity.transform([](const ::epix::render::sync_world::MainEntity& aux) {
                 return std::hash<::epix::render::sync_world::MainEntity>{}(aux);
             }).value_or(0u) +
             0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(r.subview_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
