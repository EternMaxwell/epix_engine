#include <spdlog/spdlog.h>

#include <algorithm>
#include <epix/render.hpp>
#include <utility>
#include <vector>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::render;

namespace epix::render::sync_world {

void entity_sync_system(World& main_world, World& render_world) {
    // Ensure the sync components are registered in both worlds (idempotent).
    main_world.registrator().register_component<SyncToRenderWorld>();
    main_world.registrator().register_component<RenderEntity>();
    render_world.registrator().register_component<SyncToRenderWorld>();
    render_world.registrator().register_component<RenderEntity>();
    render_world.registrator().register_component<MainEntity>();
    render_world.registrator().register_component<TemporaryRenderEntity>();

    // 0. Bevy ComponentRemoved handling (sync_world.rs:238-250): when a synced
    //    component was removed from a main entity, despawn its render entity
    //    and drop the RenderEntity link so the scan below respawns a fresh one
    //    (clearing derived/extracted components).
    if (auto pending = main_world.get_resource_mut<PendingSyncEntity>()) {
        for (const Entity main_entity : pending->get().component_removed) {
            auto render_entity =
                main_world.get_entity(main_entity).and_then([](const EntityRef& e) -> std::optional<Entity> {
                    return e.get<RenderEntity>().transform(
                        [](const std::reference_wrapper<const RenderEntity>& re) { return re.get().entity; });
                });
            if (render_entity && render_world.get_entity(*render_entity)) {
                render_world.get_entity_mut(*render_entity).transform([](EntityWorldMut&& ew) -> int {
                    ew.despawn();
                    return 0;
                });
                main_world.get_entity_mut(main_entity).transform([](EntityWorldMut&& ew) -> int {
                    ew.remove<RenderEntity>();
                    return 0;
                });
            }
        }
        pending->get().component_removed.clear();
    }

    // 1. Despawn render-world entities whose main-world counterpart is gone or
    //    no longer marked for synchronization.
    {
        auto query = render_world.try_query<Item<Entity, const MainEntity&>>();
        if (query) {
            std::vector<Entity> to_despawn;
            for (auto&& [render_entity, main_entity] : query->iter(render_world)) {
                bool keep =
                    main_world.get_entity(main_entity.entity)
                        .transform([](const EntityRef& e) { return e.template get<SyncToRenderWorld>().has_value(); })
                        .value_or(false);
                if (!keep) {
                    to_despawn.push_back(render_entity);
                }
            }
            for (const Entity entity : to_despawn) {
                render_world.get_entity_mut(entity).transform([](EntityWorldMut&& ew) -> int {
                    ew.despawn();
                    return 0;
                });
            }
        }
    }
    // 2. Spawn render-world entities for main-world entities with
    //    SyncToRenderWorld that lack a valid RenderEntity mapping.
    {
        auto query = main_world.try_query<Item<Entity, const SyncToRenderWorld&>>();
        if (query) {
            std::vector<std::pair<Entity, Entity>> to_sync;  // (main_entity, render_entity)
            for (auto&& [main_entity, sync] : query->iter(main_world)) {
                (void)sync;
                bool already_synced =
                    main_world.get_entity(main_entity)
                        .and_then([](const EntityRef& e) -> std::optional<Entity> {
                            return e.get<RenderEntity>().transform(
                                [](const std::reference_wrapper<const RenderEntity>& re) { return re.get().entity; });
                        })
                        .transform([&](Entity render_entity) {
                            return render_world.get_entity(render_entity)
                                .and_then([&](const EntityRef& re) -> std::optional<bool> {
                                    return re.get<MainEntity>().transform(
                                        [&](const std::reference_wrapper<const MainEntity>& me) {
                                            return me.get().entity == main_entity;
                                        });
                                })
                                .value_or(false);
                        })
                        .value_or(false);
                if (!already_synced) {
                    Entity render_entity = render_world.spawn(MainEntity{main_entity}).id();
                    to_sync.emplace_back(main_entity, render_entity);
                }
            }
            for (auto&& [main_entity, render_entity] : to_sync) {
                main_world.get_entity_mut(main_entity).transform([&](EntityWorldMut&& ew) -> int {
                    ew.insert(RenderEntity{render_entity});
                    return 0;
                });
            }
        }
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
