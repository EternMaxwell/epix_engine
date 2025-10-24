#pragma once

#include <concepts>
#include <functional>

#include "../system/param.hpp"

namespace epix::core::app {
template <typename T>
struct Extract {
   public:
    using type = T;

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    explicit Extract(Args&&... args) : value(std::forward<Args>(args)...) {}

    Extract(const Extract&)            = default;
    Extract(Extract&&)                 = default;
    Extract& operator=(const Extract&) = default;
    Extract& operator=(Extract&&)      = default;

    operator T&() { return value; }
    operator const T&() const { return value; }
    T& get() { return value; }
    const T& get() const { return value; }
    T& operator*() { return value; }
    const T& operator*() const { return value; }
    T* operator->() { return &value; }
    const T* operator->() const { return &value; }

   private:
    T value;
};

struct ExtractedWorld {
    std::reference_wrapper<World> world;
};
}  // namespace epix::core::app

namespace epix::core::system {
template <typename T>
struct SystemParam<app::Extract<T>> : SystemParam<T> {
    using Base  = SystemParam<T>;
    using State = typename Base::State;
    using Item  = app::Extract<typename Base::Item>;

    static State init_state(World& world) { return Base::init_state(world.resource_mut<app::ExtractedWorld>().world); }
    static void init_access(const State& state,
                            SystemMeta& meta,
                            query::FilteredAccessSet& access,
                            const World& world) {
        // This is a workaround to initialize access for ensuring no conflicts. But the access should be separated for
        // each world. Affects some performance but for readonly extract it is zero-cost.
        Base::init_access(state, meta, access, world.resource<app::ExtractedWorld>().world);
    }
    static void apply(State& state, const SystemMeta& meta, World& world) {
        Base::apply(state, meta, world.resource_mut<app::ExtractedWorld>().world);
    }
    static void queue(State& state, const SystemMeta& meta, DeferredWorld deferred_world) {
        Base::queue(state, meta, DeferredWorld(deferred_world.resource_mut<app::ExtractedWorld>().world));
    }
    static std::expected<void, ValidateParamError> validate_param(State& state, const SystemMeta& meta, World& world) {
        return Base::validate_param(state, meta, world.resource_mut<app::ExtractedWorld>().world);
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return Item(Base::get_param(state, meta, world, tick));
    }
};
static_assert(valid_system_param<SystemParam<app::Extract<ResMut<int>>>>);
}  // namespace epix::core::system