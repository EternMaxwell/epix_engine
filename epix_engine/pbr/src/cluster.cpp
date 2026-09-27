#include <epix/pbr/cluster.hpp>
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

}  // namespace epix::pbr
