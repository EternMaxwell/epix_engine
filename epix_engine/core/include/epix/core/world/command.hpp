#pragma once

#include <concepts>
#include <functional>

#include "../fwd.hpp"

namespace epix::core {
template <typename T>
struct Command {
    static_assert("Not a valid command type");
};
template <typename T>
concept is_command = requires(T t) {
    { t.apply(std::declval<World&>()) } -> std::same_as<void>;
};
template <typename T>
concept valid_command = requires {
    typename T::Type;
    T::apply(std::declval<typename T::Type&>(), std::declval<World&>());
};
template <std::invocable<World&> F>
struct Command<F> {
    using Type = F;
    static void apply(F& f, World& world) { std::invoke(f, world); }
};
template <is_command T>
struct Command<T> {
    using Type = T;
    static void apply(T& cmd, World& world) { cmd.apply(world); }
};
}  // namespace epix::core