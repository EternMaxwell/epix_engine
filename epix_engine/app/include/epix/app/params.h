#pragma once

#include <entt/container/dense_map.hpp>
#include <entt/container/dense_set.hpp>
#include <entt/entity/registry.hpp>
#include <memory>
#include <optional>
#include <typeindex>

#include "tool.h"
#include "world.h"

// pre-declare
namespace epix::app {
using entt::dense_map;
using entt::dense_set;
using tools::Label;

struct Entity;
struct World;
struct CommandQueue;

struct Commands;
struct EntityCommands;

template <typename T>
    requires(!std::is_reference_v<T>)
struct Res;
template <typename T>
    requires(!std::is_reference_v<T>)
struct ResMut;

template <typename... Ts>
struct Get;
template <typename... Ts>
struct With;
template <typename... Ts>
struct Without;

template <typename T>
struct Has;
template <typename T>
struct Opt;
template <typename G, typename W = With<>, typename WO = Without<>>
struct Query;

template <typename T>
struct Extract;

template <typename T>
struct Local;
struct LocalData;

template <typename T>
struct PrepareParam;
}  // namespace epix::app

// define
namespace epix::app {
struct EntityCommands {
   public:
    EPIX_API EntityCommands(
        Commands* commands, CommandQueue* command_queue, Entity entity
    );
    EPIX_API void despawn();
    EPIX_API void despawn_recurse();
    EPIX_API Entity id() const;

    template <typename... Args>
    EntityCommands spawn(Args&&... args);
    template <typename T, typename... Args>
    void emplace(Args&&... args);
    template <typename T>
    void insert(T&& t);
    template <typename... Args>
    void erase();

   private:
    World* m_world;
    Commands* m_command;
    CommandQueue* m_command_queue;
    Entity m_entity;
};
struct Commands {
   public:
    EPIX_API Commands(World* world, CommandQueue* command_queue);

    template <typename... Args>
    EntityCommands spawn(Args&&... args) noexcept;
    EPIX_API EntityCommands entity(const Entity& entity);
    EPIX_API std::optional<EntityCommands> get_entity(const Entity& entity
    ) noexcept;
    template <typename T, typename... Args>
    void emplace_resource(Args&&... args) noexcept;
    template <typename T>
    void insert_resource(T&& res) noexcept;
    template <typename T>
    void init_resource() noexcept;
    template <typename T>
    void add_resource(const std::shared_ptr<T>& res) noexcept;
    template <typename T>
    void add_resource(T* res) noexcept;
    EPIX_API void add_resource(std::type_index type, UntypedRes res);
    template <typename T>
    void remove_resource() noexcept;

   private:
    World* m_world;
    CommandQueue* m_command_queue;

    friend struct EntityCommands;
};
template <typename T>
    requires(!std::is_reference_v<T>)
struct Res {
   public:
    using decayed_type  = std::decay_t<T>;
    using resource_type = const T;

    Res(const UntypedRes& res) noexcept
        : resource(std::static_pointer_cast<decayed_type>(res.resource)),
          mutex(res.mutex) {}
    Res(const Res& other) noexcept
        : resource(other.resource), mutex(other.mutex) {}
    Res(Res&& other) noexcept : resource(other.resource), mutex(other.mutex) {}
    Res& operator=(const Res& other) noexcept {
        resource = other.resource;
        mutex    = other.mutex;
        return *this;
    }
    Res& operator=(Res&& other) noexcept {
        resource = other.resource;
        mutex    = other.mutex;
        return *this;
    }
    operator bool() const noexcept { return resource != nullptr; }
    bool operator!() const noexcept { return resource == nullptr; }

    const T& operator*() const noexcept { return *resource; }
    const T* operator->() const noexcept { return resource.get(); }
    const T* get() const noexcept { return resource.get(); }

    UntypedRes untyped() const noexcept {
        return UntypedRes{std::static_pointer_cast<void>(resource), mutex};
    }

   private:
    void lock() const noexcept {
        if (this->mutex) this->mutex->lock_shared();
    }
    void unlock() const noexcept {
        if (this->mutex) this->mutex->unlock_shared();
    }

   protected:
    std::shared_ptr<decayed_type> resource;
    std::shared_ptr<std::shared_mutex> mutex;

