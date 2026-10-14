#include <epix/pbr/material.hpp>

#include <utility>

namespace epix::pbr {

assets::UntypedAssetId RenderMaterialInstances::mesh_material(render::sync_world::MainEntity entity) const {
    const auto found = instances.find(entity);
    if (found == instances.end()) return DUMMY_MESH_MATERIAL;
    return found->second.asset_id;
}

std::optional<MaterialBindingId> RenderMaterialBindings::get(const assets::UntypedAssetId& asset_id) const {
    const auto found = bindings.find(asset_id);
    if (found == bindings.end()) return std::nullopt;
    return found->second;
}

void late_sweep_material_instances(ecs::ResMut<RenderMaterialInstances> material_instances,
                                   app::Extract<ecs::RemovedComponents<mesh::Mesh3d>> removed_meshes_query) {
    const ecs::Tick last_change_tick = material_instances->current_change_tick;

    for (const ecs::Entity entity : removed_meshes_query.read()) {
        const render::sync_world::MainEntity main_entity{entity};
        const auto found = material_instances->instances.find(main_entity);
        if (found != material_instances->instances.end() && found->second.last_change_tick != last_change_tick) {
            material_instances->instances.erase(found);
        }
    }

    material_instances->current_change_tick = ecs::Tick{last_change_tick.get() + 1};
}

void init_material_pipeline(ecs::Commands commands, ecs::Res<MeshPipeline> mesh_pipeline) {
    commands.insert_resource(MaterialPipeline{.mesh_pipeline = *mesh_pipeline});
}

void prepare_material_bind_groups(ecs::ResMut<MaterialBindGroupAllocators> allocators,
                                  ecs::Res<wgpu::Device> render_device, ecs::Res<wgpu::Queue> render_queue,
                                  ecs::Res<render::PipelineServer> pipeline_server,
                                  ecs::Res<render::texture::FallbackImage> fallback_image,
                                  ecs::Res<FallbackBindlessResources> fallback_resources) {
    // Bevy `prepare_material_bind_groups` (`material.rs:1907-1922`).
    for (auto& [type, allocator] : allocators->allocators) {
        (void)type;
        allocator.prepare_bind_groups(*render_device, *render_queue, *pipeline_server, *fallback_resources,
                                      *fallback_image);
    }
}

void write_material_bind_group_buffers(ecs::ResMut<MaterialBindGroupAllocators> allocators,
                                       ecs::Res<wgpu::Device> render_device, ecs::Res<wgpu::Queue> render_queue) {
    // Bevy `write_material_bind_group_buffers` (`material.rs:1929-1937`).
    for (auto& [type, allocator] : allocators->allocators) {
        (void)type;
        allocator.write_buffers(*render_device, *render_queue);
    }
}

void MaterialsPlugin::attach(epix::app::App& app) {
    // Bevy `MaterialsPlugin::build` (`material.rs:281-335`), as far as the port has it.
    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) return;
    auto& render = render_app->get();

    // `init_resource` rather than `insert_resource`: one registry serves every material
    // type, so a second material plugin must not wipe the first's contents.
    render.world_mut().init_resource<RenderMaterialInstances>();
    render.world_mut().init_resource<MaterialBindGroupAllocators>();
    render.world_mut().init_resource<EntitySpecializationTicks>();
    render.world_mut().init_resource<SpecializedMaterialPipelineCache>();
    render.world_mut().init_resource<render::SpecializedMeshPipelines<MaterialPipelineSpecializer>>();

    // Bevy registers the material-pipeline resource in `RenderStartup`, before anything
    // specializes against it (`material.rs:300`).
    render.add_systems(render::RenderStartup,
                       ecs::into(init_material_pipeline).set_name("init material pipeline"));

    // Bevy chains the pair and puts both in `PrepareBindGroups` (`material.rs:312-320`).
    render.add_systems(render::Render, ecs::into(prepare_material_bind_groups)
                                           .in_set(render::RenderSystems::PrepareBindGroups)
                                           .set_name("prepare material bind groups"));
    render.add_systems(render::Render, ecs::into(write_material_bind_group_buffers)
                                           .after(prepare_material_bind_groups)
                                           .in_set(render::RenderSystems::PrepareBindGroups)
                                           .set_name("write material bind group buffers"));
}

std::expected<render::RenderPipelineDescriptor, render::SpecializedMeshPipelineError>
MaterialPipelineSpecializer::specialize(const Key& key, const mesh::MeshVertexBufferLayoutRef& layout) const {
    // Bevy `SpecializedMeshPipeline for MaterialPipelineSpecializer`
    // (`material.rs:456-503`), in its order: the mesh pipeline first, then the two
    // shader defs, then the material's shader overrides, the bind group layout and
    // finally the bindless define.
    auto descriptor = pipeline.mesh_pipeline.specialize(key.mesh_key, layout);
    if (!descriptor) return std::unexpected(render::SpecializedMeshPipelineError{descriptor.error()});

    // The material's shader reads its bind group from this index, so the def and the
    // layout insertion below have to agree on it - which is why both use the port's
    // own constant rather than a literal.
    const shader::ShaderDefVal material_bind_group =
        shader::ShaderDefVal::from_uint("MATERIAL_BIND_GROUP", MATERIAL_BIND_GROUP_INDEX);
    descriptor->vertex.shader_defs.push_back(material_bind_group);
    if (descriptor->fragment) descriptor->fragment->shader_defs.push_back(material_bind_group);

    if (const auto* vertex_shader = properties->get_shader<MaterialVertexShader>()) {
        descriptor->vertex.shader = *vertex_shader;
    }
    if (const auto* fragment_shader = properties->get_shader<MaterialFragmentShader>()) {
        if (descriptor->fragment) descriptor->fragment->shader = *fragment_shader;
    }

    if (properties->material_layout) {
        descriptor->layouts.insert(descriptor->layouts.begin() + MATERIAL_BIND_GROUP_INDEX,
                                   *properties->material_layout);
    }

    if (properties->bindless) {
        const shader::ShaderDefVal bindless{std::string{"BINDLESS"}};
        descriptor->vertex.shader_defs.push_back(bindless);
        if (descriptor->fragment) descriptor->fragment->shader_defs.push_back(bindless);
    }

    return descriptor;
}

}  // namespace epix::pbr
