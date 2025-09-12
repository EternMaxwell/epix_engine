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
struct QueryElement {
    using check_type  = std::tuple<const T>;
    using access_type = std::tuple<const T>;
    using get_type    = const T&;
    template <typename TupleT>
    static const T& get(TupleT&& item, const World& world, const Entity& entity) {
        return std::get<const T&>(item);
    }
};
template <typename T>
struct QueryElement<Mut<T>> {
    using check_type       = std::tuple<T>;
    using access_type      = std::tuple<T>;
    using get_type         = T&;
    using readonly_element = T;
    template <typename TupleT>
    static T& get(TupleT&& item, World& world, const Entity& entity) {
        return std::get<T&>(item);
    }
};
template <typename T>
struct QueryElement<Opt<T>> {
    using check_type  = std::tuple<>;
    using access_type = std::tuple<const T>;
    using get_type    = const T*;
    template <typename TupleT>
    static const T* get(TupleT&& item, const World& world, const Entity& entity) {
        return world.entity_try_get<T>(entity);
    }
};
template <typename T>
struct QueryElement<Opt<Mut<T>>> {
    using check_type  = std::tuple<>;
    using access_type = std::tuple<T>;
    using get_type    = T*;
    template <typename TupleT>
    static T* get(TupleT&& item, World& world, const Entity& entity) {
        return world.entity_try_get<T>(entity);
    }
};
template <typename T>
struct QueryElement<Has<T>> {
    using check_type  = std::tuple<>;
    using access_type = std::tuple<const T>;
    using get_type    = bool;
    template <typename TupleT>
    static bool get(TupleT&& item, const World& world, const Entity& entity) {
        return world.entity_valid(entity) && world.registry().all_of<T>(entity);
    }
};
template <typename T>
    requires std::same_as<Entity, std::remove_const_t<T>>
struct QueryElement<T> {
    using check_type = std::tuple<>;
    using get_type   = Entity;
    template <typename TupleT>
    static Entity get(TupleT&& item, const World& world, const Entity& entity) {
        return entity;
    }
};

template <typename T>
concept ValidQueryElement = epix::util::type_traits::specialization_of<T, QueryElement> && requires(T t) {
    typename T::check_type;
    typename T::get_type;
    epix::util::type_traits::specialization_of<typename T::check_type, std::tuple>;
    {
        T::get(std::declval<typename T::check_type>(), std::declval<World&>(), std::declval<const Entity&>())
    } -> std::same_as<typename T::get_type>;
};
template <ValidQueryElement T>
struct QueryElementInfo;

template <typename E>
struct QueryElementInfo<QueryElement<E>> {
    using T          = QueryElement<E>;
    using check_type = typename T::check_type;
    // access type, if T did not has a access type, use std::tuple<>, otherwise
    // use access_type
    using access_type              = typename std::decay_t<decltype([]() -> auto {
        if constexpr (requires { typename T::access_type; }) {
            return std::optional<typename T::access_type>{};
        } else {
            return std::optional<std::tuple<>>{};
        }
    }())>::value_type;
    using get_type                 = typename T::get_type;
    using readonly_element         = E;
    static constexpr bool readonly = true;
};
template <template <typename> typename Element, typename E>
struct QueryElementInfo<QueryElement<Element<E>>> {
   private:
    using T = QueryElement<Element<E>>;

   public:
    using check_type = typename T::check_type;
    // access type, if T did not has a access type, use std::tuple<>, otherwise
    // use access_type
    using access_type              = typename std::decay_t<decltype([]() -> auto {
        if constexpr (requires { typename T::access_type; }) {
            return std::optional<typename T::access_type>{};
        } else {
            return std::optional<std::tuple<>>{};
        }
    }())>::value_type;
    using get_type                 = typename T::get_type;
    using readonly_element         = std::remove_pointer_t<typename std::decay_t<decltype([]() -> auto {
        if constexpr (requires { typename T::readonly_element; }) {
            return std::optional<typename T::readonly_element*>{};
        } else {
            return std::optional<Element<typename QueryElementInfo<QueryElement<E>>::readonly_element>*>{};
        }
    }())>::value_type>;
    static constexpr bool readonly = std::same_as<T, readonly_element>;
};

template <typename... Tuples>
struct TupleCat {
    using type = decltype(std::tuple_cat(std::declval<Tuples>()...));
};

// Args should have a valid QueryElement<Args> implementation
template <typename... Args>
    requires(ValidQueryElement<QueryElement<Args>> && ...)
