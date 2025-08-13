#pragma once

#include <epix/app.h>
#include <epix/assets.h>
#include <epix/vulkan.h>

#include "shader.h"

namespace epix::render {
struct ShaderInfo {
    assets::Handle<Shader> shader;
    std::string debugName;
    std::string entryName = "main";
};

struct RenderPipelineDesc : nvrhi::GraphicsPipelineDesc {
    using Base = nvrhi::GraphicsPipelineDesc;

    ShaderInfo vertexShader;
    ShaderInfo fragmentShader;
    ShaderInfo geometryShader;
    ShaderInfo tessellationControlShader;
    ShaderInfo tessellationEvaluationShader;

    std::vector<nvrhi::BindingLayoutDesc> bindingLayoutDescs;

    EPIX_API RenderPipelineDesc& setVertexShader(ShaderInfo shader);
    EPIX_API RenderPipelineDesc& setFragmentShader(ShaderInfo shader);
    EPIX_API RenderPipelineDesc& setGeometryShader(ShaderInfo shader);
    EPIX_API RenderPipelineDesc& setTessellationControlShader(
        ShaderInfo shader);
    EPIX_API RenderPipelineDesc& setTessellationEvaluationShader(
        ShaderInfo shader);
    EPIX_API RenderPipelineDesc& addBindingLayout(
        const nvrhi::BindingLayoutDesc& layout);
};
struct ComputePipelineDesc : nvrhi::ComputePipelineDesc {
    using Base = nvrhi::ComputePipelineDesc;

    ShaderInfo computeShader;
    std::vector<nvrhi::BindingLayoutDesc> bindingLayoutDescs;

    EPIX_API ComputePipelineDesc& setComputeShader(ShaderInfo shader);
    EPIX_API ComputePipelineDesc& addBindingLayout(
        const nvrhi::BindingLayoutDesc& layout);
};

struct ComputePipelineId {
    size_t id;

    ComputePipelineId(size_t id) : id(id) {}
    auto operator<=>(const ComputePipelineId& other) const = default;

    operator size_t() const { return id; }
};
struct RenderPipelineId {
    size_t id;

    RenderPipelineId(size_t id) : id(id) {}
    auto operator<=>(const RenderPipelineId& other) const = default;

    operator size_t() const { return id; }
};

struct RenderPipeline {
    RenderPipelineId id;
    size_t specializedIndex;
    nvrhi::GraphicsPipelineHandle handle;
};
struct ComputePipeline {
    ComputePipelineId id;
    nvrhi::ComputePipelineHandle handle;
};

struct PipelineError {
    std::string message;
};

struct PipelineServer {
   private:
    struct RenderPipelineCache {
       private:
        mutable nvrhi::DeviceHandle device;
        nvrhi::GraphicsPipelineDesc desc;

        RenderPipelineId _id;

        mutable std::vector<
            std::pair<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle>>
            specializedPipelines;

        mutable std::mutex _mutex;

       public:
        EPIX_API RenderPipelineCache(nvrhi::DeviceHandle device,
                                     RenderPipelineId id,
                                     const nvrhi::GraphicsPipelineDesc& desc);
        RenderPipelineCache(const RenderPipelineCache&)            = delete;
        RenderPipelineCache& operator=(const RenderPipelineCache&) = delete;
        EPIX_API RenderPipelineCache(RenderPipelineCache&&);
        EPIX_API RenderPipelineCache& operator=(RenderPipelineCache&&);

        EPIX_API RenderPipelineId id() const;

        EPIX_API size_t specialize(nvrhi::FramebufferHandle framebuffer) const;
        EPIX_API nvrhi::GraphicsPipelineHandle get(size_t index) const;
        EPIX_API std::tuple<size_t, nvrhi::GraphicsPipelineHandle> get(
            nvrhi::FramebufferHandle framebuffer) const;
    };

    nvrhi::DeviceHandle device;
    std::vector<std::variant<ComputePipeline,
                             ComputePipelineDesc,
                             std::pair<ComputePipelineDesc, PipelineError>>>
        computePipelines;
    std::vector<std::variant<RenderPipelineCache,
                             RenderPipelineDesc,
                             std::pair<RenderPipelineDesc, PipelineError>>>
        renderPipelines;

    std::deque<std::variant<RenderPipelineId, ComputePipelineId>>
        queuedPipelines;

   public:
    EPIX_API PipelineServer(nvrhi::DeviceHandle device);

    EPIX_API static PipelineServer from_world(epix::World& world);

    EPIX_API RenderPipelineId
    queue_render_pipeline(const RenderPipelineDesc& desc);
    EPIX_API ComputePipelineId
    queue_compute_pipeline(const ComputePipelineDesc& desc);

    EPIX_API std::optional<RenderPipeline> get_render_pipeline(
        RenderPipelineId id, nvrhi::FramebufferHandle framebuffer) const;
    EPIX_API std::optional<ComputePipeline> get_compute_pipeline(
        ComputePipelineId id) const;

    EPIX_API static void process_queued(
        ResMut<PipelineServer> server,
        Res<assets::RenderAssets<Shader>> shaders);
};

struct PipelineServerPlugin {
    EPIX_API void build(epix::App& app);
};
}  // namespace epix::render