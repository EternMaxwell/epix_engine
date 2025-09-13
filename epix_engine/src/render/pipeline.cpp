#include "epix/render/pipeline.h"

using namespace epix::render;

EPIX_API RenderPipelineDesc& RenderPipelineDesc::setVertexShader(ShaderInfo shader) {
    vertexShader = std::move(shader);
    return *this;
}
EPIX_API RenderPipelineDesc& RenderPipelineDesc::setFragmentShader(ShaderInfo shader) {
    fragmentShader = std::move(shader);
    return *this;
}
EPIX_API RenderPipelineDesc& RenderPipelineDesc::setGeometryShader(ShaderInfo shader) {
    geometryShader = std::move(shader);
    return *this;
}
EPIX_API RenderPipelineDesc& RenderPipelineDesc::setTessellationControlShader(ShaderInfo shader) {
    tessellationControlShader = std::move(shader);
    return *this;
}
EPIX_API RenderPipelineDesc& RenderPipelineDesc::setTessellationEvaluationShader(ShaderInfo shader) {
    tessellationEvaluationShader = std::move(shader);
    return *this;
}
EPIX_API RenderPipelineDesc& RenderPipelineDesc::addBindingLayout(const nvrhi::BindingLayoutDesc& layout) {
    bindingLayoutDescs.push_back(layout);
    return *this;
}

EPIX_API ComputePipelineDesc& ComputePipelineDesc::setComputeShader(ShaderInfo shader) {
    computeShader = std::move(shader);
    return *this;
}
EPIX_API ComputePipelineDesc& ComputePipelineDesc::addBindingLayout(const nvrhi::BindingLayoutDesc& layout) {
    bindingLayoutDescs.push_back(layout);
    return *this;
}

EPIX_API PipelineServer::RenderPipelineCache::RenderPipelineCache(nvrhi::DeviceHandle device,
                                                                  RenderPipelineId id,
                                                                  const nvrhi::GraphicsPipelineDesc& desc)
    : device(device), desc(desc), _id(id) {}

EPIX_API PipelineServer::RenderPipelineCache::RenderPipelineCache(RenderPipelineCache&& other)
    : device(std::move(other.device)),
      desc(std::move(other.desc)),
      _id(std::move(other._id)),
      specializedPipelines(std::move(other.specializedPipelines)) {}
EPIX_API PipelineServer::RenderPipelineCache& PipelineServer::RenderPipelineCache::operator=(
    RenderPipelineCache&& other) {
    if (this != &other) {
        device               = std::move(other.device);
        desc                 = std::move(other.desc);
        _id                  = std::move(other._id);
        specializedPipelines = std::move(other.specializedPipelines);
    }
    return *this;
}

EPIX_API RenderPipelineId PipelineServer::RenderPipelineCache::id() const { return _id; }
EPIX_API size_t PipelineServer::RenderPipelineCache::specialize(const nvrhi::FramebufferInfo& info) const {
    std::lock_guard lock(_mutex);
    size_t index = 0;
    for (; index < specializedPipelines.size(); index++) {
        if (specializedPipelines[index].first == info) {
            return index;
        }
    }

    nvrhi::GraphicsPipelineHandle pipeline = device->createGraphicsPipeline(desc, info);
    if (!pipeline) {
        spdlog::error("Failed to create pipeline for framebuffer info");
        return -1;  // Indicate failure
    }
    specializedPipelines.emplace_back(info, pipeline);
    return specializedPipelines.size() - 1;
}
EPIX_API nvrhi::GraphicsPipelineHandle PipelineServer::RenderPipelineCache::get(size_t index) const {
    std::lock_guard lock(_mutex);
    if (index < specializedPipelines.size()) {
        return specializedPipelines[index].second;
    }
    return nullptr;
}
EPIX_API std::tuple<size_t, nvrhi::GraphicsPipelineHandle> PipelineServer::RenderPipelineCache::get(
    const nvrhi::FramebufferInfo& info) const {
    size_t index = specialize(info);
    return {index, get(index)};
}

EPIX_API PipelineServer::PipelineServer(nvrhi::DeviceHandle device) : device(device) {}

EPIX_API PipelineServer PipelineServer::from_world(epix::World& world) {
    return PipelineServer{world.resource<nvrhi::DeviceHandle>()};
}
EPIX_API RenderPipelineId PipelineServer::queue_render_pipeline(const RenderPipelineDesc& desc) {
    RenderPipelineId id{renderPipelines.size()};
    renderPipelines.emplace_back(desc);
    queuedPipelines.emplace_back(id);
    return id;
}
EPIX_API ComputePipelineId PipelineServer::queue_compute_pipeline(const ComputePipelineDesc& desc) {
    ComputePipelineId id{computePipelines.size()};
    computePipelines.emplace_back(desc);
    queuedPipelines.emplace_back(id);
    return id;
}
EPIX_API std::optional<RenderPipeline> PipelineServer::get_render_pipeline(RenderPipelineId id,
                                                                           const nvrhi::FramebufferInfo& info) const {
    if (id.id < renderPipelines.size()) {
        auto& pipeline = renderPipelines[id];
        if (std::holds_alternative<RenderPipelineCache>(pipeline)) {
            auto& cache             = std::get<RenderPipelineCache>(pipeline);
            size_t specializedIndex = cache.specialize(info);
            return RenderPipeline{id, specializedIndex, cache.get(specializedIndex)};
        }
    }
    return std::nullopt;
}
EPIX_API std::optional<ComputePipeline> PipelineServer::get_compute_pipeline(ComputePipelineId id) const {
    if (id.id < computePipelines.size()) {
        auto& pipeline = computePipelines[id];
        if (std::holds_alternative<ComputePipeline>(pipeline)) {
            return std::get<ComputePipeline>(pipeline);
        }
    }
    return std::nullopt;
}