struct Item : public std::tuple<typename QueryElementInfo<QueryElement<Args>>::get_type...> {
    using base_type = std::tuple<typename QueryElementInfo<QueryElement<Args>>::get_type...>;

    static constexpr bool readonly = []() constexpr {
        if constexpr (sizeof...(Args) == 0) {
            return true;
        } else {
            // check if all access type is const or empty
            return (QueryElementInfo<QueryElement<Args>>::readonly && ...);
        }
    }();
    using readonly_item = Item<typename QueryElementInfo<QueryElement<Args>>::readonly_element...>;

    Item(const base_type& t) : base_type(t) {}
    Item(base_type&& t) : base_type(std::move(t)) {}
    Item(const Item& other)            = default;
    Item(Item&& other)                 = default;
    Item& operator=(const Item& other) = default;
    Item& operator=(Item&& other)      = default;
};

template <typename... Args>
    requires(ValidQueryElement<QueryElement<Args>> && ...)
using QueryGet = Item<Args...>;

struct FilterBase {
    using must_include = std::tuple<>;
    using access_type  = std::tuple<>;
    using must_exclude = std::tuple<>;
};

template <typename T>
concept FilterItem = requires(T t) {
    { T::check(std::declval<const World&>(), std::declval<Entity>()) } -> std::same_as<bool>;
    typename T::must_include;
    typename T::must_exclude;
    typename T::access_type;
    std::derived_from<T, FilterBase>;
};

template <typename... Args>
struct With : FilterBase {
    using must_include = std::tuple<const Args...>;
    using access_type  = std::tuple<const Args...>;
    static bool check(const World& world, Entity entity) { return world.registry().all_of<Args...>(entity); }
};

template <typename... Args>
struct Without : FilterBase {
    using must_exclude = std::tuple<Args...>;
    static bool check(const World& world, Entity entity) { return !world.registry().any_of<Args...>(entity); }
};

template <typename... Args>
    requires(FilterItem<Args> && ...)
struct Or : FilterBase {
    using access_type = typename TupleCat<typename Args::access_type...>::type;
    static bool check(const World& world, Entity entity) { return (Args::check(world, entity) || ...); }
};

template <FilterItem... Args>
struct Filter;

