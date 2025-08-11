#pragma once

#include <epix/app.h>
#include <epix/assets.h>
#include <epix/vulkan.h>

#include "shader.h"

namespace epix::render {
struct ShaderInfo {
    assets::Handle<Shader> shader;
    std::string debugName;
    std::string entryName;
};

struct RenderPipelineDesc : nvrhi::GraphicsPipelineDesc {
    using Base = nvrhi::GraphicsPipelineDesc;

    ShaderInfo vertexShader;
    ShaderInfo fragmentShader;
    ShaderInfo geometryShader;
    ShaderInfo tessellationControlShader;
    ShaderInfo tessellationEvaluationShader;

    std::vector<nvrhi::BindingLayoutDesc> bindingLayoutDescs;

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
    RenderPipelineDesc& addBindingLayout(
        const nvrhi::BindingLayoutDesc& layout) {
        bindingLayoutDescs.push_back(layout);
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
    ComputePipelineDesc& addBindingLayout(
        const nvrhi::BindingLayoutDesc& layout) {
        bindingLayoutDescs.push_back(layout);
        return *this;
    }
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
        nvrhi::DeviceHandle device;
        nvrhi::GraphicsPipelineDesc desc;

        RenderPipelineId _id;

        std::vector<
            std::pair<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle>>
            specializedPipelines;

       public:
        RenderPipelineCache(nvrhi::DeviceHandle device,
                            RenderPipelineId id,
                            const nvrhi::GraphicsPipelineDesc& desc)
            : device(device), desc(desc), _id(id) {}

        RenderPipelineId id() const { return _id; }

        size_t specialize(nvrhi::FramebufferHandle framebuffer) {
            size_t index = 0;
            for (; index < specializedPipelines.size(); index++) {
                if (specializedPipelines[index].first ==
                    framebuffer->getFramebufferInfo()) {
                    return index;
                }
            }

            nvrhi::GraphicsPipelineHandle pipeline =
                device->createGraphicsPipeline(desc, framebuffer);
            if (!pipeline) {
                spdlog::error("Failed to create pipeline for framebuffer");
                return -1;  // Indicate failure
            }
            specializedPipelines.emplace_back(framebuffer->getFramebufferInfo(),
                                              pipeline);
            return specializedPipelines.size() - 1;
        }
        nvrhi::GraphicsPipelineHandle get(size_t index) const {
            if (index < specializedPipelines.size()) {
                return specializedPipelines[index].second;
            }
            return nullptr;
        }
        std::tuple<size_t, nvrhi::GraphicsPipelineHandle> get(
            nvrhi::FramebufferHandle framebuffer) {
            size_t index = specialize(framebuffer);
            return {index, get(index)};
        }
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
    PipelineServer(nvrhi::DeviceHandle device) : device(device) {}

    static PipelineServer from_world(epix::World& world) {
        return PipelineServer{world.resource<nvrhi::DeviceHandle>()};
    }

    RenderPipelineId queue_render_pipeline(const RenderPipelineDesc& desc) {
        RenderPipelineId id{renderPipelines.size()};
        renderPipelines.emplace_back(desc);
        queuedPipelines.emplace_back(id);
        return id;
    }
    ComputePipelineId queue_compute_pipeline(const ComputePipelineDesc& desc) {
        ComputePipelineId id{computePipelines.size()};
        computePipelines.emplace_back(desc);
        queuedPipelines.emplace_back(id);
        return id;
    }

    std::optional<RenderPipeline> get_render_pipeline(
        RenderPipelineId id, nvrhi::FramebufferHandle framebuffer) {
        if (id.id < renderPipelines.size()) {
            auto& pipeline = renderPipelines[id];
            if (std::holds_alternative<RenderPipelineCache>(pipeline)) {
                auto& cache = std::get<RenderPipelineCache>(pipeline);
                size_t specializedIndex = cache.specialize(framebuffer);
                return RenderPipeline{id, specializedIndex,
                                      cache.get(specializedIndex)};
            }
        }
        return std::nullopt;
    }
    std::optional<ComputePipeline> get_compute_pipeline(ComputePipelineId id) {
        if (id.id < computePipelines.size()) {
            auto& pipeline = computePipelines[id];
            if (std::holds_alternative<ComputePipeline>(pipeline)) {
                return std::get<ComputePipeline>(pipeline);
            }
        }
        return std::nullopt;
    }

    static void process_queued(ResMut<PipelineServer> server,
                               Res<assets::RenderAssets<Shader>> shaders) {
        size_t count = server->queuedPipelines.size();
        while (count != 0) {
            count--;
            auto id = std::move(server->queuedPipelines.front());
            server->queuedPipelines.pop_front();
            auto&& device = server->device;
            std::visit(
                util::visitor{
                    [&](const RenderPipelineId& id) {
                        auto& renderDesc = std::get<RenderPipelineDesc>(
                            server->renderPipelines[id]);
                        if (!renderDesc.VS) {
                            if (!renderDesc.vertexShader.shader) {
                                spdlog::error(
                                    "[pipeline-server] "
                                    "RenderPipeline with id={} has no vertex "
                                    "shader provided",
                                    id.id);
                                server->renderPipelines[id] = std::make_pair(
                                    renderDesc,
                                    PipelineError{"No vertex shader provided"});
                                return;
                            } else {
                                if (auto vs = shaders->try_get(
                                        renderDesc.vertexShader.shader)) {
                                    renderDesc.VS = vs->create_shader(
                                        device, nvrhi::ShaderType::Vertex,
                                        renderDesc.vertexShader.entryName,
                                        renderDesc.vertexShader.debugName);
                                } else {
                                    // shader not extracted yet, retry next
                                    // frame
                                    server->queuedPipelines.emplace_back(id);
                                    return;
                                }
                            }
                        }
                        if (!renderDesc.PS) {
                            if (!renderDesc.fragmentShader.shader) {
                                spdlog::error(
                                    "[pipeline-server] "
                                    "RenderPipeline with id={} has no fragment "
                                    "shader provided",
                                    id.id);
                                server->renderPipelines[id] = std::make_pair(
                                    renderDesc,
                                    PipelineError{
                                        "No fragment shader provided"});
                                return;
                            } else {
                                if (auto ps = shaders->try_get(
                                        renderDesc.fragmentShader.shader)) {
                                    renderDesc.PS = ps->create_shader(
                                        device, nvrhi::ShaderType::Pixel,
                                        renderDesc.fragmentShader.entryName,
                                        renderDesc.fragmentShader.debugName);
                                } else {
                                    // shader not extracted yet, retry next
                                    // frame
                                    server->queuedPipelines.emplace_back(id);
                                    return;
                                }
                            }
                        }
                        if (!renderDesc.GS &&
                            renderDesc.geometryShader.shader) {
                            if (auto gs = shaders->try_get(
                                    renderDesc.geometryShader.shader)) {
                                renderDesc.GS = gs->create_shader(
                                    device, nvrhi::ShaderType::Geometry,
                                    renderDesc.geometryShader.entryName,
                                    renderDesc.geometryShader.debugName);
                            } else {
                                // shader not extracted yet, retry next
                                // frame
                                server->queuedPipelines.emplace_back(id);
                                return;
                            }
                        }
                        if (!renderDesc.HS &&
                            renderDesc.tessellationControlShader.shader) {
                            if (auto hs = shaders->try_get(
                                    renderDesc.tessellationControlShader
                                        .shader)) {
                                renderDesc.HS = hs->create_shader(
                                    device, nvrhi::ShaderType::Hull,
                                    renderDesc.tessellationControlShader
                                        .entryName,
                                    renderDesc.tessellationControlShader
                                        .debugName);
                            } else {
                                // shader not extracted yet, retry next
                                // frame
                                server->queuedPipelines.emplace_back(id);
                                return;
                            }
                        }
                        if (!renderDesc.DS &&
                            renderDesc.tessellationEvaluationShader.shader) {
                            if (auto ds = shaders->try_get(
                                    renderDesc.tessellationEvaluationShader
                                        .shader)) {
                                renderDesc.DS = ds->create_shader(
                                    device, nvrhi::ShaderType::Domain,
                                    renderDesc.tessellationEvaluationShader
                                        .entryName,
                                    renderDesc.tessellationEvaluationShader
                                        .debugName);
                            } else {
                                // shader not extracted yet, retry next
                                // frame
                                server->queuedPipelines.emplace_back(id);
                                return;
                            }
                        }
                        if (renderDesc.bindingLayouts.empty() &&
                            !renderDesc.bindingLayoutDescs.empty()) {
                            renderDesc.bindingLayouts =
                                renderDesc.bindingLayoutDescs |
                                std::views::transform([&](auto&& desc) {
                                    return device->createBindingLayout(desc);
                                }) |
                                std::ranges::to<nvrhi::BindingLayoutVector>();
                        }
                        server->renderPipelines[id] =
                            RenderPipelineCache(device, id, renderDesc);
                    },
                    [&](const ComputePipelineId& id) {
                        auto& computeDesc = std::get<ComputePipelineDesc>(
                            server->computePipelines[id]);
                        if (!computeDesc.CS &&
                            !computeDesc.computeShader.shader) {
                            spdlog::error(
                                "[pipeline-server] "
                                "ComputePipeline with id={} has no compute "
                                "shader provided",
                                id.id);
                            server->computePipelines[id] = std::make_pair(
                                computeDesc,
                                PipelineError{"No compute shader provided"});
                            return;
                        }
                        if (!computeDesc.CS) {
                            if (auto cs = shaders->try_get(
                                    computeDesc.computeShader.shader)) {
                                computeDesc.CS = cs->create_shader(
                                    device, nvrhi::ShaderType::Compute,
                                    computeDesc.computeShader.entryName,
                                    computeDesc.computeShader.debugName);
                            } else {
                                // shader not extracted yet, retry next frame
                                server->queuedPipelines.emplace_back(id);
                                return;
                            }
                        }
                        auto pipeline =
                            device->createComputePipeline(computeDesc);
                        server->computePipelines[id] =
                            ComputePipeline{id, pipeline};
                    },
                },
                id);
        }
    }
};

struct PipelineServerPlugin {
    void build(epix::App& app) {
        if (auto renderApp = app.get_sub_app(Render)) {
            renderApp->init_resource<PipelineServer>();
            renderApp->add_systems(Render,
                                   into(PipelineServer::process_queued)
                                       .set_name("process queued pipelines")
                                       .in_set(RenderSet::PrepareResources));
        }
    }
};
}  // namespace epix::render