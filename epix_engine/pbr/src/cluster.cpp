#include <epix/pbr/cluster_prepare.hpp>
#include <utility>

namespace epix::pbr {

void extract_clusters(
    ecs::Commands commands,
    app::Extract<ecs::Query<
        ecs::Item<const ::epix::render::sync_world::RenderEntity&, const Clusters&, const ::epix::camera::Camera&>>>
        views,
    app::Extract<ecs::Query<ecs::Item<const ::epix::render::sync_world::RenderEntity&>>> mapper) {
    for (auto&& [render_entity, clusters, camera] : views.iter()) {
        auto entity_commands = commands.entity(render_entity.id());
        if (!camera.is_active) {
            entity_commands.template remove<ExtractedClusterableObjects, ExtractedClusterConfig>();
            continue;
        }

        auto extracted = extract_view_clusters(clusters, [&mapper](ecs::Entity entity, ecs::Entity& render) {
            const auto row = mapper.get(entity);
            if (!row.has_value()) return false;  // not synced: Bevy skips it
            render = std::get<0>(*row).id();
            return true;
        });

        entity_commands.insert(std::move(extracted.objects), std::move(extracted.config));
    }
}

std::uint32_t pack_offset_and_counts(std::size_t offset,
                                                        std::uint32_t point_count,
                                                        std::uint32_t spot_count) {
    return ((static_cast<std::uint32_t>(offset) & CLUSTER_OFFSET_MASK) << (CLUSTER_COUNT_SIZE * 2)) |
           ((point_count & CLUSTER_COUNT_MASK) << CLUSTER_COUNT_SIZE) | (spot_count & CLUSTER_COUNT_MASK);
}

GpuClusterOffsetsAndCounts pack_cluster_offsets_and_counts_storage(
    std::size_t offset, const ClusterableObjectCounts& counts) {
    return GpuClusterOffsetsAndCounts{
        glm::uvec4(static_cast<std::uint32_t>(offset), counts.point_lights, counts.spot_lights,
                   counts.reflection_probes),
        glm::uvec4(counts.irradiance_volumes, counts.decals, 0u, 0u),
    };
}

std::uint32_t pack_index_into_uniform_word(std::uint32_t word,
                                                              std::size_t n_indices,
                                                              std::uint32_t index) {
    return word | (index << (8 * uniform_index_lists_sub_index(n_indices)));
}

ViewClusterBindings prepare_view_cluster_bindings(
    const std::vector<ExtractedClusterableObjectElement>& data,
    const std::unordered_map<ecs::Entity, std::size_t>& entity_to_index,
    wgpu::BufferBindingType binding_type,
    bool supports_storage_buffers) {
    ViewClusterBindings bindings{binding_type};
    bindings.clear();

    for (const auto& record : data) {
        if (record.kind == ExtractedClusterableObjectElement::Kind::ClusterHeader) {
            bindings.push_offset_and_counts(bindings.indices_count(), record.counts);
            continue;
        }
        const auto found = entity_to_index.find(record.entity);
        if (found == entity_to_index.end()) continue;
        if (bindings.indices_count() >= VIEW_CLUSTER_MAX_INDICES && !supports_storage_buffers) {
            // Bevy warns here, once per view that overflows (cluster.rs:317-325).
            spdlog::warn(
                "[pbr] Clusterable object index lists are full! The clusterable objects in the view are present in too "
                "many clusters.");
            break;
        }
        bindings.push_index(found->second);
    }
    return bindings;
}

void prepare_clusters(ecs::Commands commands, ecs::Res<wgpu::Device> render_device,
                      ecs::Res<wgpu::Queue> render_queue, ecs::Res<MeshPipeline> mesh_pipeline,
                      ecs::Res<GlobalClusterableObjectMeta> global_clusterable_object_meta,
                      ecs::Query<ecs::Item<ecs::Entity, const ExtractedClusterableObjects&>> views) {
    // Whether the index lists may grow past `MAX_INDICES` is a property of the
    // pipeline's binding type, which is why Bevy reads it from `MeshPipeline`
    // rather than from the view.
    const wgpu::BufferBindingType binding_type = mesh_pipeline->clustered_forward_buffer_binding_type;
    const bool supports_storage_buffers = binding_type == wgpu::BufferBindingType::eStorage ||
                                          binding_type == wgpu::BufferBindingType::eReadOnlyStorage;

    for (auto&& [entity, extracted_clusters] : views.iter()) {
        ViewClusterBindings view_clusters_bindings =
            prepare_view_cluster_bindings(extracted_clusters.data, global_clusterable_object_meta->entity_to_index,
                                          binding_type, supports_storage_buffers);
        view_clusters_bindings.write_buffers(*render_device, *render_queue);
        commands.entity(entity).insert(std::move(view_clusters_bindings));
    }
}

void init_global_clusterable_object_meta(ecs::Commands commands, ecs::Res<wgpu::Device> render_device) {
    // Bevy `init_global_clusterable_object_meta` (`cluster.rs:153-167`): the binding type is
    // the one the device can offer for the clustered-forward array, which is the same value
    // the mesh pipeline is built with - the cluster buffers and the bind group have to agree
    // on it. `read_only_binding_type` is Bevy's `RenderDevice::get_supported_read_only_binding_type`,
    // spelled here the way the mesh pipeline and the prepass spell it.
    wgpu::Limits limits{};
    render_device->getLimits(&limits);
    const auto read_only_binding_type = [&limits](std::uint32_t buffers_per_shader_stage) {
        return limits.maxStorageBuffersPerShaderStage >= buffers_per_shader_stage
                   ? wgpu::BufferBindingType::eReadOnlyStorage
                   : wgpu::BufferBindingType::eUniform;
    };
    commands.insert_resource(
        GlobalClusterableObjectMeta(read_only_binding_type(CLUSTERED_FORWARD_STORAGE_BUFFER_COUNT)));
}

}  // namespace epix::pbr
