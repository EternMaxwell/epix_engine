#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/meta.hpp>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#endif

#include <epix/ecs/system/param.hpp>
#include <epix/ecs/world/from_world.hpp>

namespace epix::ecs {

/** @brief System-local mutable state that persists across system invocations.
 *  Initialized via FromWorld on first use.
 *  @tparam T Value type (non-reference, non-const). */
EPIX_EXPORT template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct Local {
   public:
    Local(T& value) : value(std::addressof(value)) {}

    /** @brief Get a mutable reference to the local state. */
    T& get() { return *value; }
    /** @brief Arrow operator for mutable access. */
    T* operator->() { return value; }
    /** @brief Dereference operator for mutable access. */
    T& operator*() { return *value; }
    operator T&() { return *value; }

    /** @brief Get a const reference to the local state. */
    const T& get() const { return *value; }
    /** @brief Arrow operator for const access. */
    const T* operator->() const { return value; }
    /** @brief Dereference operator for const access. */
    const T& operator*() const { return *value; }
    operator const T&() const { return *value; }

   private:
    T* value;
};

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T> && internal::is_from_world<T>)
struct SystemParam<Local<T>> : ParamBase {
    using State                    = T;
    using Item                     = Local<T>;
    static constexpr bool readonly = true;
    static State init_state(World& world) { return internal::FromWorld<T>::create(world); }
    static Item get_param(State& state, const SystemMeta&, World&, Tick) { return Local<T>(const_cast<T&>(state)); }
};
static_assert(system_param<Local<int>>);
}  // namespace epix::ecs