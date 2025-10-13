#pragma once

#include <cstdint>
#include <utility>

#include "../world.hpp"
#include "epix/core/query/access.hpp"

namespace epix::core::system {
template <typename T>
struct SystemParam;
template <typename T>
struct param_type;
template <typename T>
struct param_type<SystemParam<T>> {
    using type = T;
};

enum SystemFlagBits : uint8_t {
    EXCLUSIVE = 1 << 0,  // system requires exclusive access to the world
    DEFERRED  = 1 << 1,  // system has deferred commands.
};
struct SystemMeta {
    std::string name;
    SystemFlagBits flags = (SystemFlagBits)0;

    bool is_exclusive() const { return (SystemFlagBits::EXCLUSIVE & flags) != (SystemFlagBits)0; }
    bool is_deferred() const { return (SystemFlagBits::DEFERRED & flags) != (SystemFlagBits)0; }
};

template <typename T>
concept valid_system_param = requires(World& world, SystemMeta& meta, query::FilteredAccess& access) {
    typename param_type<T>::type;
    // used to store data that persists across system runs
    typename T::State;
    // the item type returned when accessing the param, the item returned may not be T itself, it may be reference.
    typename T::Item;

    { T::init_state(world) } -> std::same_as<typename T::State>;
    requires requires(const typename T::State& state, typename T::State& state_mut, DeferredWorld deferred_world, Tick tick) {
        { T::init_access(state, meta, access, world) } -> std::same_as<void>;
        { T::apply(state_mut, world) } -> std::same_as<void>;
        { T::queue(state_mut, deferred_world) } -> std::same_as<void>;
        { T::validate_param(state_mut, std::as_const(meta), world) } -> std::same_as<void>;
        { T::get_param(state, std::as_const(meta), world, tick) } -> std::same_as<typename T::Item>;
    };
};
}  // namespace epix::core::system