    template <typename T>
    friend struct PrepareParam;
};
template <typename T>
    requires(!std::is_reference_v<T>)
struct ResMut {
   public:
    using decayed_type  = std::decay_t<T>;
    using resource_type = const T;

    ResMut(const UntypedRes& res) noexcept
        : resource(std::static_pointer_cast<decayed_type>(res.resource)),
          mutex(res.mutex) {}
    ResMut(const ResMut& other) noexcept
        : resource(other.resource), mutex(other.mutex) {}
    ResMut(ResMut&& other) noexcept
        : resource(other.resource), mutex(other.mutex) {}
    ResMut& operator=(const ResMut& other) noexcept {
        resource = other.resource;
        mutex    = other.mutex;
        return *this;
    }
    ResMut& operator=(ResMut&& other) noexcept {
        resource = other.resource;
        mutex    = other.mutex;
        return *this;
    }
    operator bool() const noexcept { return resource != nullptr; }
    bool operator!() const noexcept { return resource == nullptr; }

    const T& operator*() const noexcept { return *resource; }
    const T* operator->() const noexcept { return resource.get(); }
    const T* get() const noexcept { return resource.get(); }
    T& operator*() noexcept { return *resource; }
    T* operator->() noexcept { return resource.get(); }
    T* get() noexcept { return resource.get(); }

    UntypedRes untyped() const noexcept {
        return UntypedRes{std::static_pointer_cast<void>(resource), mutex};
    }

   private:
    void lock() const noexcept {
        if (this->mutex) this->mutex->lock();
    }
    void unlock() const noexcept {
        if (this->mutex) this->mutex->unlock();
    }

   protected:
    std::shared_ptr<decayed_type> resource;
    std::shared_ptr<std::shared_mutex> mutex;

    template <typename T>
    friend struct PrepareParam;
};

// query helpers
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_pointer_v<T>)
struct GetAbs {
    using checking_type = T;  // this is for adding info to system param info
                              // for checking system conflicts
    using get_type = T&;  // the actual type inserted into the tuple returned by
                          // the query
};
template <typename T>
struct GetAbs<Has<T>> {
    using checking_type = const T;
    using get_type      = bool;
    static bool get(entt::registry& registry, Entity entity) noexcept {
        return registry.try_get<T>(entity) != nullptr;
    }
};
template <typename T>
struct GetAbs<Opt<T>> {
    using checking_type = T;
    using get_type      = T*;
    static T* get(entt::registry& registry, Entity entity) noexcept {
        return registry.try_get<T>(entity);
    }
};
template <>
struct GetAbs<Entity> {
    using checking_type = Entity;
    using get_type      = Entity;
    static Entity get(entt::registry& registry, Entity entity) noexcept {
        return entity;
    }
};
template <typename T>
concept CustomGetType = requires(T t, entt::registry& registry, Entity entity) {
    {
        GetAbs<T>::get(registry, entity)
    } -> std::same_as<typename GetAbs<T>::get_type>;
};
template <typename... T>
struct IntoQueryGetAbsolute;
template <>
struct IntoQueryGetAbsolute<> {
    using type = std::tuple<>;
};
template <typename T1, typename T2>
struct TupleCat;
template <typename... T1, typename... T2>
struct TupleCat<std::tuple<T1...>, std::tuple<T2...>> {
    using type = std::tuple<T1..., T2...>;
};
template <typename A, typename... Other>
struct IntoQueryGetAbsolute<A, Other...> {
    using type = typename TupleCat<
        std::tuple<A>,
        typename IntoQueryGetAbsolute<Other...>::type>::type;
};
template <CustomGetType A, typename... Other>
struct IntoQueryGetAbsolute<A, Other...> {
    using type = typename IntoQueryGetAbsolute<Other...>::type;
};
template <typename T, typename U>
struct FromTupleGetView;

template <typename... T, typename... U>
struct FromTupleGetView<std::tuple<T...>, std::tuple<U...>> {
    using includes = std::tuple<T...>;
    using excludes = std::tuple<U...>;
    static auto get(entt::registry& registry) noexcept {
        return registry.view<T...>(entt::exclude<U...>);
    }
};

