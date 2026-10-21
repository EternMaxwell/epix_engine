#pragma once

// Included after the complete System/World definitions in system.hpp. Keeping
// the callable adapter public is necessary for downstream lambda system types.
namespace epix::ecs::internal {
template<observer_filter T> struct ObserverComponentIds {
    static std::vector<TypeId> get(World& world) {
        auto registrator = world.registrator();
        if constexpr (is_bundle<T>) {
            auto ids = Bundle<std::decay_t<T>>::register_components(registrator);
            return {std::ranges::begin(ids), std::ranges::end(ids)};
        } else {
            return {registrator.register_component<T>()};
        }
    }
};
template<observer_filter... Ts> struct ObserverComponentIds<std::tuple<Ts...>> {
    static std::vector<TypeId> get(World& world) {
        std::vector<TypeId> ids;
        const auto append = [&]<observer_filter T>() {
            auto nested = ObserverComponentIds<T>::get(world);
            ids.insert(ids.end(), nested.begin(), nested.end());
        };
        (append.template operator()<Ts>(), ...);
        return ids;
    }
};

template<valid_observer_system F>
class LifecycleObserverSystem final : public LifecycleObserverState {
    using Input = typename function_system_traits<F>::Input;
    using E = typename Input::Event;
    using B = typename Input::Components;
    std::unique_ptr<System<Input, void>> system_;
   public:
    explicit LifecycleObserverSystem(F&& callback)
        : LifecycleObserverState(E::kind), system_(make_system_unique(std::forward<F>(callback))) {
        reject_exclusive();
    }
    void initialize(World& world) override {
        components = ObserverComponentIds<B>::get(world);
        system_->initialize(world);
        // Also guard custom parameters that only set their flag in init_access.
        reject_exclusive();
    }
    void invoke(void* event, std::span<const TypeId> targets, World& world) override {
        if (running) throw std::logic_error("Observer system cannot run recursively");
        struct Guard { bool& value; ~Guard() { value = false; } } guard{running};
        running = true;
        auto result = system_->run_no_apply(Input{*static_cast<E*>(event), observer, targets}, world);
        // Do not use System::run/apply_deferred here: structural commands must
        // remain queued until the enclosing component lifecycle has completed.
        if (!result) std::visit([](auto& error) -> void {
            if constexpr (std::same_as<std::decay_t<decltype(error)>, SystemException>)
                std::rethrow_exception(error.exception);
            else throw error;
        }, result.error());
        // A throwing callback/default error handler interrupts the native
        // runner before queue_deferred. Keep its commands in the system until
        // a later successful invocation (or ordinary RAII destruction).
        system_->queue_deferred(DeferredWorld{world});
    }
   private:
    void reject_exclusive() const {
        if (system_->is_exclusive())
            throw std::logic_error("Exclusive systems cannot be observers; use Commands for structural changes");
    }
};
}  // namespace epix::ecs::internal

namespace epix::ecs {
template<valid_observer_system F>
Observer Observer::create(F&& callback) {
    return Observer{std::make_shared<internal::LifecycleObserverSystem<F>>(std::forward<F>(callback))};
}

template<valid_observer_system F>
EntityWorldMut World::add_observer(F&& callback) {
    return spawn(Observer::create(std::forward<F>(callback)));
}
}  // namespace epix::ecs
