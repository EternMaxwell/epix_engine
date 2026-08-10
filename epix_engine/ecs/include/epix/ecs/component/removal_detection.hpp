#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cstdint>
#include <epix/common.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>
#endif

#include <epix/ecs/core/type_id.hpp>
#include <epix/ecs/entity/entity.hpp>
#include <epix/ecs/event/events.hpp>
#include <epix/ecs/storage/sparse_set.hpp>

namespace epix::ecs {

/** @brief Entity payload stored when a component is removed. */
EPIX_EXPORT struct RemovedComponentEntity {
    Entity entity;

    operator Entity() const noexcept { return entity; }
};

/** @brief Per-component removal event streams owned by a World. */
EPIX_EXPORT struct RemovedComponentEvents {
    using Event = RemovedComponentEntity;

    void update() {
        for (auto&& [_, events] : event_sets_.iter_mut()) events.update();
    }

    auto iter() const { return event_sets_.iter(); }
    auto iter_mut() { return event_sets_.iter_mut(); }

    std::optional<std::reference_wrapper<const Events<Event>>> get(TypeId component_id) const noexcept {
        return event_sets_.get(component_id);
    }

    std::optional<std::reference_wrapper<Events<Event>>> get_mut(TypeId component_id) noexcept {
        return event_sets_.get_mut(component_id);
    }

    void write(TypeId component_id, Entity entity) {
        auto events = event_sets_.get_mut(component_id);
        if (!events) {
            event_sets_.emplace(component_id);
            events = event_sets_.get_mut(component_id);
        }
        events->get().emplace(entity);
    }

    /** Iterate removals written since the most recent World::clear_trackers call. */
    auto current(std::optional<TypeId> component_id) const {
        const Events<Event>* events = component_id.and_then([&](TypeId id) { return get(id); })
                                          .transform([](auto ref) { return std::addressof(ref.get()); })
                                          .value_or(nullptr);
        const std::uint32_t begin   = events ? events->head() : 0;
        const std::uint32_t end     = events ? events->tail() : 0;
        return std::views::iota(begin, end) | std::views::filter([events](std::uint32_t index) {
                   return events != nullptr && events->is_current(index);
               }) |
               std::views::transform([events](std::uint32_t index) { return events->get(index)->entity; });
    }

   private:
    SparseSet<TypeId, Events<Event>> event_sets_;
};

}  // namespace epix::ecs
