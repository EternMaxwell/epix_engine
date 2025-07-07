#pragma once

#include <epix/utils/type_traits.h>

#include <ranges>

#include "systemparam.h"
#include "world.h"

namespace epix::app {
template <typename T>
struct Opt;
template <typename T>
struct Has;
template <typename T>
struct Mut;

template <typename T>
struct QueryItem {
    using check_type  = std::tuple<const T>;
    using access_type = std::tuple<const T>;
    using get_type    = const T&;
    template <typename TupleT>
    static const T& get(TupleT&& item, World& world, const Entity& entity) {
        return std::get<const T&>(item);
    }
};
template <typename T>
struct QueryItem<Mut<T>> {
    using check_type  = std::tuple<T>;
    using access_type = std::tuple<T>;
    using get_type    = T&;
    template <typename TupleT>
    static T& get(TupleT&& item, World& world, const Entity& entity) {
        return std::get<T&>(item);
    }
};
template <typename T>
struct QueryItem<Opt<T>> {
    using check_type  = std::tuple<>;
    using access_type = std::tuple<const T>;
    using get_type    = const T*;
    template <typename TupleT>
    static const T* get(TupleT&& item, World& world, const Entity& entity) {
        return world.entity_try_get<T>(entity);
    }
};
template <typename T>
struct QueryItem<Opt<Mut<T>>> {
    using check_type  = std::tuple<>;
    using access_type = std::tuple<T>;
    using get_type    = T*;
    template <typename TupleT>
    static T* get(TupleT&& item, World& world, const Entity& entity) {
        return world.entity_try_get<T>(entity);
    }
};
template <typename T>
struct QueryItem<Has<T>> {
    using check_type  = std::tuple<>;
    using access_type = std::tuple<const T>;
    using get_type    = bool;
    template <typename TupleT>
    static bool get(TupleT&& item, World& world, const Entity& entity) {
        return world.entity_valid(entity) && world.registry().all_of<T>(entity);
    }
};
template <typename T>
    requires std::same_as<Entity, std::remove_const_t<T>>
struct QueryItem<T> {
    using check_type = std::tuple<>;
    using get_type   = Entity;
    template <typename TupleT>
    static Entity get(TupleT&& item, World& world, const Entity& entity) {
        return entity;
    }
};

template <typename T>
concept ValidQueryItem =
    epix::util::type_traits::specialization_of<T, QueryItem> && requires(T t) {
        typename T::check_type;
        typename T::get_type;
        epix::util::type_traits::specialization_of<
            typename T::check_type, std::tuple>;
        {
            T::get(
                std::declval<typename T::check_type>(), std::declval<World&>(),
                std::declval<const Entity&>()
            )
        } -> std::same_as<typename T::get_type>;
    };
template <ValidQueryItem T>
struct QueryItemInfo {
    using check_type = typename T::check_type;
    // access type, if T did not has a access type, use std::tuple<>, otherwise
    // use access_type
    using access_type = std::decay_t<decltype([]() -> auto {
        if constexpr (requires { typename T::access_type; }) {
            return typename T::access_type{};
        } else {
            return std::tuple<>{};
        }
    }())>;
    using get_type    = typename T::get_type;
};

template <typename... Tuples>
struct TupleCat {
    using type = decltype(std::tuple_cat(std::declval<Tuples>()...));
};

// Args should have a valid QueryItem<Args> implementation
template <typename... Args>
    requires(ValidQueryItem<QueryItem<Args>> && ...)
struct Get;

struct FilterBase {
    using must_include = std::tuple<>;
    using access_type  = std::tuple<>;
    using must_exclude = std::tuple<>;
};

template <typename T>
concept FilterItem = requires(T t) {
    {
        T::check(std::declval<World&>(), std::declval<Entity>())
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
    static bool check(World& world, Entity entity) {
        return world.registry().all_of<Args...>(entity);
    }
};

template <typename... Args>
struct Without : FilterBase {
    using must_exclude = std::tuple<Args...>;
    static bool check(World& world, Entity entity) {
        return !world.registry().any_of<Args...>(entity);
    }
};

template <typename... Args>
    requires(FilterItem<Args> && ...)
struct Or : FilterBase {
    using access_type = typename TupleCat<typename Args::access_type...>::type;
    static bool check(World& world, Entity entity) {
        return (Args::check(world, entity) || ...);
    }
};

template <FilterItem... Args>
struct Filter;

template <>
struct Filter<> {
    static constexpr bool need_check = false;
    static bool check(World& world, Entity entity) { return true; }
};

template <FilterItem T, typename... Rest>
struct Filter<T, Rest...> {
    static constexpr bool need_check = []() {
        // Check if T is specialization of With or Without
        if constexpr (epix::util::type_traits::specialization_of<T, With> ||
                      epix::util::type_traits::specialization_of<T, Without>) {
            return Filter<Rest...>::need_check;
        } else {
            return true;
        }
    }();
    static bool check(World& world, Entity entity) {
        return T::check(world, entity) && Filter<Rest...>::check(world, entity);
    }
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
        typename QueryItemInfo<QueryItem<Args>>::check_type...>::type;
    using access_type = typename TupleCat<
        typename QueryTypeBuilder<Rest...>::access_type,
        typename QueryItemInfo<QueryItem<Args>>::access_type...>::type;
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
    requires(epix::util::type_traits::specialization_of<F, Filter> || FilterItem<F>)
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
    using get_type =
        std::tuple<typename QueryItemInfo<QueryItem<Gets>>::get_type...>;

    using view_type =
        typename EnttViewBuilder<must_include, must_exclude>::type;
    using view_iterable = decltype(std::declval<view_type>().each());
    using view_iter     = decltype(std::declval<view_iterable>().begin());

   private:
    World* m_world;
    view_type m_view;

   public:
    Query(World& world)
        : m_world(&world),
          m_view(EnttViewBuilder<must_include, must_exclude>::build(
              world.registry()
          )) {}

    auto iter() {
        if constexpr (Filter<Filters...>::need_check) {
            return m_view.each() | std::views::filter([this](auto&& item) {
                       return Filter<Filters...>::check(
                           *m_world, std::get<0>(item)
                       );
                   }) |
                   std::views::transform([this](auto&& item) {
                       return get_type(QueryItem<Gets>::get(
                           item, *m_world, std::get<0>(item)
                       )...);
                   });
        } else {
            return m_view.each() | std::views::transform([this](auto&& item) {
                       return get_type(QueryItem<Gets>::get(
                           item, *m_world, std::get<0>(item)
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
    std::optional<get_type> try_get(Entity entity) {
        if (m_view.contains(entity)) {
            if constexpr (Filter<Filters...>::need_check) {
                if (Filter<Filters...>::check(*m_world, entity)) {
                    return get_type(QueryItem<Gets>::get(
                        m_view.get(entity), *m_world, entity
                    )...);
                }
            } else {
                return get_type(QueryItem<Gets>::get(
                    m_view.get(entity), *m_world, entity
                )...);
            }
        }
        return std::nullopt;
    }
    get_type get(Entity entity) { return try_get(entity).value(); }
    bool contains(Entity entity) {
        if constexpr (Filter<Filters...>::need_check) {
            return m_view.contains(entity) &&
                   Filter<Filters...>::check(*m_world, entity);
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
template <typename Q>
    requires requires(Q t) {
        typename Q::must_include;
        typename Q::access_type;
        typename Q::must_exclude;
    } && epix::util::type_traits::specialization_of<Q, Query>
struct SystemParam<Q> {
    using State = std::optional<Q>;
    State init(SystemMeta& meta) {
        auto& qs = meta.access.queries.emplace_back();
        [&]<size_t... I>(std::index_sequence<I...>) {
            // if type is const, add to reads, otherwise add to writes
            ((std::is_const_v<std::tuple_element_t<I, typename Q::access_type>>
                  ? qs.component_reads.emplace(
                        meta::type_id<std::decay_t<
                            std::tuple_element_t<I, typename Q::access_type>>>{}
                    )
                  : qs.component_writes.emplace(
                        meta::type_id<std::decay_t<
                            std::tuple_element_t<I, typename Q::access_type>>>{}
                    )),
             ...);
        }(std::make_index_sequence<
            std::tuple_size<typename Q::access_type>::value>{});
        [&]<size_t... I>(std::index_sequence<I...>) {
            (qs.component_excludes.emplace(
                 meta::type_id<std::decay_t<
                     std::tuple_element_t<I, typename Q::must_exclude>>>{}
             ),
             ...);
        }(std::make_index_sequence<
            std::tuple_size<typename Q::must_exclude>::value>{});
        return std::nullopt;
    }
    bool update(State& state, World& world, const SystemMeta& meta) {
        state.emplace(Q(world));
        return true;
    }
    Q& get(State& state) { return state.value(); }
};
}  // namespace epix::app