#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <concepts>
#include <cstdint>
#include <epix/common.hpp>
#include <memory>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

#include <epix/ecs/core/type_id.hpp>
#include <epix/ecs/entity/entity.hpp>
#include <epix/ecs/storage/storage_type.hpp>

namespace epix::ecs {
struct EPIX_API World;
struct HookContext;

EPIX_EXPORT enum class LifecycleEventKind : std::uint8_t { Add, Insert, Replace, Remove, Despawn };
EPIX_EXPORT struct Add { Entity entity; static constexpr auto kind = LifecycleEventKind::Add; };
EPIX_EXPORT struct Insert { Entity entity; static constexpr auto kind = LifecycleEventKind::Insert; };
EPIX_EXPORT struct Replace { Entity entity; static constexpr auto kind = LifecycleEventKind::Replace; };
EPIX_EXPORT struct Remove { Entity entity; static constexpr auto kind = LifecycleEventKind::Remove; };
EPIX_EXPORT struct Despawn { Entity entity; static constexpr auto kind = LifecycleEventKind::Despawn; };

EPIX_EXPORT template<class E>
concept lifecycle_event = std::same_as<E, Add> || std::same_as<E, Insert> || std::same_as<E, Replace> ||
                          std::same_as<E, Remove> || std::same_as<E, Despawn>;

namespace internal {
template<class T>
concept observer_filter_leaf = std::is_object_v<T> && std::same_as<T, std::remove_cvref_t<T>> &&
                               !std::is_array_v<T> && requires { sizeof(T); };
template<class T>
struct ObserverFilter : std::bool_constant<observer_filter_leaf<T>> {};
template<class... Ts>
struct ObserverFilter<std::tuple<Ts...>> : std::bool_constant<(ObserverFilter<Ts>::value && ...)> {};
}  // namespace internal

/** @brief Metadata filter: complete unqualified component/bundle objects or
 * nested tuples thereof. No construction or movement of filter values is needed. */
EPIX_EXPORT template<class T>
concept observer_filter = internal::ObserverFilter<T>::value;

/** @brief Borrowed lifecycle-system input. Bundle is a component, an engine
 * bundle, or a nested tuple of these; an empty tuple observes the event globally. Do not retain the
 * event or component span after the callback. Independent observers are unordered. */
EPIX_EXPORT template<lifecycle_event E, observer_filter Bundle = std::tuple<>>
class On {
   public:
    using Event = E;
    using Components = Bundle;
    On(E& event, Entity observer, std::span<const TypeId> components) noexcept
        : event_(&event), observer_(observer), components_(components) {}
    E& event() const noexcept { return *event_; }
    E* operator->() const noexcept { return event_; }
    E& operator*() const noexcept { return *event_; }
    Entity observer() const noexcept { return observer_; }
    std::span<const TypeId> components() const noexcept { return components_; }
   private:
    E* event_;
    Entity observer_;
    std::span<const TypeId> components_;
};

namespace internal {
struct LifecycleObserverState {
    LifecycleEventKind kind;
    Entity observer = Entity::PLACEHOLDER;
    std::vector<TypeId> components;
    std::uint32_t last_trigger = 0;
    bool running = false;
    explicit LifecycleObserverState(LifecycleEventKind event) : kind(event) {}
    virtual void initialize(World&) = 0;
    virtual void invoke(void*, std::span<const TypeId>, World&) = 0;
    virtual ~LifecycleObserverState() = default;
};

// Caches hold weak references: removing the Observer component destroys its
// captured system immediately, independently of the deferred cache cleanup.
class EPIX_API LifecycleObserverCache {
    using List = std::vector<std::weak_ptr<LifecycleObserverState>>;
    struct EventCache {
        List global;
        std::unordered_map<std::size_t, List> components;
    };
    std::array<EventCache, 5> events_;
    std::uint32_t trigger_ = 0;
   public:
    template<std::ranges::input_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, TypeId> &&
                 std::convertible_to<std::ranges::range_reference_t<Range>, TypeId>
    bool matches(LifecycleEventKind kind, Range&& archetype_components) const {
        const auto& event = events_[static_cast<std::size_t>(kind)];
        // Match pinned Bevy's lifecycle archetype flags: only component
        // listeners enable automatic dispatch. Global listeners join that
        // dispatch, including an empty affected-component list.
        if (event.components.empty()) return false;
        for (TypeId component : archetype_components)
            if (event.components.contains(component.get())) return true;
        return false;
    }
    void insert(const std::shared_ptr<LifecycleObserverState>&);
    void erase(LifecycleEventKind, Entity, std::span<const TypeId>);
    void invoke(LifecycleEventKind, Entity, std::span<const TypeId>, World&);
};
}  // namespace internal

}  // namespace epix::ecs