EPIX_API void PipelineServer::process_queued(ResMut<PipelineServer> server, Res<assets::RenderAssets<Shader>> shaders) {
    size_t count = server->queuedPipelines.size();
    while (count != 0) {
        count--;
        auto id = std::move(server->queuedPipelines.front());
        server->queuedPipelines.pop_front();
        auto&& device = server->device;
        std::visit(util::visitor{
                       [&](const RenderPipelineId& id) {
                           auto& renderDesc = std::get<RenderPipelineDesc>(server->renderPipelines[id]);
                           if (!renderDesc.VS) {
                               if (!renderDesc.vertexShader.shader) {
                                   spdlog::error(
                                       "[pipeline-server] "
                                       "RenderPipeline with id={} has no vertex "
                                       "shader provided",
                                       id.id);
                                   server->renderPipelines[id] =
                                       std::make_pair(renderDesc, PipelineError{"No vertex shader provided"});
                                   return;
                               } else {
                                   if (auto vs = shaders->try_get(renderDesc.vertexShader.shader)) {
                                       renderDesc.VS = vs->create_shader(device, nvrhi::ShaderType::Vertex,
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
                                   server->renderPipelines[id] =
                                       std::make_pair(renderDesc, PipelineError{"No fragment shader provided"});
                                   return;
                               } else {
                                   if (auto ps = shaders->try_get(renderDesc.fragmentShader.shader)) {
                                       renderDesc.PS = ps->create_shader(device, nvrhi::ShaderType::Pixel,
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
                           if (!renderDesc.GS && renderDesc.geometryShader.shader) {
                               if (auto gs = shaders->try_get(renderDesc.geometryShader.shader)) {
                                   renderDesc.GS = gs->create_shader(device, nvrhi::ShaderType::Geometry,
                                                                     renderDesc.geometryShader.entryName,
                                                                     renderDesc.geometryShader.debugName);
                               } else {
                                   // shader not extracted yet, retry next
                                   // frame
                                   server->queuedPipelines.emplace_back(id);
                                   return;
                               }
                           }
                           if (!renderDesc.HS && renderDesc.tessellationControlShader.shader) {
                               if (auto hs = shaders->try_get(renderDesc.tessellationControlShader.shader)) {
                                   renderDesc.HS = hs->create_shader(device, nvrhi::ShaderType::Hull,
                                                                     renderDesc.tessellationControlShader.entryName,
                                                                     renderDesc.tessellationControlShader.debugName);
                               } else {
                                   // shader not extracted yet, retry next
                                   // frame
                                   server->queuedPipelines.emplace_back(id);
                                   return;
                               }
                           }
                           if (!renderDesc.DS && renderDesc.tessellationEvaluationShader.shader) {
                               if (auto ds = shaders->try_get(renderDesc.tessellationEvaluationShader.shader)) {
                                   renderDesc.DS = ds->create_shader(device, nvrhi::ShaderType::Domain,
                                                                     renderDesc.tessellationEvaluationShader.entryName,
                                                                     renderDesc.tessellationEvaluationShader.debugName);
                               } else {
                                   // shader not extracted yet, retry next
                                   // frame
                                   server->queuedPipelines.emplace_back(id);
                                   return;
                               }
                           }
                           if (renderDesc.bindingLayouts.empty() && !renderDesc.bindingLayoutDescs.empty()) {
                               renderDesc.bindingLayouts = renderDesc.bindingLayoutDescs |
                                                           std::views::transform([&](auto&& desc) {
                                                               return device->createBindingLayout(desc);
                                                           }) |
                                                           std::ranges::to<nvrhi::BindingLayoutVector>();
                           }
                           server->renderPipelines[id] = RenderPipelineCache(device, id, renderDesc);
                       },
                       [&](const ComputePipelineId& id) {
                           auto& computeDesc = std::get<ComputePipelineDesc>(server->computePipelines[id]);
                           if (!computeDesc.CS && !computeDesc.computeShader.shader) {
                               spdlog::error(
                                   "[pipeline-server] "
                                   "ComputePipeline with id={} has no compute "
                                   "shader provided",
                                   id.id);
                               server->computePipelines[id] =
                                   std::make_pair(computeDesc, PipelineError{"No compute shader provided"});
                               return;
                           }
                           if (!computeDesc.CS) {
                               if (auto cs = shaders->try_get(computeDesc.computeShader.shader)) {
                                   computeDesc.CS = cs->create_shader(device, nvrhi::ShaderType::Compute,
                                                                      computeDesc.computeShader.entryName,
                                                                      computeDesc.computeShader.debugName);
                               } else {
                                   // shader not extracted yet, retry next frame
                                   server->queuedPipelines.emplace_back(id);
                                   return;
                               }
                           }
                           auto pipeline                = device->createComputePipeline(computeDesc);
                           server->computePipelines[id] = ComputePipeline{id, pipeline};
                       },
                   },
                   id);
    }
}

EPIX_API void PipelineServerPlugin::build(epix::App& app) {
    if (auto renderApp = app.get_sub_app(Render)) {
        renderApp->init_resource<PipelineServer>();
        renderApp->add_systems(Render, into(PipelineServer::process_queued)
                                           .set_name("process queued pipelines")
                                           .in_set(RenderSet::PrepareResources));
    }
}