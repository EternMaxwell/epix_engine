#pragma once

#include <ranges>

#include "world.h"

namespace epix::app {
template <typename T>
struct Opt;
template <typename T>
struct Has;

template <typename T>
struct GetAbs {
    using check_type  = std::tuple<T>;
    using access_type = std::tuple<T>;
    using get_type    = T&;
    template <typename TupleT>
    static get_type get_from_item(
        TupleT&& item, entt::registry& registry, Entity entity
    ) {
        return std::get<get_type>(item);
    }
};
template <>
struct GetAbs<Entity> {
    using check_type  = std::tuple<>;
    using access_type = std::tuple<>;
    using get_type    = Entity;
    template <typename TupleT>
    static get_type get_from_item(
        TupleT&& item, entt::registry& registry, Entity entity
    ) {
        return entity;
    }
};
template <typename T>
struct GetAbs<Opt<T>> {
    using check_type  = std::tuple<>;
    using access_type = std::tuple<T>;
    using get_type    = T*;
    template <typename TupleT>
    static get_type get_from_item(
        TupleT&& item, entt::registry& registry, Entity entity
    ) {
        return registry.try_get<T>(entity);
    }
};
template <typename T>
struct GetAbs<Has<T>> {
    using check_type  = std::tuple<>;
    using access_type = std::tuple<const T>;
    using get_type    = bool;
    template <typename TupleT>
    static get_type get_from_item(
        TupleT&& item, entt::registry& registry, Entity entity
    ) {
        return registry.all_of<T>(entity);
    }
};

template <typename T>
concept ValidGetType = requires(T t) {
    typename GetAbs<T>::check_type;
    typename GetAbs<T>::get_type;
    {
        GetAbs<T>::get_from_item(
            std::declval<typename GetAbs<T>::check_type>(),
            std::declval<entt::registry&>(), std::declval<Entity>()
        )
    } -> std::same_as<typename GetAbs<T>::get_type>;
};

template <typename... Tuples>
struct TupleCat {
    using type = decltype(std::tuple_cat(std::declval<Tuples>()...));
};

template <ValidGetType... Args>
struct Get;

struct FilterBase {
    using must_include = std::tuple<>;
    using access_type  = std::tuple<>;
    using must_exclude = std::tuple<>;
};

template <typename T>
concept FilterItem = requires(T t) {
    {
        T::check(std::declval<entt::registry&>(), std::declval<Entity>())
    } -> std::same_as<bool>;
    typename T::must_include;
    typename T::must_exclude;
    typename T::access_type;
    std::derived_from<T, FilterBase>;
};

template <typename... Args>
struct With : FilterBase {
    using must_include = std::tuple<const Args...>;
    using access_type  = std::tuple<const Args...>;
    static bool check(entt::registry& registry, Entity entity) {
        return (registry.try_get<Args>(entity) && ...);
    }
};

template <typename... Args>
struct Without : FilterBase {
    using must_exclude = std::tuple<Args...>;
    static bool check(entt::registry& registry, Entity entity) {
        return !(registry.try_get<Args>(entity) || ...);
    }
};

template <typename... Args>
    requires(FilterItem<Args> && ...)
struct Or : FilterBase {
    using access_type = typename TupleCat<typename Args::access_type...>::type;
    static bool check(entt::registry& registry, Entity entity) {
        return (Args::check(registry, entity) || ...);
    }
};

template <FilterItem... Args>
struct Filter;

template <>
struct Filter<> {
    static constexpr bool need_check = false;
    static bool check(entt::registry& registry, Entity entity) { return true; }
};

template <FilterItem T, typename... Rest>
struct Filter<T, Rest...> {
    static constexpr bool need_check = true;
    static bool check(entt::registry& registry, Entity entity) {
        return T::check(registry, entity) &&
               Filter<Rest...>::check(registry, entity);
    }
};

template <typename... Args, typename... Rest>
struct Filter<With<Args...>, Rest...> : Filter<Rest...> {
    static constexpr bool need_check = Filter<Rest...>::need_check;
};

template <typename... Args, typename... Rest>
struct Filter<Without<Args...>, Rest...> : Filter<Rest...> {
    static constexpr bool need_check = Filter<Rest...>::need_check;
};

template <typename... Args>
struct QueryTypeBuilder;
template <>
struct QueryTypeBuilder<> {
    using must_include = std::tuple<>;
    using must_exclude = std::tuple<>;
    using access_type  = std::tuple<>;
};
template <typename... Args, typename... Rest>
struct QueryTypeBuilder<Get<Args...>, Rest...> {
    using must_include = typename TupleCat<
        typename QueryTypeBuilder<Rest...>::must_include,
        typename GetAbs<Args>::check_type...>::type;
    using access_type = typename TupleCat<
        typename QueryTypeBuilder<Rest...>::access_type,
        typename GetAbs<Args>::access_type...>::type;
    using must_exclude = typename QueryTypeBuilder<Rest...>::must_exclude;
};
template <FilterItem T, typename... Rest>
struct QueryTypeBuilder<T, Rest...> {
    using must_include = typename TupleCat<
        typename QueryTypeBuilder<Rest...>::must_include,
        typename T::must_include>::type;
    using must_exclude = typename TupleCat<
        typename QueryTypeBuilder<Rest...>::must_exclude,
        typename T::must_exclude>::type;
    using access_type = typename TupleCat<
        typename QueryTypeBuilder<Rest...>::access_type,
        typename T::access_type>::type;
};

template <typename T>
struct TupleToEnttGetT;
template <typename... Args>
struct TupleToEnttGetT<std::tuple<Args...>> {
    using type = entt::get_t<Args...>;
};

template <typename T>
struct TupleToEnttExcludeT;
template <typename... Args>
struct TupleToEnttExcludeT<std::tuple<Args...>> {
    using type = entt::exclude_t<Args...>;
};

template <typename TI, typename TO>
struct EnttViewBuilder;
template <typename... TI, typename... TO>
struct EnttViewBuilder<std::tuple<TI...>, std::tuple<TO...>> {
    using type = decltype(std::declval<entt::registry>()
                              .view<TI...>(entt::exclude<TO...>));
    static type build(entt::registry& registry) {
        return registry.view<TI...>(entt::exclude<TO...>);
    }
};

template <
    epix::util::type_traits::specialization_of<Get> G,
    typename F = Filter<>>
struct Query;
template <typename... Gets, typename F>
    requires(!epix::util::type_traits::specialization_of<F, Filter> && FilterItem<F>)
struct Query<Get<Gets...>, F> : public Query<Get<Gets...>, Filter<F>> {};
template <typename... Gets, typename... Filters>
    requires(FilterItem<Filters> && ...)
struct Query<Get<Gets...>, Filter<Filters...>> {
    using must_include =
        typename QueryTypeBuilder<Get<Gets...>, Filters...>::must_include;
    using access_type =
        typename QueryTypeBuilder<Get<Gets...>, Filters...>::access_type;
    using must_exclude =
        typename QueryTypeBuilder<Get<Gets...>, Filters...>::must_exclude;
    using get_type = std::tuple<typename GetAbs<Gets>::get_type...>;