template <typename T>
struct GetValue {
    template <typename... Args>
    static typename GetAbs<T>::get_type get(
        std::tuple<Args...>&& tuple, entt::registry& registry
    ) noexcept {
        return std::get<typename GetAbs<T>::get_type>(tuple);
    }
};
template <CustomGetType T>
struct GetValue<T> {
    template <typename... Args>
    static typename GetAbs<T>::get_type get(
        std::tuple<Args...>&& tuple, entt::registry& registry
    ) noexcept {
        return GetAbs<T>::get(registry, std::get<entt::entity>(tuple));
    }
};

template <typename T>
struct GetSingle {
    static typename GetAbs<T>::get_type get(
        entt::registry& registry, Entity entity
    ) noexcept {
        return registry.get<typename GetAbs<T>::checking_type>(entity);
    }
};
template <CustomGetType T>
struct GetSingle<T> {
    static typename GetAbs<T>::get_type get(
        entt::registry& registry, Entity entity
    ) noexcept {
        return GetAbs<T>::get(registry, entity);
    }
};

// template <typename G, typename W, typename WO>
// struct Query<G, W, WO> {
//     static_assert(
//         false, "Query type not supported. Use Query<Get<>, With<>,
//         Without<>>"
//     );
// };

template <typename... Gets, typename... Withs, typename... Withouts>
struct Query<Get<Gets...>, With<Withs...>, Without<Withouts...>> {
    using get_type = typename IntoQueryGetAbsolute<Gets..., Withs...>::type;
    using view_type =
        decltype(FromTupleGetView<get_type, std::tuple<Withouts...>>::get(
            std::declval<entt::registry&>()
        ));
    using tuple_type    = std::tuple<typename GetAbs<Gets>::get_type...>;
    using iterable_type = decltype(std::declval<view_type>().each());
    using iterator_type = decltype(std::declval<iterable_type>().begin());

    struct iterable {
       private:
        iterable_type m_full;
        entt::registry& m_registry;

        struct iterator {
           private:
            iterator_type m_iter;
            entt::registry& m_registry;

           public:
            iterator(iterator_type iter, entt::registry& registry)
                : m_iter(iter), m_registry(registry) {}
            iterator& operator++() noexcept {
                m_iter++;
                return *this;
            }
            iterator operator++(int) noexcept {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }
            bool operator==(const iterator& rhs) const noexcept {
                return m_iter == rhs.m_iter;
            }
            bool operator!=(const iterator& rhs) const noexcept {
                return m_iter != rhs.m_iter;
            }
            tuple_type operator*() noexcept {
                // explicitly declaring tuple type to avoid returning temporary
                // rvalue in tuple
                return tuple_type(GetValue<Gets>::get(*m_iter, m_registry)...);
            }
        };

       public:
        iterable(iterable_type full, entt::registry& registry) noexcept
            : m_full(full), m_registry(registry) {}
        auto begin() noexcept { return iterator(m_full.begin(), m_registry); }
        auto end() noexcept { return iterator(m_full.end(), m_registry); }
    };

   private:
    entt::registry& registry;
    view_type m_view;

   public:
    Query(entt::registry& registry) : registry(registry) {
        m_view =
            FromTupleGetView<get_type, std::tuple<Withouts...>>::get(registry);
    }
    Query(const Query&) noexcept             = default;
    Query(Query&& other) noexcept            = default;
    Query& operator=(const Query&) noexcept  = default;
    Query& operator=(Query&& other) noexcept = default;

    /*! @brief Get the iterator for the query.
     * @return The iterator for the query.
     */
    auto iter() noexcept { return iterable(m_view.each(), registry); }
    auto get(entt::entity id) noexcept {
        // explicitly declaring tuple type to avoid returning temporary rvalue
        // in tuple
        return std::tuple<typename GetAbs<Gets>::get_type...>(
            GetSingle<Gets>::get(registry, id)...
        );
    }
    bool contains(entt::entity id) noexcept { return m_view.contains(id); }
    /*! @brief Get the single entity and requaired components.
     * @return An optional of a single tuple of entity and requaired
     * components.
     */
    auto single() {
        // auto start = *(iter().begin());
        if (iter().begin() == iter().end()) throw std::bad_optional_access();
        return *(iter().begin());
    }

    operator bool() noexcept { return iter().begin() != iter().end(); }
    bool operator!() noexcept { return iter().begin() == iter().end(); }

    auto size_hint() noexcept { return m_view.size_hint(); }

