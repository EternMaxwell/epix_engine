#pragma once

// ----THIRD PARTY INCLUDES----
#include <index/array_proxy.h>
#include <index/concurrent/channel.h>
#include <index/concurrent/conqueue.h>
#include <index/traits/template.h>
#include <index/traits/tuple.h>
#include <spdlog/spdlog.h>

#define BS_THREAD_POOL_NATIVE_EXTENSIONS
#include <BS_thread_pool.hpp>
#include <entt/container/dense_set.hpp>
#include <entt/entity/registry.hpp>

// ----THIRD PARTY INCLUDES----

// ----STANDARD LIBRARY INCLUDES----
#include <chrono>
#include <concepts>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <source_location>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>
// ----STANDARD LIBRARY INCLUDES----

// ----EPIX API----
#include "epix/common.h"
// ----EPIX API----

#if defined(EPIX_ENABLE_TRACY) && defined(NDEBUG)
#include <tracy/Tracy.hpp>
#else
#undef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace epix::app_tools {
template <typename T>
struct t_weak_ptr : std::weak_ptr<T> {
    t_weak_ptr(std::shared_ptr<T> ptr) : std::weak_ptr<T>(ptr) {}
    t_weak_ptr(std::weak_ptr<T> ptr) : std::weak_ptr<T>(ptr) {}

    T* get_p() { return *reinterpret_cast<T**>(this); }
};
}  // namespace epix::app_tools

template <typename T>
struct std::hash<std::weak_ptr<T>> {
    size_t operator()(const std::weak_ptr<T>& ptr) const {
        epix::app_tools::t_weak_ptr<T> tptr(ptr);
        return std::hash<T*>()(tptr.get_p());
    }
};

template <typename T>
struct std::equal_to<std::weak_ptr<T>> {
    bool operator()(const std::weak_ptr<T>& a, const std::weak_ptr<T>& b)
        const {
        epix::app_tools::t_weak_ptr<T> aptr(a);
        epix::app_tools::t_weak_ptr<T> bptr(b);
        return aptr.get_p() == bptr.get_p();
    }
};

namespace epix::app {
struct World;
template <typename T>
static constexpr bool external_thread_safe_v = false;

struct FuncIndex;
struct Entity;
struct Children;

template <typename Ret>
struct BasicSystem;

struct SystemSet;
struct System;
struct SystemAddInfo;

template <typename T>
struct EventReader;
template <typename T>
struct EventWriter;
template <typename T>
struct State;
template <typename T>
struct NextState;

template <typename T>
struct Res;
template <typename T>
struct ResMut;
struct UntypedRes;

template <typename... T>
struct Get;
template <typename... T>
struct With;
template <typename... T>
struct Without;

template <typename G, typename W = With<>, typename WO = Without<>>
struct Query;

template <typename T>
struct Extract;
template <typename T>
struct Local;

struct Command;
struct EntityCommand;

struct Schedule;
struct ScheduleId;
struct ScheduleInfo;

struct App;

using thread_pool = BS::thread_pool<BS::tp::priority>;
}  // namespace epix::app

template <>
struct std::hash<epix::app::FuncIndex> {
    EPIX_API size_t operator()(const epix::app::FuncIndex& func) const;
};
template <>
struct std::hash<epix::app::Entity> {
    EPIX_API size_t operator()(const epix::app::Entity& entity) const;
};
template <>
struct std::equal_to<epix::app::Entity> {
    EPIX_API bool operator()(
        const epix::app::Entity& a, const epix::app::Entity& b
    ) const;
};
template <>
struct std::hash<epix::app::ScheduleId> {
    EPIX_API size_t operator()(const epix::app::ScheduleId& id) const;
};

namespace epix::app {
struct Entity {
    entt::entity id = entt::null;
    EPIX_API Entity& operator=(entt::entity id);
    EPIX_API operator entt::entity();
    EPIX_API operator bool();
    EPIX_API bool operator!();
    EPIX_API bool operator==(const Entity& other);
    EPIX_API bool operator!=(const Entity& other);
    EPIX_API bool operator==(const entt::entity& other);
    EPIX_API bool operator!=(const entt::entity& other);
};
struct FuncIndex {
    std::type_index type;
    void* func;
    template <typename T, typename... Args>
    FuncIndex(T (*func)(Args...)) : type(typeid(T)), func((void*)(func)) {}
    FuncIndex() : type(typeid(void)), func(nullptr) {}
    EPIX_API bool operator==(const FuncIndex& other) const;
};

struct Children {
    entt::dense_set<Entity> children;
};
struct Parent {
    Entity id;
};

template <typename T>
struct Local {
    using value_type = T;
    Local(T* t) : t(t) {}
    T& operator*() { return *t; }
    T* operator->() { return t; }

   private:
    T* t;
};
template <typename T>
struct State {
    friend class app::App;

   protected:
    T m_state;
    bool just_created = true;

   public:
    State() : m_state() {}
    State(const T& state) : m_state(state) {}
    bool is_just_created() const { return just_created; }

    bool is_state(const T& state) const { return m_state == state; }
    bool is_state(const NextState<T>& state) const {
        return m_state == state.m_state;
    }
};
template <typename T>
struct NextState : public State<T> {
   public:
    NextState() : State<T>() {}
    NextState(const T& state) : State<T>(state) {}

    void set_state(const T& state) { State<T>::m_state = state; }
};
template <typename T>
struct Res {
   private:
    std::shared_ptr<T> m_res;
    std::shared_ptr<std::shared_mutex> m_mutex;

   public:
    Res(const std::shared_ptr<void>& resource,
        const std::shared_ptr<std::shared_mutex>& mutex)
        : m_res(std::static_pointer_cast<T>(resource)), m_mutex(mutex) {
        if constexpr (external_thread_safe_v<T>) {
            return;
        }
        if (m_mutex) m_mutex->lock_shared();
    }
    Res() : m_res(nullptr), m_mutex(nullptr) {}
    Res(const Res<T>& other) = delete;
    Res(Res<T>&& other) {
        m_res   = std::move(other.m_res);
        m_mutex = std::move(other.m_mutex);
    }
    Res& operator=(const Res<T>& other) = delete;
    Res& operator=(Res<T>&& other) {
        m_res   = std::move(other.m_res);
        m_mutex = std::move(other.m_mutex);
        return *this;
    }

    /**
     * @brief Check if the resource has a value.
     */
    bool has_value() { return m_res != nullptr; }

    operator bool() { return has_value(); }
    bool operator!() { return !has_value(); }

    operator ResMut<T>() { return ResMut<T>(m_res, m_mutex); }

    const T& operator*() { return *m_res; }
    const T* operator->() { return m_res.get(); }

    const T* get() { return m_res.get(); }

    ~Res() {
        if constexpr (external_thread_safe_v<T>) {
            return;
        }
        if (m_mutex) m_mutex->unlock_shared();
    }
};
template <typename T>
struct ResMut {
   private:
    std::shared_ptr<T> m_res;
    std::shared_ptr<std::shared_mutex> m_mutex;

   public:
    ResMut(
        const std::shared_ptr<void>& resource,
        const std::shared_ptr<std::shared_mutex>& mutex
    )
        : m_res(std::static_pointer_cast<T>(resource)), m_mutex(mutex) {
        if constexpr (external_thread_safe_v<T>) {
            return;
        }
        if (m_mutex) m_mutex->lock();
    }
    ResMut() : m_res(nullptr), m_mutex(nullptr) {}
    ResMut(const ResMut<T>& other) = delete;
    ResMut(ResMut<T>&& other) {
        m_res   = std::move(other.m_res);
        m_mutex = std::move(other.m_mutex);
    }
    ResMut& operator=(const ResMut<T>& other) = delete;
    ResMut& operator=(ResMut<T>&& other) {
        m_res   = std::move(other.m_res);
        m_mutex = std::move(other.m_mutex);
        return *this;
    }

    /**
     * @brief Check if the resource has a value.
     */
    bool has_value() { return m_res != nullptr; }

    operator bool() { return has_value(); }
    bool operator!() { return !has_value(); }

    operator Res<T>() { return Res<T>(m_res, m_mutex); }

    T& operator*() { return *m_res; }
    T* operator->() { return m_res.get(); }

    T* get() { return m_res.get(); }

    ~ResMut() {
        if constexpr (external_thread_safe_v<T>) {
            return;
        }
        if (m_mutex) m_mutex->unlock();
    }
};
struct UntypedRes {
    std::shared_ptr<void> resource;
    std::shared_ptr<std::shared_mutex> mutex;

