module;

#include <spdlog/spdlog.h>

module epix.render;

import :pipeline_server;

namespace render {
std::optional<wgpu::ShaderModule> load_module(const wgpu::Device& device, const Shader& shader) {
    const auto& source = shader.source;
    if (source.is_wgsl()) {
        auto wgsl_code = source.get_wgsl();
        if (!wgsl_code) {
            return std::nullopt;
        }
        wgpu::ShaderModuleDescriptor desc;
        if (!shader.label.empty()) {
            desc.setLabel(wgpu::StringView(std::string_view(shader.label)));
        }
        desc.setNextInChain(wgpu::ShaderSourceWGSL().setCode(*wgsl_code));
        return device.createShaderModule(desc);
    } else if (source.is_spirv()) {
        auto spirv_code = source.get_spirv();
        if (!spirv_code) {
            return std::nullopt;
        }
        wgpu::ShaderModuleDescriptor desc;
        if (!shader.label.empty()) {
            desc.setLabel(wgpu::StringView(std::string_view(shader.label)));
        }
        desc.setNextInChain(wgpu::ShaderSourceSPIRV().setCode(spirv_code->data()).setCodeSize(spirv_code->size()));
        return device.createShaderModule(desc);
    }
    return std::nullopt;
}

PipelineServer::PipelineServer(wgpu::Device device)
    : layout_cache(std::make_shared<utils::Mutex<LayoutCache>>()),
      shader_cache(std::make_shared<utils::Mutex<ShaderCache>>(load_module)),
      device(std::move(device)),
      pipeline_create_task_pool(std::make_unique<BS::thread_pool<BS::tp::none>>(std::thread::hardware_concurrency())) {}

auto PipelineServer::get_pipeline_state(CachedPipelineId id) const
    -> std::optional<std::reference_wrapper<const CachedPipelineState>> {
    if (pipelines.size() <= id.get()) {
        return std::nullopt;
    }
    return std::cref(pipelines[id].state);
}
auto PipelineServer::get_render_pipeline_descriptor(CachedPipelineId id) const
    -> std::optional<std::reference_wrapper<const RenderPipelineDescriptor>> {
    if (pipelines.size() <= id.get()) {
        return std::nullopt;
    }
    if (std::holds_alternative<RenderPipelineDescriptor>(pipelines[id].descriptor)) {
        return std::cref(std::get<RenderPipelineDescriptor>(pipelines[id].descriptor));
    }
    return std::nullopt;
}
auto PipelineServer::get_compute_pipeline_descriptor(CachedPipelineId id) const
    -> std::optional<std::reference_wrapper<const ComputePipelineDescriptor>> {
    if (pipelines.size() <= id.get()) {
        return std::nullopt;
    }
    if (std::holds_alternative<ComputePipelineDescriptor>(pipelines[id].descriptor)) {
        return std::cref(std::get<ComputePipelineDescriptor>(pipelines[id].descriptor));
    }
    return std::nullopt;
}
auto PipelineServer::get_render_pipeline(CachedPipelineId id) const
    -> std::expected<std::reference_wrapper<const RenderPipeline>, GetPipelineError> {
    if (pipelines.size() <= id.get()) {
        return std::unexpected(GetPipelineInvalidId{});
    }
    const auto& state = pipelines[id].state;
    if (std::holds_alternative<Pipeline>(state)) {
        const Pipeline& pipeline = std::get<Pipeline>(state);
        if (std::holds_alternative<RenderPipeline>(pipeline)) {
            return std::cref(std::get<RenderPipeline>(pipeline));
        }
        return std::unexpected(PipelineServerError{PipelineError::CreationFailure});  // wrong pipeline type
    }
    if (std::holds_alternative<PipelineServerError>(state)) {
        return std::unexpected(std::get<PipelineServerError>(state));
    }
    return std::unexpected(GetPipelineNotReady{});
}
auto PipelineServer::get_compute_pipeline(CachedPipelineId id) const
    -> std::expected<std::reference_wrapper<const ComputePipeline>, GetPipelineError> {
    if (pipelines.size() <= id.get()) {
        return std::unexpected(GetPipelineInvalidId{});
    }
    const auto& state = pipelines[id].state;
    if (std::holds_alternative<Pipeline>(state)) {
        const Pipeline& pipeline = std::get<Pipeline>(state);
        if (std::holds_alternative<ComputePipeline>(pipeline)) {
            return std::cref(std::get<ComputePipeline>(pipeline));
        }
        return std::unexpected(PipelineServerError{PipelineError::CreationFailure});
    }
    if (std::holds_alternative<PipelineServerError>(state)) {
        return std::unexpected(std::get<PipelineServerError>(state));
    }
    return std::unexpected(GetPipelineNotReady{});
}
CachedPipelineId PipelineServer::queue_render_pipeline(RenderPipelineDescriptor descriptor) const {
    auto new_pipelines  = this->new_pipelines.lock();
    CachedPipelineId id = static_cast<CachedPipelineId>(pipelines.size() + new_pipelines->size());
    new_pipelines->push_back(CachedPipeline{std::move(descriptor), PipelineStateQueued{}});
    return id;
}
CachedPipelineId PipelineServer::queue_compute_pipeline(ComputePipelineDescriptor descriptor) const {
    auto new_pipelines  = this->new_pipelines.lock();
    CachedPipelineId id = static_cast<CachedPipelineId>(pipelines.size() + new_pipelines->size());
    new_pipelines->push_back(CachedPipeline{std::move(descriptor), PipelineStateQueued{}});
    return id;
}
void PipelineServer::set_shader(assets::AssetId<Shader> id, Shader shader) {
    auto shader_cache       = this->shader_cache->lock();
    auto affected_pipelines = shader_cache->set_shader(id, std::move(shader));
    for (CachedPipelineId pipeline_id : affected_pipelines) {
        pipelines[pipeline_id].state = PipelineStateQueued{};
        waiting_pipelines.insert(pipeline_id);
    }
}
void PipelineServer::remove_shader(assets::AssetId<Shader> id) {
    auto shader_cache       = this->shader_cache->lock();
    auto affected_pipelines = shader_cache->remove(id);
    for (CachedPipelineId pipeline_id : affected_pipelines) {
        pipelines[pipeline_id].state = PipelineStateQueued{};
        waiting_pipelines.insert(pipeline_id);
    }
}

void PipelineServer::process_queue() {
    auto waiting_pipelines = std::move(this->waiting_pipelines);
    {
        auto new_pipelines = std::move(*this->new_pipelines.lock());
        for (auto&& pipeline : new_pipelines) {
            CachedPipelineId id = static_cast<CachedPipelineId>(pipelines.size());
            pipelines.push_back(std::move(pipeline));
            waiting_pipelines.insert(id);
        }
    }
    for (auto id : waiting_pipelines) {
        // processing pipeline
        process_pipeline(pipelines[id], id);
    }
}

void PipelineServer::process_pipeline(CachedPipeline& cached_pipeline, CachedPipelineId id) {
    auto create_render_pipeline = [&](const RenderPipelineDescriptor& descriptor) mutable {
        cached_pipeline.state = PipelineStateCreating{
            pipeline_create_task_pool->submit_task([device = device, descriptor, layout_cache_ptr = layout_cache,
                                                    shader_cache_ptr = shader_cache,
                                                    id]() -> std::expected<Pipeline, PipelineServerError> {
                wgpu::RenderPipelineDescriptor pipelineDesc;
                wgpu::ShaderModule vertex_module;
                std::optional<wgpu::ShaderModule> fragment_module;
                wgpu::PipelineLayout layout;
                {
                    auto shader_cache = shader_cache_ptr->lock();
                    auto layout_cache = layout_cache_ptr->lock();
                    auto vertex_opt   = shader_cache->get(device, id, descriptor.vertex.shader);
                    if (!vertex_opt) return std::unexpected(vertex_opt.error());
                    vertex_module = vertex_opt->get();
                    if (descriptor.fragment) {
                        auto fragment_opt = shader_cache->get(device, id, descriptor.fragment->shader);
                        if (!fragment_opt) return std::unexpected(fragment_opt.error());
                        fragment_module = fragment_opt->get();
                    }
                    if (!descriptor.layouts.empty()) layout = layout_cache->get(device, descriptor.layouts);
                }
                pipelineDesc.setLabel(std::string_view(descriptor.label));
                pipelineDesc.setLayout(layout);
                pipelineDesc.setVertex(wgpu::VertexState()
                                           .setModule(vertex_module)
                                           .setBuffers(descriptor.vertex.buffers)
                                           .setEntryPoint(descriptor.vertex.entry_point
                                                              .transform([](auto&& s) { return std::string_view(s); })
                                                              .value_or(std::string_view("main"))));
                pipelineDesc.setPrimitive(descriptor.primitive);
                if (descriptor.depth_stencil) pipelineDesc.setDepthStencil(*descriptor.depth_stencil);
                pipelineDesc.setMultisample(descriptor.multisample);
                if (descriptor.fragment)
                    pipelineDesc.setFragment(
                        wgpu::FragmentState()
                            .setModule(*fragment_module)
                            .setTargets(descriptor.fragment->targets)
                            .setEntryPoint(
                                descriptor.fragment->entry_point.transform([](auto&& s) { return std::string_view(s); })
                                    .value_or(std::string_view("main"))));
                auto pipeline = device.createRenderPipeline(pipelineDesc);
                if (!pipeline) return std::unexpected(PipelineError::CreationFailure);
                return Pipeline{RenderPipeline(std::move(pipeline))};
            }),
        };
    };
    auto create_compute_pipeline = [&](const ComputePipelineDescriptor& descriptor) mutable {
        cached_pipeline.state = PipelineStateCreating{
            pipeline_create_task_pool->submit_task([device = device, descriptor, layout_cache_ptr = layout_cache,
                                                    shader_cache_ptr = shader_cache,
                                                    id]() -> std::expected<Pipeline, PipelineServerError> {
                wgpu::ComputePipelineDescriptor desc;
                wgpu::PipelineLayout layout;
                wgpu::ShaderModule module;
                {
                    auto layout_cache = layout_cache_ptr->lock();
                    auto shader_cache = shader_cache_ptr->lock();
                    auto shader_opt   = shader_cache->get(device, id, descriptor.shader);
                    if (!shader_opt) return std::unexpected(shader_opt.error());
                    module = shader_opt->get();
                    if (!descriptor.layouts.empty()) layout = layout_cache->get(device, descriptor.layouts);
                }
                desc.setLabel(std::string_view(descriptor.label))
                    .setLayout(layout)
                    .setCompute(wgpu::ProgrammableStageDescriptor().setModule(module).setEntryPoint(
                        descriptor.entry_point.transform([](auto&& s) { return std::string_view(s); })
                            .value_or(std::string_view("main"))));
                auto pipeline = device.createComputePipeline(desc);
                if (!pipeline) return std::unexpected(PipelineError::CreationFailure);
                return Pipeline{ComputePipeline(std::move(pipeline))};
            }),
        };
    };
    auto pipeline_name         = std::visit(utils::visitor{
                                        [](const RenderPipelineDescriptor& desc) { return desc.label; },
                                        [](const ComputePipelineDescriptor& desc) { return desc.label; },
                                    },
                                            cached_pipeline.descriptor);
    auto handle_pipeline_error = [&](const PipelineServerError& error) {
        if (auto pipeline_error = std::get_if<PipelineError>(&error)) {
            switch (*pipeline_error) {
                case PipelineError::CreationFailure: {
                    spdlog::error("Failed to create pipeline. Id: {}, name: {}", id.get(), pipeline_name);
                    waiting_pipelines.insert(id);
                    break;
                }
            }
            return;
        }

        if (auto shader_error = std::get_if<ShaderCacheError>(&error)) {
            switch (*shader_error) {
                case ShaderCacheError::NotLoaded:
                    // Not loaded yet, retry
                    cached_pipeline.state = PipelineStateQueued{};
                    break;
                case ShaderCacheError::ModuleCreationFailure:
                    spdlog::error("Failed to create shader module. Pipeline Id: {}, name: {}", id.get(), pipeline_name);
                    break;
            }
        }
    };

    if (std::holds_alternative<PipelineStateQueued>(cached_pipeline.state)) {
        std::visit(utils::visitor{create_render_pipeline, create_compute_pipeline}, cached_pipeline.descriptor);
    }

    if (auto* creating_state = std::get_if<PipelineStateCreating>(&cached_pipeline.state)) {
        if (creating_state->wait_for(std::chrono::seconds(0)) != std::future_status::timeout) {
            auto result = creating_state->get();
            if (result) {
                cached_pipeline.state = std::move(result.value());
                return;
            }
            cached_pipeline.state = result.error();
        }
    }

    if (auto* error = std::get_if<PipelineServerError>(&cached_pipeline.state)) {
        if (auto pipeline_error = std::get_if<PipelineError>(error)) {
            switch (*pipeline_error) {
                case PipelineError::CreationFailure: {
                    spdlog::error("Failed to create pipeline. Id: {}, name: {}", id.get(), pipeline_name);
                    waiting_pipelines.insert(id);
                    break;
                }
            }
            return;
        }

        if (auto shader_error = std::get_if<ShaderCacheError>(error)) {
            switch (*shader_error) {
                case ShaderCacheError::NotLoaded:
                    // Not loaded yet, retry
                    break;
                case ShaderCacheError::ModuleCreationFailure:
                    spdlog::error("Failed to create shader module. Pipeline Id: {}, name: {}", id.get(), pipeline_name);
                    return;
                    break;
            }
        }
    }

    waiting_pipelines.insert(id);
}
void PipelineServer::process_pipeline_system(ResMut<PipelineServer> pipeline_server) {
    pipeline_server->process_queue();
}
void PipelineServer::extract_shaders(ResMut<PipelineServer> pipeline_server,
                                     Extract<Res<assets::Assets<Shader>>> shaders,
                                     Extract<EventReader<assets::AssetEvent<Shader>>> shader_events) {
    for (const auto& event : shader_events.read()) {
        if (event.is_added() || event.is_modified()) {
            if (auto shader = shaders->try_get(event.id)) {
                pipeline_server->set_shader(event.id, *shader);
            }
        } else if (event.is_removed()) {
            pipeline_server->remove_shader(event.id);
        }
    }
}
}  // namespace render