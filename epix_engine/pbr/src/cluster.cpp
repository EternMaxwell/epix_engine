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

}  // namespace epix::pbr
