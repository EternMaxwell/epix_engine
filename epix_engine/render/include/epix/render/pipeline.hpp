#pragma once

#include <epix/assets.hpp>
#include <epix/core.hpp>

#include "shader.hpp"
#include "vulkan.hpp"

namespace epix::render {
struct ShaderInfo {
    std::optional<assets::Handle<Shader>> shader;
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

    RenderPipelineDesc& setVertexShader(ShaderInfo shader) {
        vertexShader = std::move(shader);
        return *this;
    }
    RenderPipelineDesc& setFragmentShader(ShaderInfo shader) {
        fragmentShader = std::move(shader);
        return *this;
    }
    RenderPipelineDesc& setGeometryShader(ShaderInfo shader) {
        geometryShader = std::move(shader);
        return *this;
    }
    RenderPipelineDesc& setTessellationControlShader(ShaderInfo shader) {
        tessellationControlShader = std::move(shader);
        return *this;
    }
    RenderPipelineDesc& setTessellationEvaluationShader(ShaderInfo shader) {
        tessellationEvaluationShader = std::move(shader);
        return *this;
    }
};
struct ComputePipelineDesc : nvrhi::ComputePipelineDesc {
    using Base = nvrhi::ComputePipelineDesc;

    ShaderInfo computeShader;
    std::vector<nvrhi::BindingLayoutDesc> bindingLayoutDescs;

    ComputePipelineDesc& setComputeShader(ShaderInfo shader) {
        computeShader = std::move(shader);
        return *this;
    }
    ComputePipelineDesc& addBindingLayout(const nvrhi::BindingLayoutDesc& layout) {
        bindingLayoutDescs.push_back(layout);
        return *this;
    }
};

struct ComputePipelineId {
    size_t id;

    ComputePipelineId() : id(std::numeric_limits<size_t>::max()) {}
    ComputePipelineId(size_t id) : id(id) {}
    auto operator<=>(const ComputePipelineId& other) const = default;

    operator size_t() const { return id; }
};
struct RenderPipelineId {
    size_t id;

    RenderPipelineId() : id(std::numeric_limits<size_t>::max()) {}
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

        mutable std::vector<std::pair<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle>> specializedPipelines;

        mutable std::mutex _mutex;

       public:
        RenderPipelineCache(nvrhi::DeviceHandle device, RenderPipelineId id, const nvrhi::GraphicsPipelineDesc& desc);
        RenderPipelineCache(const RenderPipelineCache&)            = delete;
        RenderPipelineCache& operator=(const RenderPipelineCache&) = delete;
        RenderPipelineCache(RenderPipelineCache&&);
        RenderPipelineCache& operator=(RenderPipelineCache&&);

        RenderPipelineId id() const;

        size_t specialize(const nvrhi::FramebufferInfo& info) const;
        nvrhi::GraphicsPipelineHandle get(size_t index) const;
        std::tuple<size_t, nvrhi::GraphicsPipelineHandle> get(const nvrhi::FramebufferInfo& info) const;
    };

    nvrhi::DeviceHandle device;
    std::vector<std::variant<ComputePipeline, ComputePipelineDesc, std::pair<ComputePipelineDesc, PipelineError>>>
        computePipelines;
    std::vector<std::variant<RenderPipelineCache, RenderPipelineDesc, std::pair<RenderPipelineDesc, PipelineError>>>
        renderPipelines;

    std::deque<std::variant<RenderPipelineId, ComputePipelineId>> queuedPipelines;

   public:
    PipelineServer(nvrhi::DeviceHandle device);
    PipelineServer(const PipelineServer&)            = delete;
    PipelineServer& operator=(const PipelineServer&) = delete;
    PipelineServer(PipelineServer&&)                 = default;
    PipelineServer& operator=(PipelineServer&&)      = default;

    static PipelineServer from_world(epix::World& world);

    RenderPipelineId queue_render_pipeline(const RenderPipelineDesc& desc);
    ComputePipelineId queue_compute_pipeline(const ComputePipelineDesc& desc);

    std::optional<RenderPipeline> get_render_pipeline(RenderPipelineId id, const nvrhi::FramebufferInfo& info) const;
    std::optional<ComputePipeline> get_compute_pipeline(ComputePipelineId id) const;

    static void process_queued(ResMut<PipelineServer> server, Res<assets::RenderAssets<Shader>> shaders);
};

struct PipelineServerPlugin {
    void build(epix::App& app);
};
}  // namespace epix::render