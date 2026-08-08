#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <optional>
#include <unordered_map>
#endif

#include <epix/ecs/core/tick.hpp>
#include <epix/ecs/event/events.hpp>
#include <epix/ecs/system.hpp>
#include <epix/ecs/system/local.hpp>
#include <epix/ecs/world.hpp>

namespace epix::ecs {

EPIX_EXPORT struct RegisteredEvent {
    bool previously_updated;
    bool (*update)(World&, Tick, bool);
};

EPIX_EXPORT enum class UpdateState { Always, Waiting, Ready };

EPIX_EXPORT struct EventRegistry {
    UpdateState state;
    std::unordered_map<TypeId, RegisteredEvent> events;

    template <std::movable T>
    static void register_event(World& world) {
        auto id         = world.init_resource<Events<T>>();
        auto&& registry = world.resource_or_init<EventRegistry>();
        registry.events.emplace(
            id, RegisteredEvent{.previously_updated = false,
                                .update             = [](World& world, Tick last_change_tick, bool previously_updated) {
                                    auto entity = world.resource_entity<Events<T>>();
                                    if (!entity) return false;
                                    auto resource_ref = world.entity(*entity).template get_ref<Events<T>>(
                                        last_change_tick, world.change_tick());
                                    if (!resource_ref) return false;
                                    bool has_changed = resource_ref->is_modified();
                                    if (previously_updated || has_changed) {
                                        world.entity_mut(*entity).template get_mut<Events<T>>()->get_mut().update();
                                        return has_changed || !previously_updated;
                                    }
                                    return false;
                                }});
    }
    void run_updates(World& world, Tick last_change_tick) {
        for (auto&& [_, event] : events) {
            event.previously_updated = event.update(world, last_change_tick, event.previously_updated);
        }
    }
    template <std::movable T>
    static void deregister_event(World& world) {
        auto id        = world.init_resource<Events<T>>();
        auto& registry = world.resource_or_init<EventRegistry>();
        registry.events.erase(id);
        world.remove_resource<Events<T>>();
    }
};

EPIX_EXPORT constexpr inline struct EventUpdateSystemT {
} EventUpdateSystem;

EPIX_EXPORT inline void signal_event_update(std::optional<ResMut<EventRegistry>> registry) {
    registry.transform([](ResMut<EventRegistry>& reg) {
        if (reg.get().state == UpdateState::Waiting) reg.get_mut().state = UpdateState::Ready;
        return 0;
    });
}
EPIX_EXPORT inline void event_update_system(World& world, Local<Tick> last_update_tick) {
    world.get_resource_mut<EventRegistry>().transform([&](EventRegistry& registry) {
        registry.run_updates(world, last_update_tick);
        if (registry.state == UpdateState::Ready) registry.state = UpdateState::Waiting;
        return 0;
    });
    last_update_tick.get() = world.change_tick();
}
EPIX_EXPORT inline bool event_update_condition(std::optional<ResMut<EventRegistry>> registry) {
    return registry.transform([](ResMut<EventRegistry>& reg) { return reg.get().state != UpdateState::Waiting; })
        .value_or(false);
}

}  // namespace epix::ecs