template <>
struct Filter<> {
    static constexpr bool need_check = false;
    static bool check(const World& world, Entity entity) { return true; }
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
    static bool check(const World& world, Entity entity) {
        if constexpr (!need_check) {
            return true;
        } else {
            return T::check(world, entity) && Filter<Rest...>::check(world, entity);
        }
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
struct QueryTypeBuilder<Item<Args...>, Rest...> {
    using must_include = typename TupleCat<typename QueryTypeBuilder<Rest...>::must_include,
                                           typename QueryElementInfo<QueryElement<Args>>::check_type...>::type;
    using access_type  = typename TupleCat<typename QueryTypeBuilder<Rest...>::access_type,
                                           typename QueryElementInfo<QueryElement<Args>>::access_type...>::type;
    using must_exclude = typename QueryTypeBuilder<Rest...>::must_exclude;
};
template <FilterItem T, typename... Rest>
struct QueryTypeBuilder<T, Rest...> {
    using must_include =
        typename TupleCat<typename QueryTypeBuilder<Rest...>::must_include, typename T::must_include>::type;
    using must_exclude =
        typename TupleCat<typename QueryTypeBuilder<Rest...>::must_exclude, typename T::must_exclude>::type;
    using access_type =
        typename TupleCat<typename QueryTypeBuilder<Rest...>::access_type, typename T::access_type>::type;
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
    static constexpr bool all_const = (std::is_const_v<std::remove_reference_t<TI>> && ...);
    using reg_t                     = std::conditional_t<all_const, const entt::registry, entt::registry>;
    using type                      = decltype(std::declval<reg_t>().template view<TI...>(entt::exclude<TO...>));
    static type build(entt::registry& registry)
        requires(!all_const)
    {
        return registry.template view<TI...>(entt::exclude<TO...>);
    }
    static type build(const entt::registry& registry)
        requires(all_const)
    {
        return registry.template view<TI...>(entt::exclude<TO...>);
    }
};

template <epix::util::type_traits::specialization_of<Item> G, typename F = Filter<>>
    requires(epix::util::type_traits::specialization_of<F, Filter> || FilterItem<F>)
struct Query;
template <typename... Gets, typename F>
    requires(!epix::util::type_traits::specialization_of<F, Filter> && FilterItem<F>)
struct Query<Item<Gets...>, F> : public Query<Item<Gets...>, Filter<F>> {};
template <typename... Gets, typename... Filters>
    requires(FilterItem<Filters> && ...)
struct Query<Item<Gets...>, Filter<Filters...>> {
    using must_include = typename QueryTypeBuilder<Item<Gets...>, Filters...>::must_include;
    using access_type  = typename QueryTypeBuilder<Item<Gets...>, Filters...>::access_type;
    using must_exclude = typename QueryTypeBuilder<Item<Gets...>, Filters...>::must_exclude;
    using get_type     = std::tuple<typename QueryElementInfo<QueryElement<Gets>>::get_type...>;

    using view_type     = typename EnttViewBuilder<must_include, must_exclude>::type;
    using view_iterable = decltype(std::declval<view_type>().each());
    using view_iter     = decltype(std::declval<view_iterable>().begin());

    using world_ptr = std::conditional_t<Item<Gets...>::readonly, const World*, World*>;

   private:
    world_ptr m_world;
    view_type m_view;

   public:
    Query(World& world)
        requires(!Item<Gets...>::readonly)
        : m_world(&world), m_view(EnttViewBuilder<must_include, must_exclude>::build(world.registry())) {}
    Query(const World& world)
        requires(Item<Gets...>::readonly)
        : m_world(&world), m_view(EnttViewBuilder<must_include, must_exclude>::build(world.registry())) {}

    auto iter() {
        if constexpr (Filter<Filters...>::need_check) {
            return m_view.each() | std::views::filter([this](auto&& item) {
                       return Filter<Filters...>::check(*m_world, std::get<0>(item));
                   }) |
                   std::views::transform([this](auto&& item) {
                       return get_type(QueryElement<Gets>::get(item, *m_world, std::get<0>(item))...);
                   });
        } else {
            return m_view.each() | std::views::transform([this](auto&& item) {
                       return get_type(QueryElement<Gets>::get(item, *m_world, std::get<0>(item))...);
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
            if (Filter<Filters...>::check(*m_world, entity)) {
                return get_type(QueryElement<Gets>::get(m_view.get(entity), *m_world, entity)...);
            }
        }
        return std::nullopt;
    }
    get_type get(Entity entity) { return try_get(entity).value(); }
    bool contains(Entity entity) { return m_view.contains(entity) && Filter<Filters...>::check(*m_world, entity); }
    template <typename Func>
        requires requires(Func func) { std::apply(func, std::declval<get_type>()); }
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

    static constexpr bool readonly = Item<Gets...>::readonly;
};
template <typename Q>
    requires requires(Q t) {
        typename Q::must_include;
        typename Q::access_type;
        typename Q::must_exclude;
        { Q::readonly } -> std::convertible_to<bool>;
    } && epix::util::type_traits::specialization_of<Q, Query>
struct SystemParam<Q> {
    using State = std::optional<Q>;
    State init(SystemMeta& meta) {
        auto& qs = meta.access.queries.emplace_back();
        [&]<size_t... I>(std::index_sequence<I...>) {
            // if type is const, add to reads, otherwise add to writes
            ((std::is_const_v<std::tuple_element_t<I, typename Q::access_type>>
                  ? qs.component_reads.emplace(
                        meta::type_id<std::decay_t<std::tuple_element_t<I, typename Q::access_type>>>{})
                  : qs.component_writes.emplace(
                        meta::type_id<std::decay_t<std::tuple_element_t<I, typename Q::access_type>>>{})),
             ...);
        }(std::make_index_sequence<std::tuple_size<typename Q::access_type>::value>{});
        [&]<size_t... I>(std::index_sequence<I...>) {
            (qs.component_excludes.emplace(
                 meta::type_id<std::decay_t<std::tuple_element_t<I, typename Q::must_exclude>>>{}),
             ...);
        }(std::make_index_sequence<std::tuple_size<typename Q::must_exclude>::value>{});
        return std::nullopt;
    }
    bool update(State& state, World& world, const SystemMeta& meta)
        requires(!Q::readonly)
    {
        state.emplace(Q(world));
        return true;
    }
    bool update(State& state, const World& world, const SystemMeta& meta)
        requires(Q::readonly)
    {
        state.emplace(Q(world));
        return true;
    }
    Q& get(State& state) { return state.value(); }
};
}  // namespace epix::app