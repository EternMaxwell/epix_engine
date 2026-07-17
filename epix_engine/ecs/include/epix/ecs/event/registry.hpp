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
    void (*update)(void*);
};

EPIX_EXPORT enum class UpdateState { Always, Waiting, Ready };

EPIX_EXPORT struct EventRegistry {
    UpdateState state;
    std::unordered_map<TypeId, RegisteredEvent> events;

    template <std::movable T>
    static void register_event(World& world) {
        auto id         = world.init_resource<Events<T>>();
        auto&& registry = world.resource_or_init<EventRegistry>();
        registry.events.emplace(id, RegisteredEvent{.previously_updated = false, .update = [](void* queue) {
                                                        static_cast<Events<T>*>(queue)->update();
                                                    }});
    }
    void run_updates(World& world, Tick last_change_tick) {
        for (auto&& [id, event] : events) {
            auto& data       = world.storage_mut().resources.get_mut(id).value().get();
            TicksMut ticks   = TicksMut::from_refs(data.get_tick_refs().value(), last_change_tick, world.change_tick());
            bool has_changed = ticks.is_modified();
            if (event.previously_updated || has_changed) {
                event.update(data.get_mut().value());
                event.previously_updated = has_changed || !event.previously_updated;
            }
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
