#include <epix/ecs/system.hpp>
#include <epix/ecs/world.hpp>

#include <algorithm>
#include <variant>

namespace epix::ecs {
bool internal::world_has_lifecycle_observers(const World& world, const Archetype& archetype, LifecycleEventKind kind) {
    return world._lifecycle_observers.matches(kind, archetype.components());
}
void internal::world_invoke_lifecycle_observers(World& world, LifecycleEventKind kind, Entity entity,
                                               std::span<const TypeId> components) {
    world._lifecycle_observers.invoke(kind, entity, components, world);
}

void Observer::on_add(World& world, HookContext context) {
    world.command_queue().push([entity = context.entity](World& deferred) {
        const auto row = deferred.get_entity(entity);
        if (!row) return;
        const auto node = row->get<Observer>();
        if (!node || !node->get().state_) return;
        auto state = node->get().state_;
        state->observer = entity;
        state->initialize(deferred);
        deferred._lifecycle_observers.insert(state);
    });
}

void Observer::on_remove(World& world, HookContext context) {
    const auto node = world.entity(context.entity).get<Observer>();
    if (!node || !node->get().state_) return;
    const auto& state = *node->get().state_;
    world.command_queue().push([kind = state.kind, entity = context.entity, components = state.components](World& deferred) {
        deferred._lifecycle_observers.erase(kind, entity, components);
    });
}

void internal::LifecycleObserverCache::insert(const std::shared_ptr<LifecycleObserverState>& state) {
    auto& event = events_[static_cast<std::size_t>(state->kind)];
    if (state->components.empty()) event.global.emplace_back(state);
    for (auto component : state->components) event.components[component.get()].emplace_back(state);
}

void internal::LifecycleObserverCache::erase(LifecycleEventKind kind, Entity observer, std::span<const TypeId> components) {
    auto& event = events_[static_cast<std::size_t>(kind)];
    const auto remove = [observer](auto& list) {
        std::erase_if(list, [observer](const auto& weak) {
            const auto state = weak.lock();
            return !state || state->observer == observer;
        });
    };
    remove(event.global);
    for (auto component : components) {
        auto found = event.components.find(component.get());
        if (found == event.components.end()) continue;
        remove(found->second);
        if (found->second.empty()) event.components.erase(found);
    }
}

void internal::LifecycleObserverCache::invoke(LifecycleEventKind kind, Entity entity,
                                             std::span<const TypeId> components, World& world) {
    auto& cache = events_[static_cast<std::size_t>(kind)];
    ++trigger_;
    std::vector<std::shared_ptr<LifecycleObserverState>> pending;
    const auto collect = [&](const auto& list) {
        for (const auto& weak : list) if (auto state = weak.lock()) pending.push_back(std::move(state));
    };
    collect(cache.global);
    for (auto component : components)
        if (const auto found = cache.components.find(component.get()); found != cache.components.end()) collect(found->second);

    // Every callback borrows the same typed event, including changes to it.
    auto event = [&]() -> std::variant<Add, Insert, Replace, Remove, Despawn> {
        switch (kind) {
            case LifecycleEventKind::Add: return Add{entity};
            case LifecycleEventKind::Insert: return Insert{entity};
            case LifecycleEventKind::Replace: return Replace{entity};
            case LifecycleEventKind::Remove: return Remove{entity};
            case LifecycleEventKind::Despawn: return Despawn{entity};
        }
        throw std::logic_error("Unknown lifecycle event kind");
    }();
    for (const auto& state : pending) {
        if (state->last_trigger == trigger_) continue;
        const auto owner = world.get_entity(state->observer);
        if (!owner) continue;
        const auto node = owner->get<Observer>();
        if (!node || node->get().state_ != state) continue;
        state->last_trigger = trigger_;
        std::visit([&](auto& typed) { state->invoke(&typed, components, world); }, event);
    }
}
}  // namespace epix::ecs