    template <typename T>
    Res<T> into() {
        return Res<T>(resource, mutex);
    }
    template <typename T>
    ResMut<T> into_mut() {
        return ResMut<T>(resource, mutex);
    }
};
struct TickableEventQueue {
    virtual void tick() = 0;
};
template <typename T>
struct EventQueue : public TickableEventQueue {
   private:
    std::deque<T> m_queue;
    std::deque<int> m_lifetime;
    mutable std::shared_mutex m_mutex;

   public:
    EventQueue()                             = default;
    EventQueue(const EventQueue&)            = delete;
    EventQueue(EventQueue&&)                 = delete;
    EventQueue& operator=(const EventQueue&) = delete;
    EventQueue& operator=(EventQueue&&)      = delete;
    ~EventQueue()                            = default;

    void tick() override {
        std::unique_lock lock(m_mutex);
        while (!m_queue.empty()) {
            auto& front = m_queue.front();
            if (m_lifetime.front() == 0) {
                m_queue.pop_front();
                m_lifetime.pop_front();
            } else {
                break;
            }
        }
        for (auto it = m_lifetime.begin(); it != m_lifetime.end(); it++) {
            (*it)--;
        }
    }

    template <typename... Args>
    void emplace(Args&&... args) {
        std::unique_lock lock(m_mutex);
        m_queue.emplace_back(std::forward<Args>(args)...);
        m_lifetime.emplace_back(1);
    }
    void push(T&& value) {
        std::unique_lock lock(m_mutex);
        m_queue.emplace_back(std::move(value));
        m_lifetime.emplace_back(1);
    }
    void push(const T& value) {
        std::unique_lock lock(m_mutex);
        m_queue.push_back(value);
        m_lifetime.push_back(1);
    }
    void clear() {
        std::unique_lock lock(m_mutex);
        m_queue.clear();
        m_lifetime.clear();
    }
    bool empty() const {
        std::shared_lock lock(m_mutex);
        return m_queue.empty();
    }
    std::optional<T> try_pop() {
        std::unique_lock lock(m_mutex);
        if (m_queue.empty()) return std::nullopt;
        T value = std::move(m_queue.front());
        m_queue.pop_front();
        m_lifetime.pop_front();
        return std::move(value);
    }
    struct iterator {
        struct iter {
            typename std::deque<T>::iterator it;
            iter operator++() {
                it++;
                return *this;
            }
            bool operator!=(const iter& other) { return it != other.it; }
            const T& operator*() { return *it; }
        };

       private:
        EventQueue* queue;
        std::shared_lock<std::shared_mutex> lock;

       public:
        iterator(EventQueue* queue) : queue(queue), lock(queue->m_mutex) {}
        iter begin() { return iter{queue->m_queue.begin()}; }
        iter end() { return iter{queue->m_queue.end()}; }
    };
    iterator read() { return iterator(this); }
};
template <typename T>
struct EventReader {
   private:
    std::shared_ptr<EventQueue<T>> m_queue;

   public:
    EventReader(const std::shared_ptr<TickableEventQueue>& queue)
        : m_queue(std::dynamic_pointer_cast<EventQueue<T>>(queue)) {}
    EventReader(const std::shared_ptr<EventQueue<T>>& queue) : m_queue(queue) {}

    operator bool() { return m_queue != nullptr; }

    auto read() { return m_queue->read(); }
    void clear() { m_queue->clear(); }
    bool empty() { return m_queue->empty(); }
    std::optional<T> try_get() { return m_queue->try_pop(); }
};
template <typename T>
struct EventWriter {
   private:
    std::shared_ptr<EventQueue<T>> m_queue;

   public:
    EventWriter(const std::shared_ptr<TickableEventQueue>& queue)
        : m_queue(std::dynamic_pointer_cast<EventQueue<T>>(queue)) {}
    EventWriter(const std::shared_ptr<EventQueue<T>>& queue) : m_queue(queue) {}

    operator bool() { return m_queue != nullptr; }

    EventWriter& write(const T& event) {
        m_queue->push(event);
        return *this;
    }
    EventWriter& write(T&& event) {
        m_queue->push(std::move(event));
        return *this;
    }

    friend struct App;
};
template <typename T>
concept is_bundle = requires(T t) {
    { t.unpack() };
};
struct WorldCommand;
struct WorldEntityCommand;
struct WorldEntityCommand {
   private:
    World* m_world;
    WorldCommand* m_command;
    Entity m_entity;

   public:
    EPIX_API WorldEntityCommand(
        World* world, WorldCommand* command, Entity entity
    );
    EPIX_API void despawn();
    EPIX_API void despawn_recurse();
    template <typename T, typename... Args>
    void emplace(Args&&... args);
    template <typename T>
    void emplace(T&& t);
    template <typename... Args>
    void erase();

    template <typename... Args>
    Entity spawn(Args&&... args);
};
struct WorldCommand {
   private:
    World* m_world;
    index::concurrent::conqueue<Entity> m_despawn;
    index::concurrent::conqueue<Entity> m_recurse_despawn;
    index::concurrent::conqueue<std::type_index> m_remove_resources;
    index::concurrent::conqueue<std::pair<void (*)(World*, Entity), Entity>>
        m_entity_erase;

   public:
    EPIX_API WorldCommand(World* world);
    EPIX_API void flush();
    EPIX_API void flush_relax();
    template <typename... Args>
    Entity spawn(Args&&... args);

    EPIX_API WorldEntityCommand entity(Entity entity);

    template <typename T>
    void insert_resource(T&& res);
    template <typename T>
    void init_resource();
    template <typename T, typename... Args>
    void emplace_resource(Args&&... args);
    template <typename T>
    void add_resource(const std::shared_ptr<T>& res);
    template <typename T>
    void add_resource(T* res);
    template <typename T>
    void add_resource(
        const std::shared_ptr<T>& res,
        const std::shared_ptr<std::shared_mutex>& mutex
    );
    template <typename T>
    void remove_resource();

    friend struct Command;
    friend struct WorldEntityCommand;
};
struct EntityCommand : public WorldEntityCommand {
    EntityCommand(World* world, WorldCommand* command, Entity entity)
        : WorldEntityCommand(world, command, entity) {}
};
struct Command {
   private:
    WorldCommand* src_cmd;
    WorldCommand* dst_cmd;

   public:
    EPIX_API Command(WorldCommand* src, WorldCommand* dst);

    template <typename... Args>
    Entity spawn(Args&&... args);
    template <typename T, typename... Args>
    void emplace_resource(Args&&... args);
    template <typename T>
    void insert_resource(T&& res);
    template <typename T>
    void init_resource();
    template <typename T>
    void add_resource(const std::shared_ptr<T>& res);
    template <typename T>
    void add_resource(T* res);
    template <typename T>
    void share_resource();
    template <typename T>
    void share_resource(Res<T>& res);
    template <typename T>
    void share_resource(ResMut<T>& res);
    template <typename T>
    void remove_resource();

    EPIX_API EntityCommand entity(Entity entity);
};
template <typename... T>
struct Get {};
template <typename... T>
struct With {};
template <typename... T>
struct Without {};
template <typename... Gets, typename... Withs, typename... Withouts>
struct Query<Get<Gets...>, With<Withs...>, Without<Withouts...>> {
    using view_type =
        decltype(std::declval<entt::registry>().view<Gets..., Withs...>(
            entt::exclude_t<Withouts...>{}
        ));
    using iterable_type = decltype(std::declval<view_type>().each());
    using iterator_type = decltype(std::declval<iterable_type>().begin());

    struct iterable {
       private:
        iterable_type m_full;

        struct iterator {
           private:
            iterator_type m_iter;

           public:
            iterator(iterator_type iter) : m_iter(iter) {}
            iterator& operator++() {
                m_iter++;
                return *this;
            }
            iterator operator++(int) {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }
            bool operator==(const iterator& rhs) const {
                return m_iter == rhs.m_iter;
            }
            bool operator!=(const iterator& rhs) const {
                return m_iter != rhs.m_iter;
            }
            auto operator*() {
                return std::tuple<Gets&...>(std::get<Gets&>(*m_iter)...);
            }
        };

       public:
        iterable(iterable_type full) : m_full(full) {}
        auto begin() { return iterator(m_full.begin()); }
        auto end() { return iterator(m_full.end()); }
    };

   private:
    entt::registry& registry;
    view_type m_view;

   public:
    Query(entt::registry& registry) : registry(registry) {
        m_view =
            registry.view<Gets..., Withs...>(entt::exclude_t<Withouts...>{});
    }

