#include <spdlog/spdlog.h>

#include <epix/render.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::render;

namespace epix::render::sync_world {

void SyncToRenderWorld::on_add(World& world, HookContext context) {
    if (auto pending = world.get_resource_mut<PendingSyncEntity>()) {
        pending->get().records.emplace_back(EntityAdded{context.entity});
    }
}

void SyncToRenderWorld::on_remove(World& world, HookContext context) {
    auto render_entity = world.get_entity(context.entity).and_then([](const EntityRef& entity) {
        return entity.get<RenderEntity>().transform(
            [](const std::reference_wrapper<const RenderEntity>& value) { return value.get(); });
    });
    if (render_entity) {
        if (auto pending = world.get_resource_mut<PendingSyncEntity>()) {
            pending->get().records.emplace_back(EntityRemoved{*render_entity});
        }
    }
}

void record_component_removed(World& world, HookContext context) {
    if (auto pending = world.get_resource_mut<PendingSyncEntity>()) {
        pending->get().records.emplace_back(ComponentRemoved{context.entity});
    }
}

void entity_sync_system(World& main_world, World& render_world) {
    std::vector<EntityRecord> records;
    if (auto pending = main_world.get_resource_mut<PendingSyncEntity>()) {
        records = std::move(pending->get().records);
    } else {
        return;
    }

    // Bevy sync_world.rs:220-252: process exactly the lifecycle changes
    // observed since the preceding extraction, without a world-wide scan.
    for (const auto& record : records) {
        std::visit(
            [&]<typename T>(const T& value) {
                if constexpr (std::same_as<T, EntityAdded>) {
                    if (!main_world.get_entity(value.entity)) return;
                    if (main_world.get_entity(value.entity)->contains<RenderEntity>()) {
                        throw std::logic_error("attempting to synchronize an entity that is already synchronized");
                    }
                    const Entity render_entity = render_world.spawn(MainEntity{value.entity}).id();
                    main_world.get_entity_mut(value.entity).transform([&](EntityWorldMut&& entity) -> int {
                        entity.insert(RenderEntity{render_entity});
                        return 0;
                    });
                } else if constexpr (std::same_as<T, EntityRemoved>) {
                    render_world.get_entity_mut(value.entity.id()).transform([](EntityWorldMut&& entity) -> int {
                        entity.despawn();
                        return 0;
                    });
                } else {
                    auto current_render = main_world.get_entity(value.entity).and_then([](const EntityRef& entity) {
                        return entity.get<RenderEntity>().transform(
                            [](const std::reference_wrapper<const RenderEntity>& render) { return render.get().id(); });
                    });
                    if (!current_render) return;
                    render_world.get_entity_mut(*current_render).transform([](EntityWorldMut&& entity) -> int {
                        entity.despawn();
                        return 0;
                    });
                    const Entity replacement = render_world.spawn(MainEntity{value.entity}).id();
                    main_world.get_entity_mut(value.entity).transform([&](EntityWorldMut&& entity) -> int {
                        entity.get_mut<RenderEntity>()->get_mut() = RenderEntity{replacement};
                        return 0;
                    });
                }
            },
            record);
    }
}

void remove_temporary_render_entities(World& world) {
    world.registrator().register_component<TemporaryRenderEntity>();
    auto query = world.try_query<Item<Entity, const TemporaryRenderEntity&>>();
    if (!query) {
        return;
    }
    std::vector<Entity> entities;
    for (auto&& [entity, tmp] : query->iter(world)) {
        (void)tmp;
        entities.push_back(entity);
    }
    // Bevy sorts by entity index ascending and despawns in reverse so the next
    // frame's allocations keep order (sync_world.rs:256-270).
    std::ranges::sort(entities, [](Entity a, Entity b) { return a.index < b.index; });
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        world.get_entity_mut(*it).transform([](EntityWorldMut&& ew) -> int {
            ew.despawn();
            return 0;
        });
    }
}

}  // namespace epix::render::sync_world

void epix::render::sync_world::SyncWorldPlugin::attach(App& app) {
    spdlog::debug("[render.sync_world] Attaching SyncWorldPlugin.");
    // Bevy SyncWorldPlugin::build (sync_world.rs:94): pending sync records live
    // in the main world so entity_sync_system can react to component removals.
    app.world_mut().init_resource<PendingSyncEntity>();
    if (auto render_app = app.get_sub_app_mut(Render)) {
        render_app->get().add_systems(Render,
                                      into(remove_temporary_render_entities).in_set(RenderSystems::PostCleanup));
    }
}