    template <typename Func>
    void for_each(Func&& func) {
        m_view.each(std::forward<Func>(func));
    }
};
template <typename... Gets, typename... Withouts, typename W>
struct Query<Get<Gets...>, Without<Withouts...>, W>
    : public Query<Get<Gets...>, With<>, Without<Withouts...>> {};

template <typename T>
struct Extract : public T {
    using value_type = T;
    using T::T;
    Extract(const T& t) : T(t) {}
    Extract(T&& t) : T(std::move(t)) {}
    Extract(const Extract& other) : T(other) {}
    Extract(Extract&& other) : T(std::move(other)) {}
    Extract& operator=(const T& other) {
        T::operator=(other);
        return *this;
    }
    Extract& operator=(T&& other) {
        T::operator=(std::move(other));
        return *this;
    }
    Extract& operator=(const Extract& other) {
        T::operator=(other);
        return *this;
    }
    Extract& operator=(Extract&& other) {
        T::operator=(std::move(other));
        return *this;
    }
};

template <typename T>
struct Local {
    using value_type = T;
    Local(T* t) : t(t) {}
    T& operator*() { return *t; }
    T* operator->() { return t; }
    const T& operator*() const { return *t; }
    const T* operator->() const { return t; }

   private:
    T* t;
};
struct LocalData {
   private:
    entt::dense_map<std::type_index, std::shared_ptr<void>> m_locals;

   public:
    template <typename T>
    auto get() {
        auto it = m_locals.find(typeid(T));
        if (it == m_locals.end()) {
            m_locals.emplace(typeid(T), std::make_shared<T>());
            return m_locals.at(typeid(T));
        }
        return it->second;
    }
};

template <typename T>
struct GetWorldParam {
    static_assert(
        false,
        "GetWorldParam not supported for this type. This type maybe passed "
        "here by GetParams."
    );
};
template <>
struct GetWorldParam<Commands> {
    using type = Commands;
    static type get(World& world) noexcept {
        return type(&world, &world.m_command_queue);
    }
};
template <>
struct GetWorldParam<World> {
    using type = World&;
    static type get(World& world) noexcept { return world; }
};
template <>
struct GetWorldParam<World&> {
    using type = World&;
    static type get(World& world) noexcept { return world; }
};
template <typename T>
struct GetWorldParam<Res<T>> {
    using type = Res<T>;
    static type get(World& world) noexcept {
        return Res<T>(world.m_resources[typeid(T)]);
    }
};
template <typename T>
struct GetWorldParam<ResMut<T>> {
    using type = ResMut<T>;
    static type get(World& world) noexcept {
        return ResMut<T>(world.m_resources[typeid(T)]);
    }
};
template <typename G, typename W, typename WO>
struct GetWorldParam<Query<G, W, WO>> {
    using type = Query<G, W, WO>;
    static type get(World& world) noexcept { return type(world.m_registry); }
};

/**
 * @brief This type handles raw parameters and extract raw parameters, e.g.
 * let U be raw parameters (World&, Res, ResMut, Query, Commands)
 *
 * @tparam T
 */
template <typename T>
struct GetParams {
    using type = typename GetWorldParam<T>::type;
    static typename GetWorldParam<T>::type get(
        World* src, World* dst, LocalData* local_data
    ) {
        return GetWorldParam<T>::get(*dst);
    }
};
template <typename T>
struct GetParams<Extract<T>> {
    using type = Extract<T>;
    static typename GetWorldParam<T>::type get(
        World* src, World* dst, LocalData* local_data
    ) {
        return GetWorldParam<T>::get(*src);
    }
};
template <typename T>
struct GetParams<Local<T>> {
    using type = Local<T>;
    static Local<T> get(World* src, World* dst, LocalData* local_data) {
        return Local<T>(std::static_pointer_cast<typename Local<T>::value_type>(
                            local_data->get<typename Local<T>::value_type>()
        )
                            .get());
    }
};
template <typename... Args>
struct GetParams<std::tuple<Args...>> {
    using type = std::tuple<typename GetParams<Args>::type...>;
    static std::tuple<Args...> get(
        World* src, World* dst, LocalData* local_data
    ) {
        return std::forward_as_tuple(
            GetParams<Args>::get(src, dst, local_data)...
        );
    }
};
}  // namespace epix::app