    /*! @brief Get the iterator for the query.
     * @return The iterator for the query.
     */
    auto iter() { return iterable(m_view.each()); }
    std::tuple<Gets&...> get(entt::entity id) {
        return m_view.template get<Gets...>(id);
    }
    bool contains(entt::entity id) { return m_view.contains(id); }
    /*! @brief Get the single entity and requaired components.
     * @return An optional of a single tuple of entity and requaired
     * components.
     */
    auto single() {
        // auto start = *(iter().begin());
        if (iter().begin() == iter().end()) throw std::bad_optional_access();
        return *(iter().begin());
    }

    operator bool() { return iter().begin() != iter().end(); }
    bool operator!() { return iter().begin() == iter().end(); }

    auto size_hint() { return m_view.size_hint(); }

    template <typename Func>
    void for_each(Func func) {
        m_view.each(func);
    }
};
template <typename... Gets, typename... Withs, typename... Withouts>
struct Query<Get<Entity, Gets...>, With<Withs...>, Without<Withouts...>> {
    using view_type =
        decltype(std::declval<entt::registry>().view<Gets..., Withs...>(
            entt::exclude_t<Withouts...>{}
        ));
    using iterable_type = decltype(std::declval<view_type>().each());
    using iterator_type = decltype(std::declval<iterable_type>().begin());

    class iterable {
       private:
        iterable_type m_full;

        struct iterator {
           private:
            iterator_type m_iter;

           public:
            iterator(iterator_type iter) : m_iter(iter) {}
            iterator& operator++() {
                m_iter++;
                return *this;
            }
            iterator operator++(int) {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }
            bool operator==(const iterator& rhs) const {
                return m_iter == rhs.m_iter;
            }
            bool operator!=(const iterator& rhs) const {
                return m_iter != rhs.m_iter;
            }
            auto operator*() {
                return std::tuple<Entity, Gets&...>(
                    Entity{std::get<entt::entity>(*m_iter)},
                    std::get<Gets&>(*m_iter)...
                );
            }
        };

       public:
        iterable(decltype(m_full) full) : m_full(full) {}
        auto begin() { return iterator(m_full.begin()); }
        auto end() { return iterator(m_full.end()); }
    };

   private:
    entt::registry& registry;
    view_type m_view;

   public:
    Query(entt::registry& registry) : registry(registry) {
        m_view =
            registry.view<Gets..., Withs...>(entt::exclude_t<Withouts...>{});
    }

    /*! @brief Get the iterator for the query.
     * @return The iterator for the query.
     */
    auto iter() { return iterable(m_view.each()); }
    std::tuple<Gets&...> get(entt::entity id) {
        return m_view.template get<Gets...>(id);
    }
    bool contains(entt::entity id) { return m_view.contains(id); }
    /*! @brief Get the single entity and requaired components.
     * @return An optional of a single tuple of entity and requaired
     * components.
     */
    auto single() {
        // auto start = *(iter().begin());
        if (iter().begin() == iter().end()) throw std::bad_optional_access();
        return *(iter().begin());
    }

    operator bool() { return iter().begin() != iter().end(); }
    bool operator!() { return iter().begin() == iter().end(); }

    auto size_hint() { return m_view.size_hint(); }

    template <typename Func>
    void for_each(Func func) {
        m_view.each(func);
    }
};
template <typename... Gets, typename... Withouts, typename W>
struct Query<Get<Gets...>, Without<Withouts...>, W>
    : public Query<Get<Gets...>, With<>, Without<Withouts...>> {};
template <typename T>
struct Extract : public T {
    template <typename... Args>
    Extract(Args&&... args) : T(std::forward<Args>(args)...) {}
    Extract(const T& other) : T(other) {}
    Extract(T&& other) : T(std::move(other)) {}
    Extract(const Extract& other) : T(other) {}
    Extract(Extract&& other) : T(std::move(other)) {}
    Extract& operator=(const Extract& other) {
        T::operator=(other);
        return *this;
    }
    Extract& operator=(Extract&& other) {
        T::operator=(std::move(other));
        return *this;
    }
};
struct World {
   private:
    entt::registry m_registry;
    entt::dense_map<std::type_index, UntypedRes> m_resources;
    std::shared_mutex m_resources_mutex;
    entt::dense_map<std::type_index, std::shared_ptr<TickableEventQueue>>
        m_events;
    WorldCommand m_command;

   public:
    World() : m_resources(), m_resources_mutex(), m_events(), m_command(this) {}

    template <typename T>
    UntypedRes resource() {
        auto&& it = m_resources.find(typeid(T));
        if (it == m_resources.end()) return UntypedRes{};
        return it->second;
    }
    EPIX_API UntypedRes resource(std::type_index type);
    template <typename T>
    void init_resource() {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::remove_cvref_t<T>))) {
            m_resources.emplace(
                typeid(T),
                UntypedRes{
                    std::static_pointer_cast<void>(
                        std::make_shared<std::remove_cvref_t<T>>()
                    ),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T>
    void insert_resource(T&& res) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::remove_cvref_t<T>))) {
            m_resources.emplace(
                typeid(T),
                UntypedRes{
                    std::static_pointer_cast<void>(
                        std::make_shared<std::remove_cvref_t<T>>(
                            std::forward<T>(res)
                        )
                    ),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T, typename... Args>
    void emplace_resource(Args&&... args) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::remove_cvref_t<T>))) {
            m_resources.emplace(
                typeid(T),
                UntypedRes{
                    std::static_pointer_cast<void>(
                        std::make_shared<std::remove_cvref_t<T>>(
                            std::forward<Args>(args)...
                        )
                    ),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T>
    void add_resource(const std::shared_ptr<T>& res) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::remove_cvref_t<T>))) {
            m_resources.emplace(
                typeid(T),
                UntypedRes{
                    std::static_pointer_cast<void>(res),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T>
    void add_resource(
        const std::shared_ptr<T>& res,
        const std::shared_ptr<std::shared_mutex>& mutex
    ) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::remove_cvref_t<T>))) {
            m_resources.emplace(
                typeid(T),
                UntypedRes{std::static_pointer_cast<void>(res), mutex}
            );
        }
    }
    template <typename T>
    void add_resource(T* res) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::remove_cvref_t<T>))) {
            m_resources.emplace(
                typeid(T),
                UntypedRes{
                    std::static_pointer_cast<void>(std::shared_ptr<T>(res)),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T>
    void add_resource(const UntypedRes& res) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::remove_cvref_t<T>))) {
            m_resources.emplace(typeid(T), res);
        }
    }
    EPIX_API void add_resource(std::type_index type, UntypedRes res);
    template <typename T>
    void remove_resource() {
        std::unique_lock lock(m_resources_mutex);
        auto it = m_resources.find(typeid(T));
        if (it != m_resources.end()) {
            m_resources.erase(it);
        }
    }

    template <typename T>
    void add_event() {
        if (!m_events.contains(typeid(T))) {
            m_events.emplace(typeid(T), std::make_shared<EventQueue<T>>());
        }
    }

    template <typename T, typename... Args>
        requires std::constructible_from<std::remove_cvref_t<T>, Args...> ||
                 is_bundle<T>
    void entity_emplace(Entity entity, Args&&... args) {
        if constexpr (is_bundle<T>) {
            auto&& bundle = T(std::forward<Args>(args)...);
            entity_emplace_tuple(entity, std::move(bundle.unpack()));
        } else {
            m_registry.emplace<std::remove_cvref_t<T>>(
                entity, std::forward<Args>(args)...
            );
        }
    }
    template <typename... Args>
    void entity_emplace_tuple(Entity entity, std::tuple<Args...>&& args) {
        entity_emplace_tuple(
            entity, std::move(args), std::index_sequence_for<Args...>()
        );
    }
    template <typename... Args, size_t... I>
    void
    entity_emplace_tuple(Entity entity, std::tuple<Args...>&& args, std::index_sequence<I...>) {
        (entity_emplace<decltype(std::get<I>(args))>(
             entity, std::forward<std::tuple_element_t<I, std::tuple<Args...>>>(
                         std::get<I>(args)
                     )
         ),
         ...);
    }
    template <typename... Args>
    void entity_erase(Entity entity) {
        m_registry.remove<Args...>(entity);
    }
    template <typename... Args>
    Entity spawn(Args&&... args) {
        auto entity = m_registry.create();
        (entity_emplace<Args>(Entity{entity}, std::forward<Args>(args)), ...);
        return Entity{entity};
    }

    template <typename T>
    struct param_type {
        using type = T;
    };

    friend struct Schedule;
    friend struct WorldCommand;
    friend struct WorldEntityCommand;
};

