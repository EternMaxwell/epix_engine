module;
#include <epix/render.hpp>

export module epix.render;

export import epix.shader;

export namespace epix::render {
using epix::render::AnonymousSurface;
using epix::render::CachedPipelineId;
using epix::render::CachedPipelineState;
using epix::render::ComputePipeline;
using epix::render::ComputePipelineDescriptor;
using epix::render::CustomRendered;
using epix::render::DefaultImageSampler;
using epix::render::ExtractAssetPlugin;
using epix::render::ExtractAssetSet;
using epix::render::ExtractResourcePlugin;
using epix::render::ExtractSchedule;
using epix::render::ExtractScheduleT;
using epix::render::FragmentState;
using epix::render::GetPipelineError;
using epix::render::GetPipelineInvalidId;
using epix::render::GetPipelineNotReady;
using epix::render::GPUImage;
using epix::render::LayoutCache;
using epix::render::LayoutCacheKey;
using epix::render::Pipeline;
using epix::render::PipelineDescriptor;
using epix::render::PipelineError;
using epix::render::PipelineServer;
using epix::render::PipelineServerError;
using epix::render::PipelineStateCreating;
using epix::render::PipelineStateQueued;
using epix::render::PipelineStateRecoverableShaderError;
using epix::render::Render;
using epix::render::RenderAsset;
using epix::render::RenderAssets;
using epix::render::RenderAssetUsage;
using epix::render::RenderAssetUsageBits;
using epix::render::RenderPipeline;
using epix::render::RenderPipelineDescriptor;
using epix::render::RenderPlugin;
using epix::render::RenderSet;
using epix::render::RenderT;
using epix::render::VertexState;
using epix::render::phase::sort_phase_items;
}  // namespace epix::render

export namespace epix::render::camera {
using epix::render::camera::Camera;
using epix::render::camera::CameraBundle;
using epix::render::camera::CameraProjectionPlugin;
using epix::render::camera::CameraRenderGraph;
using epix::render::camera::CameraUpdateSystems;
using epix::render::camera::ClearColor;
using epix::render::camera::ClearColorConfig;
using epix::render::camera::ExtractedCamera;
using epix::render::camera::OrthographicProjection;
using epix::render::camera::PerspectiveProjection;
using epix::render::camera::Projection;
using epix::render::camera::RenderLayer;
using epix::render::camera::RenderTarget;
using epix::render::camera::ScalingMode;
using epix::render::camera::Viewport;
using epix::render::camera::WindowRef;
}  // namespace epix::render::camera

export namespace epix::render::graph {
using epix::render::graph::EdgeError;
using epix::render::graph::EdgeNodesNotPresent;
using epix::render::graph::EmptyNode;
using epix::render::graph::GraphContext;
using epix::render::graph::GraphError;
using epix::render::graph::GraphInputNode;
using epix::render::graph::GraphLabel;
using epix::render::graph::InputSlotOccupied;
using epix::render::graph::Node;
using epix::render::graph::NodeLabel;
using epix::render::graph::NodeNotPresent;
using epix::render::graph::NodeState;
using epix::render::graph::RenderContext;
using epix::render::graph::RenderGraph;
using epix::render::graph::SlotInfo;
using epix::render::graph::SlotLabel;
using epix::render::graph::SlotNotPresent;
using epix::render::graph::SlotType;
using epix::render::graph::SlotTypeMismatch;
using epix::render::graph::SlotValue;
using epix::render::graph::SubGraphExists;
}  // namespace epix::render::graph

export namespace epix::render::phase {
using epix::render::phase::BatchedPhaseItem;
using epix::render::phase::CachedRenderPipelinePhaseItem;
using epix::render::phase::Draw;
using epix::render::phase::DrawError;
using epix::render::phase::DrawFunction;
using epix::render::phase::DrawFunctionId;
using epix::render::phase::DrawFunctions;
using epix::render::phase::EmptyDrawFunction;
using epix::render::phase::OpaqueSortKey;
using epix::render::phase::PhaseItem;
using epix::render::phase::RenderCommand;
using epix::render::phase::RenderCommandError;
using epix::render::phase::RenderPhase;
using epix::render::phase::SetItemPipeline;
}  // namespace epix::render::phase

export namespace epix::render::view {
using epix::render::view::BindViewUniform;
using epix::render::view::ExtractedView;
using epix::render::view::UVec2Hash;
using epix::render::view::ViewBindGroup;
using epix::render::view::ViewDepth;
using epix::render::view::ViewDepthCache;
using epix::render::view::ViewPlugin;
using epix::render::view::ViewTarget;
using epix::render::view::ViewUniform;
using epix::render::view::ViewUniformBindingLayout;
using epix::render::view::VisibleEntities;
}  // namespace epix::render::view

export namespace epix::render::window {
using epix::render::window::ExtractedWindow;
using epix::render::window::ExtractedWindows;
using epix::render::window::SurfaceCreation;
using epix::render::window::WindowRenderPlugin;
}  // namespace epix::render::window