    using view_type =
        typename EnttViewBuilder<must_include, must_exclude>::type;
    using view_iterable = decltype(std::declval<view_type>().each());
    using view_iter     = decltype(std::declval<view_iterable>().begin());

   private:
    entt::registry* m_registry;
    view_type m_view;

   public:
    Query(World& world)
        : m_registry(&world.registry()),
          m_view(EnttViewBuilder<must_include, must_exclude>::build(*m_registry)
          ) {}

    auto iter() {
        if constexpr (Filter<Filters...>::need_check) {
            return m_view.each() | std::views::filter([this](auto&& item) {
                       return Filter<Filters...>::check(
                           *m_registry, std::get<0>(item)
                       );
                   }) |
                   std::views::transform([this](auto&& item) {
                       return get_type(GetAbs<Gets>::get_from_item(
                           item, *m_registry, std::get<0>(item)
                       )...);
                   });
        } else {
            return m_view.each() | std::views::transform([this](auto&& item) {
                       return get_type(GetAbs<Gets>::get_from_item(
                           item, *m_registry, std::get<0>(item)
                       )...);
                   });
        }
    }

    get_type single() {
        auto it   = iter();
        auto iter = it.begin();
        if (iter != it.end()) {
            return *iter;
        }
        throw std::runtime_error("No entity found in query!");
    }
    std::optional<get_type> get_single() {
        auto it   = iter();
        auto iter = it.begin();
        if (iter != it.end()) {
            return *iter;
        }
        return std::nullopt;
    }
    std::optional<get_type> get(Entity entity) {
        if (m_view.contains(entity)) {
            if constexpr (Filter<Filters...>::need_check) {
                if (Filter<Filters...>::check(*m_registry, entity)) {
                    return get_type(GetAbs<Gets>::get_from_item(
                        m_view.get(entity), *m_registry, entity
                    )...);
                }
            } else {
                return get_type(GetAbs<Gets>::get_from_item(
                    m_view.get(entity), *m_registry, entity
                )...);
            }
        }
        return std::nullopt;
    }
    bool contains(Entity entity) {
        if constexpr (Filter<Filters...>::need_check) {
            return m_view.contains(entity) &&
                   Filter<Filters...>::check(*m_registry, entity);
        } else {
            return m_view.contains(entity);
        }
    }
    template <typename Func>
        requires requires(Func func) {
            std::apply(func, std::declval<get_type>());
        }
    void for_each(Func&& func) {
        for (get_type&& item : iter()) {
            std::apply(func, item);
        }
    }
    bool empty() {
        auto it = iter();
        return it.begin() == it.end();
    }
    operator bool() { return !empty(); }
};
}  // namespace epix::app