template <typename T>
struct World::param_type<Res<T>> {
    using type = Res<T>;
    static Res<T> get(World* src, World* dst) {
        return std::move(dst->resource<T>().template into<T>());
    }
};
template <typename T>
struct World::param_type<ResMut<T>> {
    using type = ResMut<T>;
    static ResMut<T> get(World* src, World* dst) {
        return std::move(dst->resource<T>().template into_mut<T>());
    }
};
template <typename T>
struct World::param_type<EventReader<T>> {
    using type = EventReader<T>;
    static EventReader<T> get(World* src, World* dst) {
        auto&& it = dst->m_events.find(typeid(T));
        if (it == dst->m_events.end()) {
            spdlog::error("Event {} not found", typeid(T).name());
            throw std::runtime_error("Event not found");
        }
        return EventReader<T>(it->second);
    }
};
template <typename T>
struct World::param_type<EventWriter<T>> {
    using type = EventWriter<T>;
    static EventWriter<T> get(World* src, World* dst) {
        auto&& it = dst->m_events.find(typeid(T));
        if (it == dst->m_events.end()) {
            spdlog::error("Event {} not found", typeid(T).name());
            throw std::runtime_error("Event not found");
        }
        return EventWriter<T>(it->second);
    }
};
template <typename G, typename W, typename WO>
struct World::param_type<Query<G, W, WO>> {
    using type = Query<G, W, WO>;
    static Query<G, W, WO> get(World* src, World* dst) {
        return Query<G, W, WO>(dst->m_registry);
    }
};
template <>
struct World::param_type<Command> {
    using type = Command;
    static Command get(World* src, World* dst) {
        return Command(&src->m_command, &dst->m_command);
    }
};

template <typename T>
struct World::param_type<Extract<Res<T>>> {
    using type = Extract<Res<T>>;
    static Extract<Res<T>> get(World* src, World* dst) {
        return std::move(src->resource<T>().template into<T>());
    }
};
template <typename T>
struct World::param_type<Extract<ResMut<T>>> {
    using type = Extract<ResMut<T>>;
    static Extract<ResMut<T>> get(World* src, World* dst) {
        return std::move(src->resource<T>().template into_mut<T>());
    }
};
template <typename T>
struct World::param_type<Extract<EventReader<T>>> {
    using type = Extract<EventReader<T>>;
    static Extract<EventReader<T>> get(World* src, World* dst) {
        return param_type<EventReader<T>>::get(dst, src);
    }
};
template <typename T>
struct World::param_type<Extract<EventWriter<T>>> {
    using type = Extract<EventWriter<T>>;
    static Extract<EventWriter<T>> get(World* src, World* dst) {
        return param_type<EventWriter<T>>::get(dst, src);
    }
};
template <typename G, typename W, typename WO>
struct World::param_type<Extract<Query<G, W, WO>>> {
    using type = Extract<Query<G, W, WO>>;
    static Extract<Query<G, W, WO>> get(World* src, World* dst) {
        return Extract<Query<G, W, WO>>(src->m_registry);
    }
};

template <typename Ret>
struct BasicSystem {
   protected:
    entt::dense_map<std::type_index, std::shared_ptr<void>> m_locals;
    std::function<Ret(World*, World*, BasicSystem*)> m_func;
    double factor;
    double avg_time;  // in milliseconds
    struct param_info {
        bool has_command = false;
        bool has_query   = false;
        std::vector<std::tuple<
            entt::dense_set<std::type_index>,
            entt::dense_set<std::type_index>,
            entt::dense_set<std::type_index>>>
            query_types;
        entt::dense_set<std::type_index> resource_types;
        entt::dense_set<std::type_index> resource_const;
        entt::dense_set<std::type_index> event_read_types;
        entt::dense_set<std::type_index> event_write_types;

        bool conflict_with(const param_info& other) const {
            // systems with command cannot run parallelly with systems with
            // command or query
            if (has_command && (other.has_command || other.has_query)) {
                return true;
            }
            if (other.has_command && (has_command || has_query)) {
                return true;
            }
            // check if queries conflict
            static auto query_conflict =
                [](const std::tuple<
                       entt::dense_set<std::type_index>,
                       entt::dense_set<std::type_index>,
                       entt::dense_set<std::type_index>>& query,
                   const std::tuple<
                       entt::dense_set<std::type_index>,
                       entt::dense_set<std::type_index>,
                       entt::dense_set<std::type_index>>& other_query) -> bool {
                auto&& [get_a, with_a, without_a] = query;
                auto&& [get_b, with_b, without_b] = other_query;
                for (auto& type : without_a) {
                    if (get_b.contains(type) || with_b.contains(type))
                        return false;
                }
                for (auto& type : without_b) {
                    if (get_a.contains(type) || with_a.contains(type))
                        return false;
                }
                for (auto& type : get_a) {
                    if (get_b.contains(type) || with_b.contains(type))
                        return true;
                }
                for (auto& type : get_b) {
                    if (get_a.contains(type) || with_a.contains(type))
                        return true;
                }
                return false;
            };
            for (auto& query : query_types) {
                for (auto& other_query : other.query_types) {
                    if (query_conflict(query, other_query)) {
                        return true;
                    }
                }
            }
            // check if resources conflict
            for (auto& res : resource_types) {
                if (other.resource_types.contains(res) ||
                    other.resource_const.contains(res)) {
                    return true;
                }
            }
            for (auto& res : other.resource_types) {
                if (resource_types.contains(res) ||
                    resource_const.contains(res)) {
                    return true;
                }
            }
            // check if events conflict
            // currently event read and event write can all modify the
            // queue, but in future maybe we will replace it by a
            // thread safe queue
            for (auto& event : event_read_types) {
                if (other.event_read_types.contains(event) ||
                    other.event_write_types.contains(event)) {
                    return true;
                }
            }
            for (auto& event : other.event_read_types) {
                if (event_read_types.contains(event) ||
                    event_write_types.contains(event)) {
                    return true;
                }
            }
            for (auto& event : event_write_types) {
                if (other.event_read_types.contains(event) ||
                    other.event_write_types.contains(event)) {
                    return true;
                }
            }
            for (auto& event : other.event_write_types) {
                if (event_read_types.contains(event) ||
                    event_write_types.contains(event)) {
                    return true;
                }
            }
            return false;
        }
    } system_param_src, system_param_dst;

    template <typename Arg>
    struct param_add {
        static void add(entt::dense_set<std::type_index>& infos) {
            infos.emplace(typeid(std::remove_const_t<Arg>));
        }
    };

    template <typename Arg>
    struct cparam_add {
        static void add(entt::dense_set<std::type_index>& infos) {
            if constexpr (std::is_const_v<Arg> || external_thread_safe_v<Arg>)
                infos.emplace(typeid(std::remove_const_t<Arg>));
        }
    };

    template <typename Arg>
    struct mparam_add {
        static void add(entt::dense_set<std::type_index>& infos) {
            if constexpr (!std::is_const_v<Arg> && !external_thread_safe_v<Arg>)
                infos.emplace(typeid(Arg));
        }
    };

    template <typename T>
    struct infos_adder {
        static void add(param_info& src_info, param_info& dst_info) {
            if constexpr (std::same_as<Command, std::remove_cvref_t<T>>) {
                dst_info.has_command = true;
            }
        }
    };

    template <typename... Args>
    void add_infos() {
        (infos_adder<Args>().add(system_param_src, system_param_dst), ...);
    }

    template <typename T>
    Local<T> get_local() {
        if (auto it = m_locals.find(std::type_index(typeid(T)));
            it == m_locals.end()) {
            m_locals.emplace(
                std::type_index(typeid(T)),
                std::static_pointer_cast<void>(std::make_shared<T>())
            );
        }
        return Local<T>(
            static_cast<T*>(m_locals[std::type_index(typeid(T))].get())
        );
    }

    template <typename T>
    struct LocalRetriever {};

    template <typename T>
    struct LocalRetriever<Local<T>> {
        static Local<T> get(BasicSystem* sys) { return sys->get_local<T>(); }
    };

    entt::dense_set<const BasicSystem*> m_contrary_to;
    entt::dense_set<const BasicSystem*> m_not_contrary_to;

