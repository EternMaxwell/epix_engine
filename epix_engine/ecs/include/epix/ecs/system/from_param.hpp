#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <epix/common.hpp>
#include <epix/utils.hpp>
#include <tuple>
#include <utility>
#endif

#include <epix/ecs/system/param.hpp>

namespace epix::ecs {
namespace internal {
template <typename T>
concept from_param = requires {
    typename traits::function_traits<decltype(T::from_param)>::args_tuple;
    requires system_param<typename traits::function_traits<decltype(T::from_param)>::args_tuple>;
    {
        std::apply(&T::from_param,
                   std::declval<typename traits::function_traits<decltype(T::from_param)>::args_tuple&&>())
    } -> std::same_as<T>;
};
}  // namespace internal
template <internal::from_param T>
struct SystemParam<T> : public SystemParam<typename traits::function_traits<decltype(T::from_param)>::args_tuple> {
    using Base  = SystemParam<typename traits::function_traits<decltype(T::from_param)>::args_tuple>;
    using State = typename Base::State;
    using Item  = T;
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) noexcept(
        noexcept(std::apply(&T::from_param, Base::get_param(state, meta, world, tick)))) {
        return std::apply(&T::from_param, Base::get_param(state, meta, world, tick));
    }
    // other methods are inherited from Base
};
}  // namespace epix::ecs