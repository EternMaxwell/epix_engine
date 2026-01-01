module;

export module epix.core:system.from_param;

import std;

import :utils;
import :system.param;

namespace core {
template <typename T>
concept from_param = requires {
    typename function_traits<decltype(T::from_param)>::args_tuple;
    requires system_param<typename function_traits<decltype(T::from_param)>::args_tuple>;
    {
        std::apply(&T::from_param, std::declval<typename function_traits<decltype(T::from_param)>::args_tuple&&>())
    } -> std::same_as<T>;
};
template <from_param T>
struct SystemParam<T> : public SystemParam<typename function_traits<decltype(T::from_param)>::args_tuple> {
    using Base  = SystemParam<typename function_traits<decltype(T::from_param)>::args_tuple>;
    using State = typename Base::State;
    using Item  = T;
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return std::apply(&T::from_param, Base::get_param(state, meta, world, tick));
    }
    // other methods are inherited from Base
};
}  // namespace core