   public:
    bool contrary_to(BasicSystem* other) {
        if (m_contrary_to.find(other) != m_contrary_to.end()) return true;
        if (m_not_contrary_to.find(other) != m_not_contrary_to.end())
            return false;
        if (system_param_src.conflict_with(other->system_param_src) ||
            system_param_dst.conflict_with(other->system_param_dst)) {
            m_contrary_to.emplace(other);
            return true;
        } else {
            m_not_contrary_to.emplace(other);
            return false;
        }
    }
    const double get_avg_time() const { return avg_time; }
    template <typename T>
    T retrieve(World* src, World* dst) {
        if constexpr (index::is_template_of<Local, T>::value) {
            return LocalRetriever<T>::get(this);
        } else {
            return World::param_type<T>::get(src, dst);
        }
    }
    template <typename... Args>
    BasicSystem(std::function<Ret(Args...)> func)
        : m_func([func](World* src, World* dst, BasicSystem* sys) {
              return func(sys->retrieve<Args>(src, dst)...);
          }),
          factor(0.1),
          avg_time(1) {
        add_infos<Args...>();
    }
    template <typename... Args>
    BasicSystem(Ret (*func)(Args...))
        : m_func([func](World* src, World* dst, BasicSystem* sys) {
              return func(sys->retrieve<Args>(src, dst)...);
          }),
          factor(0.1),
          avg_time(1) {
        add_infos<Args...>();
    }
    template <typename T>
        requires requires(T t) {
            { std::function(t) };
        }
    BasicSystem(T&& func) : BasicSystem(std::function(std::forward<T>(func))) {}
    BasicSystem(const BasicSystem& other)            = default;
    BasicSystem(BasicSystem&& other)                 = default;
    BasicSystem& operator=(const BasicSystem& other) = default;
    BasicSystem& operator=(BasicSystem&& other)      = default;
    Ret run(World* src, World* dst) {
        auto start = std::chrono::high_resolution_clock::now();
        if constexpr (std::is_same_v<Ret, void>) {
            m_func(src, dst, this);
            auto end = std::chrono::high_resolution_clock::now();
            auto delta =
                (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    end - start
                )
                    .count() /
                1000000.0;
            avg_time = delta * 0.1 + avg_time * 0.9;
        } else {
            auto&& ret = m_func(src, dst, this);
            auto end   = std::chrono::high_resolution_clock::now();
            auto delta =
                (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    end - start
                )
                    .count() /
                1000000.0;
            avg_time = delta * 0.1 + avg_time * 0.9;
            return ret;
        }
    }
};

template <typename Ret>
template <typename... Includes, typename... Withs, typename... Excludes>
struct BasicSystem<Ret>::infos_adder<
    Query<Get<Includes...>, With<Withs...>, Without<Excludes...>>> {
    static void add(param_info& src_info, param_info& dst_info) {
        dst_info.has_query = true;
        auto& query_types  = dst_info.query_types;
        entt::dense_set<std::type_index> query_include_types,
            query_exclude_types, query_include_const;
        (mparam_add<Includes>::add(query_include_types), ...);
        (cparam_add<Includes>::add(query_include_const), ...);
        (param_add<Withs>::add(query_include_const), ...);
        (param_add<Excludes>::add(query_exclude_types), ...);
        query_types.emplace_back(
            std::move(query_include_types), std::move(query_include_const),
            std::move(query_exclude_types)
        );
    }
};

template <typename Ret>
template <typename... Includes, typename... Excludes, typename T>
struct BasicSystem<Ret>::infos_adder<
    Query<Get<Includes...>, Without<Excludes...>, T>> {
    static void add(param_info& src_info, param_info& dst_info) {
        dst_info.has_query = true;
        auto& query_types  = dst_info.query_types;
        entt::dense_set<std::type_index> query_include_types,
            query_exclude_types, query_include_const;
        (mparam_add<Includes>::add(query_include_types), ...);
        (cparam_add<Includes>::add(query_include_const), ...);
        (param_add<Excludes>::add(query_exclude_types), ...);
        query_types.emplace_back(
            std::move(query_include_types), std::move(query_include_const),
            std::move(query_exclude_types)
        );
    }
};

template <typename Ret>
template <typename T>
struct BasicSystem<Ret>::infos_adder<Res<T>> {
    static void add(param_info& src_info, param_info& dst_info) {
        auto& resource_const = dst_info.resource_const;
        param_add<T>().add(resource_const);
    }
};

template <typename Ret>
template <typename T>
struct BasicSystem<Ret>::infos_adder<ResMut<T>> {
    static void add(param_info& src_info, param_info& dst_info) {
        auto& resource_types = dst_info.resource_types;
        param_add<T>().add(resource_types);
    }
};

template <typename Ret>
template <typename T>
struct BasicSystem<Ret>::infos_adder<EventReader<T>> {
    static void add(param_info& src_info, param_info& dst_info) {
        auto& event_read_types = dst_info.event_read_types;
        param_add<T>().add(event_read_types);
    }
};

template <typename Ret>
template <typename T>
struct BasicSystem<Ret>::infos_adder<EventWriter<T>> {
    static void add(param_info& src_info, param_info& dst_info) {
        auto& event_write_types = dst_info.event_write_types;
        param_add<T>().add(event_write_types);
    }
};

template <typename Ret>
template <typename T>
struct BasicSystem<Ret>::infos_adder<Extract<T>> {
    static void add(param_info& src_info, param_info& dst_info) {
        // reverse the order of src and dst for extracted parameters
        infos_adder<T>().add(dst_info, src_info);
    }
};

template <typename Ret>
template <typename T>
struct BasicSystem<Ret>::infos_adder<Local<T>> {
    static void add(param_info& src_info, param_info& dst_info) {}
};

struct SystemSet {
    std::type_index type;
    size_t value;

    template <typename T>
        requires std::is_enum_v<T>
    SystemSet(T value)
        : type(typeid(std::remove_cvref_t<T>)),
          value(static_cast<size_t>(value)) {}

    EPIX_API bool operator==(const SystemSet& other) const;
    EPIX_API bool operator!=(const SystemSet& other) const;
};

struct SystemAddInfo {
    struct each_t {
        std::string name;
        FuncIndex index;
        std::unique_ptr<BasicSystem<void>> system;
        std::vector<std::unique_ptr<BasicSystem<bool>>> conditions;

        std::vector<SystemSet> m_in_sets;
        std::string m_worker = "default";
        entt::dense_set<FuncIndex> m_ptr_prevs;
        entt::dense_set<FuncIndex> m_ptr_nexts;

        EPIX_API each_t(
            const std::string& name,
            FuncIndex index,
            std::unique_ptr<BasicSystem<void>>&& system
        );
        EPIX_API each_t(each_t&& other);
        EPIX_API each_t& operator=(each_t&& other);
    };
    std::vector<each_t> m_systems;

    SystemAddInfo()                                      = default;
    SystemAddInfo(const SystemAddInfo& other)            = delete;
    SystemAddInfo(SystemAddInfo&& other)                 = default;
    SystemAddInfo& operator=(const SystemAddInfo& other) = delete;
    SystemAddInfo& operator=(SystemAddInfo&& other)      = default;

    EPIX_API SystemAddInfo& chain();

    template <typename T, typename... Args>
    SystemAddInfo& before(T (*func)(Args...)) {
        for (auto& each : m_systems) {
            each.m_ptr_nexts.emplace(FuncIndex(func));
        }
        return *this;
    }
    template <typename T, typename... Args>
    SystemAddInfo& after(T (*func)(Args...)) {
        for (auto& each : m_systems) {
            each.m_ptr_prevs.emplace(FuncIndex(func));
        }
        return *this;
    }
    template <typename T>
        requires std::is_enum_v<T>
    SystemAddInfo& in_set(T t) {
        for (auto& each : m_systems) {
            each.m_in_sets.emplace_back(SystemSet(t));
        }
        return *this;
    }
    EPIX_API SystemAddInfo& worker(const std::string& worker);
    template <typename... Args>
    SystemAddInfo& run_if(const std::function<bool(Args...)>& func) {
        for (auto& each : m_systems) {
            each.conditions.emplace_back(
                std::make_unique<BasicSystem<bool>>(func)
            );
        }
        return *this;
    }
    template <typename... Args>
    SystemAddInfo& run_if(std::function<bool(Args...)>&& func) {
        for (auto& each : m_systems) {
            each.conditions.emplace_back(
                std::make_unique<BasicSystem<bool>>(func)
            );
        }
        return *this;
    }
    template <typename... Args>
    SystemAddInfo& run_if(bool (*func)(Args...)) {
        for (auto& each : m_systems) {
            each.conditions.emplace_back(
                std::make_unique<BasicSystem<bool>>(func)
            );
        }
        return *this;
    }
    template <typename T>
        requires requires(T t) {
            { std::function(t) };
        }
    SystemAddInfo& run_if(T&& func) {
        run_if(std::function(func));
        return *this;
    }
    template <typename T>
    SystemAddInfo& in_state(T state) {
        run_if([state](Res<State<T>> cur) { return cur->is_state(state); });
        return *this;
    };
    EPIX_API SystemAddInfo& set_label(const std::string& label);
    EPIX_API SystemAddInfo& set_label(uint32_t index, const std::string& label);
    EPIX_API SystemAddInfo& set_labels(index::ArrayProxy<std::string> labels);
    EPIX_API SystemAddInfo& set_labels(index::ArrayProxy<const char*> labels);
};

