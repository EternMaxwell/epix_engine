#pragma once

#include <epix/ecs/observer/types.hpp>
#include <epix/ecs/system/function_traits.hpp>

namespace epix::ecs {
namespace internal {
template<class T>
struct LifecycleObserverInput : std::false_type {};
template<lifecycle_event E, observer_filter B>
struct LifecycleObserverInput<On<E, B>> : std::true_type {};
}  // namespace internal

/** @brief A storable, invocable system with an exact On input, valid system
 * parameters and a void/fallible-void result. Exclusivity is checked at runtime
 * before allocating the observer entity, matching Bevy's Observer::new. */
EPIX_EXPORT template<class F>
concept valid_observer_system = internal::valid_function_system<F> &&
    internal::LifecycleObserverInput<typename internal::function_system_traits<F>::Input>::value &&
    std::same_as<internal::system_output_t<typename internal::function_system_traits<F>::Output>, void>;

/** @brief A World-owned lifecycle observer. Despawn this entity (or remove this
 * component) to stop observing. Retarget by creating a new observer, as in Bevy.
 * Arbitrary user events and entity-targeted observers are separate facilities. */
EPIX_EXPORT class EPIX_API Observer {
   public:
    Observer(const Observer&) = delete;
    Observer& operator=(const Observer&) = delete;
    Observer(Observer&&) noexcept = default;
    Observer& operator=(Observer&&) noexcept = default;
    /** @brief Create an observer component for spawning directly or via Commands.
     *  Exclusive systems are rejected before allocating an entity. */
    template<valid_observer_system F>
    static Observer create(F&& callback);
    static void on_add(World&, HookContext);
    static void on_remove(World&, HookContext);
   private:
    friend struct World;
    friend class internal::LifecycleObserverCache;
    explicit Observer(std::shared_ptr<internal::LifecycleObserverState> state) : state_(std::move(state)) {}
    std::shared_ptr<internal::LifecycleObserverState> state_;
};
template<> struct sparse_component<Observer> : std::true_type {};
}  // namespace epix::ecs