struct System {
    EPIX_API System(
        const std::string& label,
        FuncIndex index,
        std::unique_ptr<BasicSystem<void>>&& system
    );
    System(const System& other)            = delete;
    System(System&& other)                 = delete;
    System& operator=(const System& other) = delete;
    System& operator=(System&& other)      = delete;

    EPIX_API bool run(World* src, World* dst, bool enable_tracy);
    EPIX_API void clear_tmp();
    EPIX_API double reach_time();

    std::string label;
    FuncIndex index;
    std::unique_ptr<BasicSystem<void>> system;
    std::string worker;
    std::vector<SystemSet> sets;
    std::vector<std::unique_ptr<BasicSystem<bool>>> conditions;

    entt::dense_set<std::weak_ptr<System>> m_prevs;
    entt::dense_set<std::weak_ptr<System>> m_nexts;
    entt::dense_set<std::weak_ptr<System>> m_tmp_prevs;
    entt::dense_set<std::weak_ptr<System>> m_tmp_nexts;
    entt::dense_set<FuncIndex> m_ptr_prevs;
    entt::dense_set<FuncIndex> m_ptr_nexts;

    std::optional<double> m_reach_time;
    size_t m_prev_count;
    size_t m_next_count;
};
using SetMap = entt::dense_map<std::type_index, std::vector<SystemSet>>;
struct ScheduleId {
    std::type_index type;
    size_t value;
    template <typename T>
    ScheduleId(T value) : type(typeid(std::remove_cvref_t<T>)) {
        if constexpr (std::is_enum_v<std::remove_cvref_t<T>>) {
            this->value = static_cast<size_t>(value);
        } else {
            this->value = 0;
        }
    }
    ScheduleId(const ScheduleId& other)            = default;
    ScheduleId(ScheduleId&& other)                 = default;
    ScheduleId& operator=(const ScheduleId& other) = default;
    ScheduleId& operator=(ScheduleId&& other)      = default;

    EPIX_API bool operator==(const ScheduleId& other) const;
};
struct Executor {
   private:
    entt::dense_map<
        std::string,
        std::shared_ptr<BS::thread_pool<BS::tp::priority>>>
        m_pools;

   public:
    EPIX_API void add(const std::string& name, size_t count);
    EPIX_API std::shared_ptr<BS::thread_pool<BS::tp::priority>> get(
        const std::string& name
    );
};
enum ScheduleExecutionType {
    SingleThread,
    MultiThread,
};
struct Schedule {
   private:
    SetMap m_sets;
    std::shared_ptr<spdlog::logger> m_logger;
    std::shared_ptr<Executor> m_executor;
    ScheduleId m_id;

    std::type_index m_src_world;
    std::type_index m_dst_world;

    entt::dense_set<std::weak_ptr<Schedule>> m_prev_schedules;
    entt::dense_set<std::weak_ptr<Schedule>> m_next_schedules;
    entt::dense_set<std::weak_ptr<Schedule>> m_tmp_prevs;
    entt::dense_set<std::weak_ptr<Schedule>> m_tmp_nexts;

    entt::dense_map<FuncIndex, std::shared_ptr<System>> m_systems;
    std::shared_ptr<index::concurrent::conqueue<std::shared_ptr<System>>>
        m_finishes;

    entt::dense_set<ScheduleId> m_prev_ids;
    entt::dense_set<ScheduleId> m_next_ids;

    double m_avg_time = 1.0;
    std::optional<double> m_reach_time;

    size_t m_prev_count = 0;

    EPIX_API Schedule& set_logger(const std::shared_ptr<spdlog::logger>& logger
    );

   public:
    EPIX_API Schedule(ScheduleId id);
    Schedule(Schedule&& other)            = default;
    Schedule& operator=(Schedule&& other) = default;

    EPIX_API Schedule& set_executor(const std::shared_ptr<Executor>& executor);
    template <typename T, typename... Args>
    Schedule& after(T scheduleId, Args... others) {
        m_prev_ids.emplace(scheduleId);
        (m_prev_ids.emplace(others), ...);
        return *this;
    }
    template <typename T, typename... Args>
    Schedule& before(T scheduleId, Args... others) {
        m_next_ids.emplace(scheduleId);
        (m_next_ids.emplace(others), ...);
        return *this;
    }
    template <typename T, typename... Args>
    Schedule& configure_sets(T set, Args... others) {
        m_sets.emplace(
            std::type_index(typeid(std::remove_cvref_t<T>)),
            std::vector<SystemSet>{
                SystemSet(std::forward<T>(set)),
                SystemSet(std::forward<Args>(others))...
            }
        );
        return *this;
    }
    template <typename T>
    Schedule& set_src_world() {
        m_src_world = typeid(std::remove_cvref_t<T>);
        return *this;
    }
    template <typename T>
    Schedule& set_dst_world() {
        m_dst_world = typeid(std::remove_cvref_t<T>);
        return *this;
    }
    EPIX_API Schedule& add_system(SystemAddInfo&& info);
    EPIX_API void build();
    EPIX_API void bake();
    EPIX_API void run(World* src, World* dst, bool enable_tracy);
    EPIX_API void run(
        std::shared_ptr<System> system,
        World* src,
        World* dst,
        bool enable_tracy
    );
    EPIX_API double get_avg_time() const;
    EPIX_API void clear_tmp();
    EPIX_API double reach_time();

    friend struct App;
};
struct ScheduleInfo : public ScheduleId {
    std::vector<std::function<void(SystemAddInfo&)>> transforms;
};

inline struct PreStartupT {
} PreStartup;
inline struct StartupT {
} Startup;
inline struct PostStartupT {
} PostStartup;
inline struct FirstT {
} First;
inline struct PreUpdateT {
} PreUpdate;
inline struct UpdateT {
} Update;
inline struct PostUpdateT {
} PostUpdate;
inline struct LastT {
} Last;
inline struct PrepareT {
} Prepare;
inline struct PreRenderT {
} PreRender;
inline struct RenderT {
} Render;
inline struct PostRenderT {
} PostRender;
inline struct PreExitT {
} PreExit;
inline struct ExitT {
} Exit;
inline struct PostExitT {
} PostExit;
inline struct StateTransitionT {
} StateTransition;
enum StateTransitionSet {
    Transit,
    Callback,
};
inline struct PreExtractT {
} PreExtract;
inline struct ExtractionT {
} Extraction;
inline struct PostExtractT {
} PostExtract;
inline struct OnEnterT {
    template <typename T>
    ScheduleInfo operator()(T&& state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SystemAddInfo& info) {
            info.run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
                return (cur->is_state(state) && cur->is_just_created()) ||
                       (!cur->is_state(state) && next->is_state(state));
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnEnter;
inline struct OnExitT {
    template <typename T>
    ScheduleInfo operator()(T&& state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SystemAddInfo& info) {
            info.run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
                return cur->is_state(state) && !next->is_state(state);
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnExit;
inline struct OnChangeT {
    template <typename T>
    ScheduleInfo operator()(T&& state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SystemAddInfo& info) {
            info.run_if([](Res<State<T>> cur, Res<NextState<T>> next) {
                return !cur->is_state(next->m_state);
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnChange;

struct MainWorld {};
struct RenderWorld {};

struct ExitControl {};

struct AppExit {};

template <typename T>
    requires std::same_as<SystemAddInfo, std::remove_cvref_t<T>>
void info_append(SystemAddInfo& info, T&& func) {
    SystemAddInfo&& info2 = std::move(func);
    std::move(
        info2.m_systems.begin(), info2.m_systems.end(),
        std::back_inserter(info.m_systems)
    );
}

template <typename Func>
    requires requires(Func func) {
        { std::function(func) };
    }
void info_append(SystemAddInfo& info, Func&& func) {
    if constexpr (std::is_function_v<
                      std::remove_pointer_t<std::remove_cvref_t<Func>>>) {
        info.m_systems.emplace_back(
            std::format("system:{:#016x}", (size_t)func), FuncIndex(func),
            std::make_unique<BasicSystem<void>>(func)
        );
    } else {
        auto ptr = std::make_unique<BasicSystem<void>>(func);
        FuncIndex index;
        index.func = ptr.get();
        info.m_systems.emplace_back(
            std::format("system:{:#016x}", (size_t)ptr.get()), index,
            std::move(ptr)
        );
    }
}

template <typename... Funcs>
SystemAddInfo into(Funcs&&... funcs) {
    SystemAddInfo info;
    info.m_systems.reserve(sizeof...(funcs));
    (info_append(info, std::forward<Funcs>(funcs)), ...);
    return std::move(info);
}

template <>
EPIX_API SystemAddInfo into(SystemAddInfo&& info);

struct Plugin {
    virtual void build(App& app) = 0;
};

struct AppCreateInfo {
    uint32_t control_threads                                     = 2;
    std::vector<std::pair<std::string, uint32_t>> worker_threads = {
        {"default", 4},
        {"single", 1},
    };
    bool enable_loop                       = false;
    bool enable_tracy                      = true;
    std::shared_ptr<spdlog::logger> logger = nullptr;
};

inline struct StartGraphT {
} StartGraph;
inline struct LoopGraphT {
} LoopGraph;
inline struct ExitGraphT {
} ExitGraph;
struct GraphId : public std::type_index {
    template <typename T>
    GraphId(T value) : std::type_index(typeid(std::remove_cvref_t<T>)) {}
    GraphId(const GraphId& other)            = default;
    GraphId(GraphId&& other)                 = default;
    GraphId& operator=(const GraphId& other) = default;
    GraphId& operator=(GraphId&& other)      = default;
};

struct AppProfile {
    /// in milliseconds
    double frame_time = 0.0;
    /// frames per second
    double fps = 0.0;
};

struct App {
    struct ScheduleGraph {
        entt::dense_map<ScheduleId, std::shared_ptr<Schedule>> m_schedules;
        index::concurrent::conqueue<std::shared_ptr<Schedule>> m_finishes;
    };

   private:
    std::vector<std::pair<std::type_index, std::shared_ptr<Plugin>>> m_plugins;
    entt::dense_set<std::type_index> m_built_plugins;

    entt::dense_map<std::type_index, std::unique_ptr<World>> m_worlds;

    entt::dense_map<std::type_index, std::unique_ptr<ScheduleGraph>> m_graphs;
    entt::dense_map<ScheduleId, std::type_index> m_graph_ids;

    std::unique_ptr<BS::thread_pool<BS::tp::priority>> m_pool;
    std::shared_ptr<Executor> m_executor;

    std::unique_ptr<bool> m_enable_loop;
    std::unique_ptr<bool> m_enable_tracy;

    std::shared_ptr<spdlog::logger> m_logger;

    EPIX_API App(const AppCreateInfo& info);

   public:
    EPIX_API static App create(const AppCreateInfo& info = {});
    EPIX_API static App create2(const AppCreateInfo& info = {});

    EPIX_API App* operator->();
    EPIX_API App& enable_loop();
    EPIX_API App& disable_loop();
    EPIX_API App& enable_tracy();
    EPIX_API App& disable_tracy();
    EPIX_API App& set_log_level(spdlog::level::level_enum level);

    EPIX_API App& add_schedule(GraphId, Schedule&& schedule);
    EPIX_API App& add_schedule(GraphId, Schedule& schedule);

    template <typename... Schs>
        requires(sizeof...(Schs) != 1)
    App& add_schedule(GraphId id, Schs&&... schs) {
        (add_schedule(id, std::forward<Schs>(schs)), ...);
        return *this;
    }

    EPIX_API App& set_logger(const std::shared_ptr<spdlog::logger>& logger);
    EPIX_API std::shared_ptr<spdlog::logger> logger() const;

    template <typename T>
    App& add_world() {
        auto id = std::type_index(typeid(std::remove_cvref_t<T>));
        if (!m_worlds.contains(id)) {
            m_worlds.emplace(id, std::make_unique<World>());
        }
        return *this;
    };
    template <typename T>
    World& world() {
        auto id = std::type_index(typeid(std::remove_cvref_t<T>));
        if (auto it = m_worlds.find(id); it != m_worlds.end()) {
            return *it->second;
        }
        throw std::runtime_error("World not found.");
    };
    template <typename T>
    World* get_world() {
        auto id = std::type_index(typeid(std::remove_cvref_t<T>));
        if (auto it = m_worlds.find(id); it != m_worlds.end()) {
            return it->second.get();
        }
        return nullptr;
    };

    template <typename... Args>
    App& configure_sets(Args... sets) {
        for (auto&& [id, graph] : m_graphs) {
            for (auto&& [type, schedules] : graph->m_schedules) {
                schedules->configure_sets(sets...);
            }
        }
        return *this;
    }

    template <typename... Funcs>
    App& add_system(ScheduleId id, Funcs&&... funcs) {
        if (!m_graph_ids.contains(id)) {
            return *this;
        }
        auto&& graph    = m_graphs.at(m_graph_ids.at(id));
        auto&& schedule = graph->m_schedules[id];
        (schedule->add_system(std::move(into(std::forward<Funcs>(funcs)))),
         ...);
        return *this;
    };
    template <typename T>
    App& add_plugin(T&& plugin) {
        auto id = std::type_index(typeid(std::remove_cvref_t<T>));
        if (std::find_if(
                m_plugins.begin(), m_plugins.end(),
                [id](const auto& pair) { return pair.first == id; }
            ) == m_plugins.end()) {
            m_plugins.emplace_back(
                id,
                std::make_shared<std::remove_cvref_t<T>>(std::forward<T>(plugin)
                )
            );
        }
        return *this;
    };

    template <typename T>
    T& plugin() {
        auto id = std::type_index(typeid(std::remove_cvref_t<T>));
        if (auto it = std::find_if(
                m_plugins.begin(), m_plugins.end(),
                [id](const auto& pair) { return pair.first == id; }
            );
            it != m_plugins.end()) {
            return *std::static_pointer_cast<T>(it->second).get();
        }
        throw std::runtime_error("Plugin not found.");
    };
    template <typename T>
    std::shared_ptr<T> get_plugin() {
        auto id = std::type_index(typeid(std::remove_cvref_t<T>));
        if (auto it = std::find_if(
                m_plugins.begin(), m_plugins.end(),
                [id](const auto& pair) { return pair.first == id; }
            );
            it != m_plugins.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    };

    template <typename T, typename... Args>
    App& emplace_resource(Args&&... args) {
        auto&& w = world<MainWorld>();
        w.emplace_resource<T>(std::forward<Args>(args)...);
        return *this;
    };
    template <typename T>
    App& insert_resource(T&& resource) {
        auto&& w = world<MainWorld>();
        w.insert_resource(std::forward<T>(resource));
        return *this;
    };
    template <typename T>
    App& init_resource() {
        auto&& w = world<MainWorld>();
        w.init_resource<T>();
        return *this;
    };
    template <typename T>
    App& add_resource(T* resource) {
        auto&& w = world<MainWorld>();
        w.add_resource<T>(resource);
        return *this;
    };
    template <typename T>
    App& remove_resource() {
        auto&& w = world<MainWorld>();
        w.remove_resource<T>();
        return *this;
    };
    template <typename T>
    App& insert_state(T&& state) {
        auto&& w = world<MainWorld>();
        w.insert_resource<State<T>>(State<T>(std::forward<T>(state)));
        SystemAddInfo info;
        info.m_systems.emplace_back(
            std::format("update State<{}>", typeid(T).name()), FuncIndex(),
            std::make_unique<BasicSystem<void>>([](ResMut<State<T>> state,
                                                   Res<NextState<T>> next) {
                state->m_state = next->m_state;
            })
        );
        add_system(
            StateTransition, std::move(info.in_set(StateTransitionSet::Transit))
        );
        return *this;
    };
    template <typename T>
    App& init_state() {
        auto&& w = world<MainWorld>();
        w.init_resource<State<T>>();
        SystemAddInfo info;
        info.m_systems.emplace_back(
            std::format("update State<{}>", typeid(T).name()), FuncIndex(),
            std::make_unique<BasicSystem<void>>([](ResMut<State<T>> state,
                                                   Res<NextState<T>> next) {
                state->m_state = next->m_state;
            })
        );
        add_system(
            StateTransition, std::move(info.in_set(StateTransitionSet::Transit))
        );
        return *this;
    };
    template <typename T>
    App& add_event() {
        auto&& w = world<MainWorld>();
        w.add_event<T>();
        SystemAddInfo info;
        auto func =
            std::make_unique<BasicSystem<void>>([](EventWriter<T> event) {
                event.m_queue->tick();
            });
        FuncIndex index;
        index.func = func.get();
        info.m_systems.emplace_back(
            std::format("update Event<{}>", typeid(T).name()), index,
            std::move(func)
        );
        add_system(Last, std::move(info));
        return *this;
    };

    EPIX_API void build_plugins();
    EPIX_API void build(ScheduleGraph&);
    EPIX_API void bake(ScheduleGraph&);
    EPIX_API void run(ScheduleGraph&);
    EPIX_API void run();
};
}  // namespace epix::app

namespace epix::utility {
template <typename Resolution = std::chrono::milliseconds>
    requires std::same_as<Resolution, std::chrono::nanoseconds> ||
             std::same_as<Resolution, std::chrono::microseconds> ||
             std::same_as<Resolution, std::chrono::milliseconds> ||
             std::same_as<Resolution, std::chrono::seconds> ||
             std::same_as<Resolution, std::chrono::minutes> ||
             std::same_as<Resolution, std::chrono::hours>
struct time_scope {
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    std::string name;
    std::function<void(int64_t)> dur_op;
    std::function<void(int64_t, int64_t)> dur_op2;
    std::function<void(int64_t, int64_t, int64_t)> dur_op3;
    time_scope(const std::string& name) : name(name) {
        start = std::chrono::high_resolution_clock::now();
    }
    time_scope(const std::string& name, std::function<void(int64_t)> operation)
        : name(name), dur_op(operation) {
        start = std::chrono::high_resolution_clock::now();
    }
    time_scope(
        const std::string& name, std::function<void(int64_t, int64_t)> operation
    )
        : name(name), dur_op2(operation) {
        start = std::chrono::high_resolution_clock::now();
    }
    time_scope(
        const std::string& name,
        std::function<void(int64_t, int64_t, int64_t)> operation
    )
        : name(name), dur_op3(operation) {
        start = std::chrono::high_resolution_clock::now();
    }
    ~time_scope() {
        auto end        = std::chrono::high_resolution_clock::now();
        auto dur        = std::chrono::duration_cast<Resolution>(end - start);
        auto start_time = std::chrono::time_point_cast<Resolution>(start);
        auto end_time   = std::chrono::time_point_cast<Resolution>(end);

        if (dur_op) dur_op(dur.count());
        if (dur_op2)
            dur_op2(
                start_time.time_since_epoch().count(),
                end_time.time_since_epoch().count()
            );
        if (dur_op3)
            dur_op3(
                start_time.time_since_epoch().count(),
                end_time.time_since_epoch().count(), dur.count()
            );
    }
};
}  // namespace epix::utility

// impl
namespace epix::app {
template <typename T, typename... Args>
void WorldEntityCommand::emplace(Args&&... args) {
    m_world->entity_emplace<T>(m_entity, std::forward<Args>(args)...);
}
template <typename T>
void WorldEntityCommand::emplace(T&& obj) {
    m_world->entity_emplace<T>(m_entity, std::forward<T>(obj));
}
template <typename... Args>
void WorldEntityCommand::erase() {
    static void (*erase)(World*, Entity) = [](World* world, Entity entity) {
        world->m_registry.erase<Args...>(entity);
    };
    m_command->m_entity_erase.emplace(erase, m_entity);
}
template <typename... Args>
Entity WorldEntityCommand::spawn(Args&&... args) {
    auto entity =
        m_command->spawn(Parent{m_entity}, std::forward<Args>(args)...);
    m_world->m_registry.get_or_emplace<Children>(m_entity).children.emplace(
        entity
    );
    return entity;
}

template <typename... Args>
Entity WorldCommand::spawn(Args&&... args) {
    return m_world->spawn(std::forward<Args>(args)...);
}

template <typename T>
void WorldCommand::insert_resource(T&& res) {
    m_world->insert_resource(std::forward<T>(res));
}
template <typename T>
void WorldCommand::init_resource() {
    m_world->init_resource<T>();
}
template <typename T, typename... Args>
void WorldCommand::emplace_resource(Args&&... args) {
    m_world->emplace_resource<T>(std::forward<Args>(args)...);
}
template <typename T>
void WorldCommand::add_resource(const std::shared_ptr<T>& res) {
    m_world->add_resource(res);
}
template <typename T>
void WorldCommand::add_resource(T* res) {
    m_world->add_resource(res);
}
template <typename T>
void WorldCommand::add_resource(
    const std::shared_ptr<T>& res,
    const std::shared_ptr<std::shared_mutex>& mutex
) {
    m_world->add_resource(res, mutex);
}
template <typename T>
void WorldCommand::remove_resource() {
    m_remove_resources.emplace(std::type_index(typeid(T)));
}

template <typename... Args>
Entity Command::spawn(Args&&... args) {
    return dst_cmd->spawn(std::forward<Args>(args)...);
}
template <typename T, typename... Args>
void Command::emplace_resource(Args&&... args) {
    dst_cmd->emplace_resource<T>(std::forward<Args>(args)...);
}
template <typename T>
void Command::insert_resource(T&& res) {
    dst_cmd->insert_resource(std::forward<T>(res));
}
template <typename T>
void Command::init_resource() {
    dst_cmd->init_resource<T>();
}
template <typename T>
void Command::add_resource(const std::shared_ptr<T>& res) {
    dst_cmd->add_resource(res);
}
template <typename T>
void Command::add_resource(T* res) {
    dst_cmd->add_resource(res);
}
template <typename T>
void Command::share_resource() {
    auto* world_src  = src_cmd->m_world;
    auto untyped_res = world_src->resource<T>();
    if (untyped_res.resource) {
        auto res   = untyped_res.resource;
        auto mutex = untyped_res.mutex;
        dst_cmd->add_resource<T>(std::static_pointer_cast<T>(res), mutex);
    }
}
template <typename T>
void Command::share_resource(Res<T>&) {
    auto* world_src  = src_cmd->m_world;
    auto untyped_res = world_src->resource<T>();
    if (untyped_res.resource) {
        auto res   = untyped_res.resource;
        auto mutex = untyped_res.mutex;
        dst_cmd->add_resource<T>(std::static_pointer_cast<T>(res), mutex);
    }
}
template <typename T>
void Command::share_resource(ResMut<T>&) {
    auto* world_src  = src_cmd->m_world;
    auto untyped_res = world_src->resource<T>();
    if (untyped_res.resource) {
        auto res   = untyped_res.resource;
        auto mutex = untyped_res.mutex;
        dst_cmd->add_resource<T>(std::static_pointer_cast<T>(res), mutex);
    }
}
template <typename T>
void Command::remove_resource() {
    dst_cmd->remove_resource<T>();
}
}  // namespace epix::app

namespace epix {

// ENTITY PART
using app::App;
using app::AppCreateInfo;
using app::Children;
using app::Entity;
using app::Parent;
using app::Plugin;

// PROFILE
using app::AppProfile;

// EVENTS
using app::AppExit;

// SCHEDULES
using app::Schedule;
using app::ScheduleId;
using app::ScheduleInfo;

using app::Exit;
using app::Extraction;
using app::First;
using app::Last;
using app::OnChange;
using app::OnEnter;
using app::OnExit;
using app::PostExit;
using app::PostExtract;
using app::PostRender;
using app::PostStartup;
using app::PostUpdate;
using app::PreExit;
using app::PreExtract;
using app::Prepare;
using app::PreRender;
using app::PreStartup;
using app::PreUpdate;
using app::Render;
using app::Startup;
using app::Update;

// SYSTEM PARA PART
using app::Command;
using app::EventReader;
using app::EventWriter;
using app::Extract;
using app::Get;
using app::Local;
using app::NextState;
using app::Query;
using app::Res;
using app::ResMut;
using app::State;
using app::With;
using app::Without;

// OTHER TOOLS
using entt::dense_map;
using entt::dense_set;
using epix::app::into;
using epix::app::thread_pool;
using epix::utility::time_scope;

}  // namespace epix
namespace epix::prelude {
using namespace epix;
}

#define EPIX_INTO(function) epix::app::into(##function).set_label(#function)

#ifndef into2
#define into2(x) EPIX_INTO(##x)
#endif