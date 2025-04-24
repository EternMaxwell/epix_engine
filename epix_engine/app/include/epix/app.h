#pragma once

// ----THIRD PARTY INCLUDES----
#include <epix/utils/core.h>
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
    t_weak_ptr(std::shared_ptr<T> ptr) noexcept : std::weak_ptr<T>(ptr) {}
    t_weak_ptr(std::weak_ptr<T> ptr) noexcept : std::weak_ptr<T>(ptr) {}

    T* get_p() noexcept { return *reinterpret_cast<T**>(this); }
};
struct Label {
   protected:
    std::type_index type;
    size_t index;

    template <typename U>
    Label(U u) noexcept : type(typeid(U)), index(0) {
        if constexpr (std::is_enum_v<U>) {
            index = static_cast<size_t>(u);
        }
    }
    EPIX_API Label(std::type_index t, size_t i) noexcept;
    EPIX_API Label() noexcept;

   public:
    Label(const Label&)            = default;
    Label(Label&&)                 = default;
    Label& operator=(const Label&) = default;
    Label& operator=(Label&&)      = default;
    EPIX_API bool operator==(const Label& other) const noexcept;
    EPIX_API bool operator!=(const Label& other) const noexcept;
    EPIX_API void set_type(std::type_index t) noexcept;
    EPIX_API void set_index(size_t i) noexcept;
    EPIX_API size_t hash_code() const noexcept;
    EPIX_API std::string name() const noexcept;
};
template <typename T>
struct Hasher {
    size_t operator()(const T& t) const { return std::hash<T>()(t); }
};
template <typename T>
    requires requires(T t) {
        { t.hash_code() } -> std::convertible_to<size_t>;
    }
struct Hasher<T> {
    size_t operator()(const T& t) const { return t.hash_code(); }
};
}  // namespace epix::app_tools

template <typename T>
struct std::hash<std::weak_ptr<T>> {
    size_t operator()(const std::weak_ptr<T>& ptr) const {
        epix::app_tools::t_weak_ptr<T> tptr(ptr);
        return std::hash<T*>()(tptr.get_p());
    }
};
template <std::derived_from<epix::app_tools::Label> T>
struct std::hash<T> {
    size_t operator()(const T& label) const
        noexcept(noexcept(label.hash_code())) {
        return label.hash_code();
    }
};

template <typename T>
struct std::equal_to<std::weak_ptr<T>> {
    bool operator()(const std::weak_ptr<T>& a, const std::weak_ptr<T>& b)
        const noexcept {
        epix::app_tools::t_weak_ptr<T> aptr(a);
        epix::app_tools::t_weak_ptr<T> bptr(b);
        return aptr.get_p() == bptr.get_p();
    }
};

namespace epix::app {
using app_tools::Label;
using entt::dense_map;
using entt::dense_set;

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

struct Command;
struct EntityCommand;

struct Schedule;
struct ScheduleId;
struct ScheduleInfo;

struct App;

using thread_pool = BS::thread_pool<BS::tp::priority>;
}  // namespace epix::app

template <>
struct std::hash<epix::app::Entity> {
    EPIX_API size_t operator()(const epix::app::Entity& entity) const;
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
    EPIX_API size_t index() const;
    EPIX_API size_t hash_code() const;
};
struct FuncIndex : Label {
    template <typename T, typename... Args>
    FuncIndex(T (*func)(Args...)) : Label(typeid(func), (size_t)func) {}
    FuncIndex() : Label() {}
    using Label::operator==;
    using Label::operator!=;
};

struct Children {
    dense_set<Entity> children;
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
    const T& operator*() const { return *t; }
    const T* operator->() const { return t; }

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

    void lock() {
        if (m_mutex) m_mutex->lock_shared();
    }
    void unlock() {
        if (m_mutex) m_mutex->unlock_shared();
    }

    template <typename U>
    friend struct PrepareParam;

   public:
    Res(const std::shared_ptr<void>& resource,
        const std::shared_ptr<std::shared_mutex>& mutex)
        : m_res(std::static_pointer_cast<T>(resource)), m_mutex(mutex) {}
    Res() : m_res(nullptr), m_mutex(nullptr) {}
    Res(const Res& other) {
        m_res   = other.m_res;
        m_mutex = other.m_mutex;
    }
    Res(Res&& other) {
        m_res   = other.m_res;
        m_mutex = other.m_mutex;
    }
    Res& operator=(const Res& other) {
        m_res   = other.m_res;
        m_mutex = other.m_mutex;
        return *this;
    }
    Res& operator=(Res&& other) {
        m_res   = other.m_res;
        m_mutex = other.m_mutex;
        return *this;
    }

    /**
     * @brief Check if the resource has a value.
     */
    bool has_value() const { return m_res != nullptr; }

    operator bool() const { return has_value(); }
    bool operator!() const { return !has_value(); }

    // const T& operator*() { return *m_res; }
    // const T* operator->() { return m_res.get(); }
    const T& operator*() const { return *m_res; }
    const T* operator->() const { return m_res.get(); }

    // const T* get() { return m_res.get(); }
    const T* get() const { return m_res.get(); }
};
template <typename T>
struct ResMut {
   private:
    std::shared_ptr<T> m_res;
    std::shared_ptr<std::shared_mutex> m_mutex;

    void lock() {
        if (m_mutex) {
            if constexpr (std::is_const_v<T> || external_thread_safe_v<T>) {
                m_mutex->lock_shared();
            } else {
                m_mutex->lock();
            }
        }
    }
    void unlock() {
        if (m_mutex) {
            if constexpr (std::is_const_v<T> || external_thread_safe_v<T>) {
                m_mutex->unlock_shared();
            } else {
                m_mutex->unlock();
            }
        }
    }

    template <typename U>
    friend struct PrepareParam;

   public:
    ResMut(
        const std::shared_ptr<void>& resource,
        const std::shared_ptr<std::shared_mutex>& mutex
    )
        : m_res(std::static_pointer_cast<T>(resource)), m_mutex(mutex) {}
    ResMut() : m_res(nullptr), m_mutex(nullptr) {}
    ResMut(const ResMut& other) {
        m_res   = other.m_res;
        m_mutex = other.m_mutex;
    }
    ResMut(ResMut&& other) {
        m_res   = other.m_res;
        m_mutex = other.m_mutex;
    }
    ResMut& operator=(const ResMut& other) {
        m_res   = other.m_res;
        m_mutex = other.m_mutex;
        return *this;
    }
    ResMut& operator=(ResMut&& other) {
        m_res   = other.m_res;
        m_mutex = other.m_mutex;
        return *this;
    }

    /**
     * @brief Check if the resource has a value.
     */
    bool has_value() const { return m_res != nullptr; }

    operator bool() const { return has_value(); }
    bool operator!() const { return !has_value(); }

    operator Res<T>() const { return Res<T>(m_res, m_mutex); }

    T& operator*() { return *m_res; }
    const T& operator*() const { return *m_res; }
    T* operator->() { return m_res.get(); }
    const T* operator->() const { return m_res.get(); }

    T* get() { return m_res.get(); }
    const T* get() const { return m_res.get(); }
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
template <typename T>
struct Events {
   private:
    std::deque<std::pair<T, uint32_t>> m_events;  // event lifetime pair
    uint32_t m_head;
    uint32_t m_tail;  // m_tail - m_head should be equal to m_events.size()

   public:
    Events() : m_head(0), m_tail(0) {}
    Events(const Events&) = delete;
    Events(Events&& other) {
        m_events = std::move(other.m_events);
        m_head   = other.m_head;
        m_tail   = other.m_tail;
    }
    Events& operator=(const Events&) = delete;
    Events& operator=(Events&& other) {
        m_events = std::move(other.m_events);
        m_head   = other.m_head;
        m_tail   = other.m_tail;
        return *this;
    }

    void push(const T& event) {
        m_events.emplace_back(event, 1);
        m_tail++;
    }
    void push(T&& event) {
        m_events.emplace_back(std::move(event), 1);
        m_tail++;
    }
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events.emplace_back(T(std::forward<Args>(args)...), 1);
        m_tail++;
    }
    void update() {
        while (!m_events.empty() && m_events.front().second == 0) {
            m_events.pop_front();
            m_head++;
        }
        for (auto& event : m_events) {
            if (event.second > 0) {
                event.second--;
            }
        }
    }
    void clear() {
        m_events.clear();
        m_head = m_tail;
    }
    bool empty() const { return m_events.empty(); }
    size_t size() const { return m_events.size(); }
    uint32_t head() const { return m_head; }
    uint32_t tail() const { return m_tail; }
    T* get(uint32_t index) {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head].first;
        }
        return nullptr;
    }
    const T* get(uint32_t index) const {
        if (index >= m_head && index < m_tail) {
            return &m_events[index - m_head].first;
        }
        return nullptr;
    }
};
template <typename T>
struct EventPointer {
    uint32_t index = 0;
};
template <typename T>
struct EventReader {
    struct iterator {
        uint32_t index;
        const Events<T>* events;
        iterator(uint32_t index, const Events<T>* events)
            : index(index), events(events) {}
        iterator& operator++() {
            index++;
            return *this;
        }
        bool operator==(const iterator& rhs) const {
            return index == rhs.index;
        }
        bool operator!=(const iterator& rhs) const {
            return index != rhs.index;
        }
        const T& operator*() {
            auto event = events->get(index);
            return *event;
        }
    };
    struct iterator_index {
        uint32_t index;
        const Events<T>* events;
        iterator_index(uint32_t index, const Events<T>* events)
            : index(index), events(events) {}
        iterator_index& operator++() {
            index++;
            return *this;
        }
        bool operator==(const iterator_index& rhs) const {
            return index == rhs.index;
        }
        bool operator!=(const iterator_index& rhs) const {
            return index != rhs.index;
        }
        std::pair<uint32_t, const T&> operator*() {
            auto event = events->get(index);
            return {index, *event};
        }
    };
    struct iterable {
       private:
        const uint32_t m_read_begin;
        const Events<T>* m_events;
        const iterator m_begin;
        const iterator m_end;

       public:
        iterable(uint32_t read_begin, const Events<T>* events)
            : m_read_begin(read_begin),
              m_events(events),
              m_begin(read_begin, events),
              m_end(events->tail(), events) {}
        iterator begin() { return m_begin; }
        iterator end() { return m_end; }
    };
    struct iterable_index {
       private:
        const uint32_t m_read_begin;
        const Events<T>* m_events;
        const iterator_index m_begin;
        const iterator_index m_end;

       public:
        iterable_index(uint32_t read_begin, const Events<T>* events)
            : m_read_begin(read_begin),
              m_events(events),
              m_begin(read_begin, events),
              m_end(events->tail(), events) {}
        iterator_index begin() { return m_begin; }
        iterator_index end() { return m_end; }
    };

   private:
    Local<EventPointer<T>> m_pointer;
    Res<Events<T>> m_events;
    EventReader(Local<EventPointer<T>> pointer, Res<Events<T>> events)
        : m_pointer(pointer), m_events(events) {}

   public:
    static EventReader<T> from_system_param(
        Local<EventPointer<T>> pointer, Res<Events<T>> events
    ) {
        pointer->index = std::max(pointer->index, events->head());
        pointer->index = std::min(pointer->index, events->tail());
        return EventReader<T>(pointer, events);
    }

    /**
     * @brief Iterating through events this reader has not yet read.
     *
     * @return `iterable` object that can be used to iterate through events.
     */
    auto read() {
        iterable iter(m_pointer->index, m_events.get());
        m_pointer->index = m_events->tail();
        return iter;
    }
    auto read_with_index() {
        iterable_index iter(m_pointer->index, m_events.get());
        m_pointer->index = m_events->tail();
        return iter;
    }
    /**
     * @brief Get the remaining events this reader has not yet read.
     *
     * @return `size_t` number of events remaining.
     */
    size_t size() const { return m_events->tail() - m_pointer->index; }
    /**
     * @brief Read the next event.
     *
     * @return `T*` pointer to the next event, or `nullptr` if there are no more
     */
    T* read_one() {
        auto event = m_events->get(m_pointer->index);
        if (event) {
            m_pointer->index++;
            return event;
        } else {
            return nullptr;
        }
    }
    /**
     * @brief Read the next event and its index.
     *
     * @return `std::pair<uint32_t, T*>` pair of the index and pointer to the
     * next event, or {current_ptr, nullptr} if there are no more
     */
    std::pair<uint32_t, T*> read_one_index() {
        auto pair =
            std::make_pair(m_pointer->index, m_events->get(m_pointer->index));
        if (pair.second) {
            m_pointer->index++;
        }
        return pair;
    }
    bool empty() const { return m_pointer->index == m_events->tail(); }
};
template <typename T>
struct EventWriter {
   private:
    ResMut<Events<T>> m_events;
    EventWriter(ResMut<Events<T>> events) : m_events(events) {}

   public:
    static EventWriter<T> from_system_param(ResMut<Events<T>> events) {
        return EventWriter<T>(events);
    }

    void write(const T& event) { m_events->push(event); }
    void write(T&& event) { m_events->push(std::move(event)); }
    template <typename... Args>
    void emplace(Args&&... args) {
        m_events->emplace(std::forward<Args>(args)...);
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
    EPIX_API Entity id() const;

    template <typename T, typename... Args>
    void emplace(Args&&... args);
    template <typename T>
    void insert(T&& t);
    template <typename... Args>
    void erase();

    template <typename... Args>
    Entity spawn(Args&&... args);
};
struct WorldCommand {
   private:
    World* m_world;
    epix::utils::async::ConQueue<Entity> m_despawn;
    epix::utils::async::ConQueue<Entity> m_recurse_despawn;
    epix::utils::async::ConQueue<std::type_index> m_remove_resources;
    epix::utils::async::ConQueue<std::pair<void (*)(World*, Entity), Entity>>
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
    EntityCommand spawn(Args&&... args);
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
template <typename... T>
struct IntoQueryGetAbsolute;
template <>
struct IntoQueryGetAbsolute<> {
    using type = std::tuple<>;
};
template <typename A, typename... Other>
struct IntoQueryGetAbsolute<A, Other...> {
    using type = decltype(std::tuple_cat(
        std::declval<std::tuple<A>>(),
        std::declval<typename IntoQueryGetAbsolute<Other...>::type>()
    ));
};
template <typename A, typename... Other>
struct IntoQueryGetAbsolute<Has<A>, Other...> {
    using type = typename IntoQueryGetAbsolute<Other...>::type;
};
template <typename A, typename... Other>
struct IntoQueryGetAbsolute<Opt<A>, Other...> {
    using type = typename IntoQueryGetAbsolute<Other...>::type;
};
template <typename... Other>
struct IntoQueryGetAbsolute<Entity, Other...> {
    using type = typename IntoQueryGetAbsolute<Other...>::type;
};
template <typename T, typename U>
struct FromTupleGetView;

template <typename... T, typename... U>
struct FromTupleGetView<std::tuple<T...>, std::tuple<U...>> {
    using includes = std::tuple<T...>;
    using excludes = std::tuple<U...>;
    static auto get(entt::registry& registry) {
        return registry.view<T...>(entt::exclude<U...>);
    }
};

template <typename T>
struct is_has {
    static constexpr bool value = false;
};
template <typename T>
struct is_has<Has<T>> {
    static constexpr bool value = true;
};
template <typename T>
struct is_opt {
    static constexpr bool value = false;
};
template <typename T>
struct is_opt<Opt<T>> {
    static constexpr bool value = true;
};

template <typename T>
struct GetFromViewIterOrRegistry {
    template <typename... TupleT>
    static T& get(std::tuple<TupleT...>&& tuple, entt::registry& registry) {
        return std::get<T&>(tuple);
    }
};
template <typename T>
struct GetFromViewIterOrRegistry<Has<T>> {
    template <typename... TupleT>
    static bool get(std::tuple<TupleT...>&& tuple, entt::registry& registry) {
        auto id = std::get<entt::entity>(tuple);
        return registry.try_get<T>(id) != nullptr;
    }
};
template <typename T>
struct GetFromViewIterOrRegistry<Opt<T>> {
    template <typename... TupleT>
    static T* get(std::tuple<TupleT...>&& tuple, entt::registry& registry) {
        auto id = std::get<entt::entity>(tuple);
        return registry.try_get<T>(id);
    }
};
template <>
struct GetFromViewIterOrRegistry<Entity> {
    template <typename... TupleT>
    static Entity get(std::tuple<TupleT...>&& tuple, entt::registry& registry) {
        return Entity{std::get<entt::entity>(tuple)};
    }
};

template <typename T>
struct GetFromViewRegistry {
    template <typename ViewT>
    static T& get(ViewT&& view, entt::registry& registry, entt::entity id) {
        return view.template get<T>(id);
    }
};
template <typename T>
struct GetFromViewRegistry<Has<T>> {
    template <typename ViewT>
    static bool get(ViewT&& view, entt::registry& registry, entt::entity id) {
        return registry.try_get<T>(id) != nullptr;
    }
};
template <typename T>
struct GetFromViewRegistry<Opt<T>> {
    template <typename ViewT>
    static T* get(ViewT&& view, entt::registry& registry, entt::entity id) {
        return registry.try_get<T>(id);
    }
};
template <>
struct GetFromViewRegistry<Entity> {
    template <typename ViewT>
    static Entity get(ViewT&& view, entt::registry& registry, entt::entity id) {
        return Entity{id};
    }
};

template <typename... Gets, typename... Withs, typename... Withouts>
struct Query<Get<Gets...>, With<Withs...>, Without<Withouts...>> {
    using get_type = typename IntoQueryGetAbsolute<Gets..., Withs...>::type;
    using view_type =
        decltype(FromTupleGetView<get_type, std::tuple<Withouts...>>::get(
            std::declval<entt::registry&>()
        ));
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
                // explicitly declaring tuple type to avoid returning temporary
                // rvalue in tuple
                return std::tuple<decltype(GetFromViewIterOrRegistry<Gets>::get(
                    *m_iter, m_registry
                ))...>(
                    GetFromViewIterOrRegistry<Gets>::get(*m_iter, m_registry)...
                );
            }
        };

       public:
        iterable(iterable_type full, entt::registry& registry)
            : m_full(full), m_registry(registry) {}
        auto begin() { return iterator(m_full.begin(), m_registry); }
        auto end() { return iterator(m_full.end(), m_registry); }
    };

   private:
    entt::registry& registry;
    view_type m_view;

   public:
    Query(entt::registry& registry) : registry(registry) {
        m_view =
            FromTupleGetView<get_type, std::tuple<Withouts...>>::get(registry);
    }
    Query(const Query&) = default;
    Query(Query&& other) : Query(other) {}
    Query& operator=(const Query&) = default;
    Query& operator=(Query&& other) {
        m_view   = other.m_view;
        registry = other.registry;
        return *this;
    }

    /*! @brief Get the iterator for the query.
     * @return The iterator for the query.
     */
    auto iter() { return iterable(m_view.each(), registry); }
    auto get(entt::entity id) {
        // explicitly declaring tuple type to avoid returning temporary rvalue
        // in tuple
        return std::tuple<
            decltype(GetFromViewRegistry<Gets>::get(m_view, registry, id))...>(
            GetFromViewRegistry<Gets>::get(m_view, registry, id)...
        );
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
concept IsStaticFunction = std::is_function_v<T>;

template <typename T>
struct IsTupleV {
    static constexpr bool value = false;
};
template <typename... Args>
struct IsTupleV<std::tuple<Args...>> {
    static constexpr bool value = true;
};

template <typename T>
concept IsTuple = IsTupleV<std::decay_t<T>>::value;

template <typename T>
struct FunctionParam;

template <typename Ret, typename... Args>
struct FunctionParam<Ret(Args...)> {
    using type = std::tuple<Args...>;
};

template <typename T>
concept FromSystemParam = requires(T t) {
    IsStaticFunction<decltype(T::from_system_param)>;
    {
        std::apply(
            T::from_system_param,
            std::declval<typename FunctionParam<std::remove_pointer_t<
                std::decay_t<decltype(T::from_system_param)>>>::type>()
        )
    } -> std::same_as<T>;
};

template <typename T, template <typename...> typename U>
struct specialize_of {
    static constexpr bool value = false;
};
template <template <typename... Args> typename T, typename... Args>
struct specialize_of<T<Args...>, T> {
    static constexpr bool value = true;
};

template <typename T>
struct ExtractType {
    using type = T;
};

template <typename T>
struct ExtractType<Extract<T>> {
    using type = T;
};

template <typename T>
concept ValidSystemParam =
    FromSystemParam<T> || specialize_of<T, Query>::value ||
    specialize_of<T, Local>::value || specialize_of<T, Res>::value ||
    specialize_of<T, ResMut>::value || std::same_as<T, Command> ||
    std::same_as<World&, std::remove_cv_t<T>> ||
    (specialize_of<T, Extract>::value &&
     (specialize_of<typename ExtractType<T>::type, Query>::value ||
      specialize_of<typename ExtractType<T>::type, Local>::value ||
      specialize_of<typename ExtractType<T>::type, Res>::value ||
      specialize_of<typename ExtractType<T>::type, ResMut>::value ||
      std::same_as<typename ExtractType<T>::type, Command> ||
      FromSystemParam<typename ExtractType<T>::type>));

struct World {
   private:
    entt::registry m_registry;
    dense_map<std::type_index, UntypedRes> m_resources;
    std::shared_mutex m_resources_mutex;
    WorldCommand m_command;

   public:
    EPIX_API World();
    World(const World&)            = delete;
    World(World&&)                 = delete;
    World& operator=(const World&) = delete;
    World& operator=(World&&)      = delete;
    EPIX_API ~World();

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
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
            m_resources.emplace(
                typeid(T),
                UntypedRes{
                    std::static_pointer_cast<void>(
                        std::make_shared<std::decay_t<T>>()
                    ),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T>
    void insert_resource(T&& res) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
            m_resources.emplace(
                typeid(T),
                UntypedRes{
                    std::static_pointer_cast<void>(
                        std::make_shared<std::decay_t<T>>(std::forward<T>(res))
                    ),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T, typename... Args>
    void emplace_resource(Args&&... args) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
            m_resources.emplace(
                typeid(T),
                UntypedRes{
                    std::static_pointer_cast<void>(
                        std::make_shared<std::decay_t<T>>(
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
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
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
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
            m_resources.emplace(
                typeid(T),
                UntypedRes{std::static_pointer_cast<void>(res), mutex}
            );
        }
    }
    template <typename T>
    void add_resource(T* res) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
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
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
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

    template <typename T, typename... Args>
        requires std::constructible_from<std::decay_t<T>, Args...> ||
                 is_bundle<T>
    void entity_emplace(Entity entity, Args&&... args) {
        using type = std::decay_t<T>;
        if constexpr (is_bundle<T>) {
            auto&& bundle = T(std::forward<Args>(args)...);
            entity_emplace_tuple(entity, std::move(bundle.unpack()));
        } else {
            m_registry.emplace<std::decay_t<T>>(
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

    template <ValidSystemParam T>
    struct param_type;

    friend struct Schedule;
    friend struct WorldCommand;
    friend struct WorldEntityCommand;
};

template <typename T>
struct World::param_type<Res<T>> {
    using out_type = Res<T>;
    using in_type  = Res<T>;
    static Res<T> get(World* src, World* dst) {
        return std::move(dst->resource<T>().template into<T>());
    }
};
template <typename T>
struct World::param_type<ResMut<T>> {
    using out_type = ResMut<T>;
    using in_type  = ResMut<T>;
    static ResMut<T> get(World* src, World* dst) {
        return std::move(dst->resource<T>().template into_mut<T>());
    }
};
template <typename G, typename W, typename WO>
struct World::param_type<Query<G, W, WO>> {
    using out_type = Query<G, W, WO>;
    using in_type  = Query<G, W, WO>;
    static Query<G, W, WO> get(World* src, World* dst) {
        return Query<G, W, WO>(dst->m_registry);
    }
};
template <>
struct World::param_type<Command> {
    using out_type = Command;
    using in_type  = Command;
    static Command get(World* src, World* dst) {
        return Command(&src->m_command, &dst->m_command);
    }
};

template <typename T>
struct World::param_type<Extract<Res<T>>> {
    using out_type = Extract<Res<T>>;
    using in_type  = Extract<Res<T>>;
    static Extract<Res<T>> get(World* src, World* dst) {
        return std::move(src->resource<T>().template into<T>());
    }
};
template <typename T>
struct World::param_type<Extract<ResMut<T>>> {
    using out_type = Extract<ResMut<T>>;
    using in_type  = Extract<ResMut<T>>;
    static Extract<ResMut<T>> get(World* src, World* dst) {
        return std::move(src->resource<T>().template into_mut<T>());
    }
};
template <typename G, typename W, typename WO>
struct World::param_type<Extract<Query<G, W, WO>>> {
    using out_type = Extract<Query<G, W, WO>>;
    using in_type  = Extract<Query<G, W, WO>>;
    static Extract<Query<G, W, WO>> get(World* src, World* dst) {
        return Extract<Query<G, W, WO>>(src->m_registry);
    }
};

template <>
struct World::param_type<World&> {
    using out_type = World&;
    using in_type  = World&;
    static World& get(World* src, World* dst) { return *dst; }
};

template <typename T>
struct ParamResolve {
    using out_params = T;
    using in_params  = std::decay_t<T>;
};
template <typename T>
    requires std::same_as<std::decay_t<T>, World>
struct ParamResolve<T> {
    using out_params = World&;
    using in_params  = World&;
};
template <typename T>
struct ParamResolve<Extract<T>> {
    using out_params = Extract<T>;
    using in_params  = Extract<T>;
};
template <typename T>
struct ParamResolve<Extract<Local<T>>> {
    using out_params = Local<T>;
    using in_params  = Local<T>;
};
template <FromSystemParam T>
struct ParamResolve<T> {
    using out_params = T;
    using in_params =
        typename FunctionParam<decltype(T::from_system_param)>::type;
};
template <typename T>
struct TupleAddExtract {
    using type = T;
};
template <typename... Args>
struct TupleAddExtract<std::tuple<Args...>> {
    using type = std::tuple<Extract<Args>...>;
};
template <FromSystemParam T>
struct ParamResolve<Extract<T>> {
    using out_params = Extract<T>;
    using in_params  = typename TupleAddExtract<
         typename FunctionParam<decltype(T::from_system_param)>::type>::type;
};
template <typename T>
struct ParamResolve<Extract<Extract<T>>> {
    using out_params = ParamResolve<T>::out_params;
    using in_params  = ParamResolve<T>::in_params;
};
template <typename T>
struct ParamResolve<Local<T>> {
    using out_params = Local<T>;
    using in_params  = Local<T>;
};

template <typename... Args>
struct ParamResolve<std::tuple<Args...>> {
    using out_params = std::tuple<typename ParamResolve<Args>::out_params...>;
    using in_params  = std::tuple<typename ParamResolve<Args>::in_params...>;
    template <typename O, typename I>
    struct RootParams {
        using type =
            typename RootParams<I, typename ParamResolve<I>::in_params>::type;
    };
    template <typename T>
    struct RootParams<T, T> {
        using type = T;
    };
    using root_params = typename RootParams<out_params, in_params>::type;
    template <size_t I>
    static auto resolve_i(in_params&& in) {
        using type     = std::tuple_element_t<I, in_params>;
        using out_type = std::tuple_element_t<I, out_params>;
        if constexpr (IsTupleV<type>::value) {
            if constexpr (IsTupleV<out_type>::value) {
                // this is a tuple, so it needs to be resolved recursively
                return ParamResolve<out_type>::resolve(std::move(std::get<I>(in)
                ));
            } else {
                // this is a FromSystemParam, so it should just be constructed
                return std::apply(out_type::from_system_param, std::get<I>(in));
            }
        } else {
            return std::forward<type>(std::get<I>(in));
        }
    }
    template <size_t... I>
    static out_params resolve(in_params&& in, std::index_sequence<I...>) {
        return out_params(resolve_i<I>(std::forward<in_params>(in))...);
    }
    static out_params resolve(in_params&& in) {
        if constexpr (std::same_as<in_params, out_params>) {
            return std::forward<in_params>(in);
        } else {
            return resolve(
                std::forward<in_params>(in), std::index_sequence_for<Args...>()
            );
        }
    }
    static out_params resolve_from_root(root_params& in_addr) {
        if constexpr (std::same_as<root_params, out_params>) {
            return std::forward<root_params>(in_addr);
        } else {
            return resolve(ParamResolve<in_params>::resolve_from_root(in_addr));
        }
    }
};

template <typename T>
struct ParamResolver;

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
struct LocalType;
template <typename T>
struct LocalType<Local<T>> {
    using type = T;
};

template <typename T>
struct GetParam {
    static T get(World* src, World* dst, LocalData* local_data) {
        if constexpr (specialize_of<T, Local>::value) {
            return T(std::static_pointer_cast<typename LocalType<T>::type>(
                         local_data->get<typename LocalType<T>::type>()
            )
                         .get());
        } else {
            return World::param_type<T>::get(src, dst);
        }
    }
};
// template <typename T>
// struct GetParam<Extract<Extract<T>>> {
//     static T get(World* src, World* dst, LocalData* local_data) {
//         return GetParam<T>::get(src, dst, local_data);
//     }
// };

template <typename T>
struct GetParams {
    static T get(World* src, World* dst, LocalData* local_data) {
        return GetParam<T>::get(src, dst, local_data);
    }
};

template <typename... Args>
struct GetParams<std::tuple<Args...>> {
    static std::tuple<Args...> get(
        World* src, World* dst, LocalData* local_data
    ) {
        return std::forward_as_tuple(
            GetParams<Args>::get(src, dst, local_data)...
        );
    }
};

// prepare params. now only for resources since it need lock and unlock.
template <typename T>
struct PrepareParam {
    static void prepare(T& t) {};
    static void unprepare(T& t) {};
};

template <typename T>
struct PrepareParam<Res<T>> {
    static void prepare(Res<T>& t) { t.lock(); }
    static void unprepare(Res<T>& t) { t.unlock(); }
};
template <typename T>
struct PrepareParam<ResMut<T>> {
    static void prepare(ResMut<T>& t) { t.lock(); }
    static void unprepare(ResMut<T>& t) { t.unlock(); }
};

template <typename... Args>
struct PrepareParam<std::tuple<Args...>> {
    template <size_t... I>
    static void prepare(std::tuple<Args...>& t, std::index_sequence<I...>) {
        (PrepareParam<std::tuple_element_t<I, std::tuple<Args...>>>::prepare(
             std::get<I>(t)
         ),
         ...);
    }
    template <size_t... I>
    static void unprepare(std::tuple<Args...>& t, std::index_sequence<I...>) {
        (PrepareParam<std::tuple_element_t<I, std::tuple<Args...>>>::unprepare(
             std::get<I>(t)
         ),
         ...);
    }
    static void prepare(std::tuple<Args...>& t) {
        prepare(t, std::index_sequence_for<Args...>());
    }
    static void unprepare(std::tuple<Args...>& t) {
        unprepare(t, std::index_sequence_for<Args...>());
    }
};

template <typename... Args>
struct ParamResolver<std::tuple<Args...>> {
    using param_data_t =
        typename ParamResolve<std::tuple<Args...>>::root_params;

   private:
    param_data_t m_param_data;

    void prepare() { PrepareParam<param_data_t>::prepare(m_param_data); };
    void unprepare() { PrepareParam<param_data_t>::unprepare(m_param_data); };

   public:
    ParamResolver(World* src, World* dst, LocalData* local_data)
        : m_param_data(GetParams<param_data_t>::get(src, dst, local_data)) {
        prepare();
    }
    ~ParamResolver() { unprepare(); };

    std::tuple<Args...> resolve() {
        return ParamResolve<std::tuple<Args...>>::resolve_from_root(m_param_data
        );
    }
};

struct SystemParamInfo {
    bool has_world   = false;
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

    bool conflict_with(const SystemParamInfo& other) const {
        // any system with world is not thread safe, since you can get any
        // allowed param type from a single world.
        if (has_world || other.has_world) {
            return true;
        }
        // use command and query at the same time is now always thread safe
        // if (has_command && (other.has_command || other.has_query)) {
        //     return true;
        // }
        // if (other.has_command && (has_command || has_query)) {
        //     return true;
        // }
        // if two systems use command at the same time, it is not thread safe
        if (has_command && other.has_command) {
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
                if (get_b.contains(type) || with_b.contains(type)) return false;
            }
            for (auto& type : without_b) {
                if (get_a.contains(type) || with_a.contains(type)) return false;
            }
            for (auto& type : get_a) {
                if (get_b.contains(type) || with_b.contains(type)) return true;
            }
            for (auto& type : get_b) {
                if (get_a.contains(type) || with_a.contains(type)) return true;
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
            if (resource_types.contains(res) || resource_const.contains(res)) {
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
};

template <typename T>
struct SystemParamInfoWrite;

template <>
struct SystemParamInfoWrite<World&> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        dst.has_world = true;
    }
};

template <typename T>
struct SystemParamInfoWrite<Local<T>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {}
};

template <typename T>
struct SystemParamInfoWrite<Res<T>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        dst.resource_const.emplace(typeid(T));
    }
};

template <typename T>
struct SystemParamInfoWrite<ResMut<T>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        if constexpr (std::is_const_v<T> || external_thread_safe_v<T>) {
            dst.resource_const.emplace(typeid(T));
        } else {
            dst.resource_types.emplace(typeid(T));
        }
    }
};

template <typename T>
struct SystemParamInfoWrite<EventReader<T>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        dst.event_read_types.emplace(typeid(T));
    }
};
template <typename T>
struct SystemParamInfoWrite<EventWriter<T>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        dst.event_write_types.emplace(typeid(T));
    }
};

template <typename T>
struct QueryTypeDecay {
    using type = T;
};
template <typename T>
struct QueryTypeDecay<Has<T>> {
    using type = T;
};
template <typename T>
struct QueryTypeDecay<Opt<T>> {
    using type = T;
};
template <>
struct QueryTypeDecay<Entity> {
    using type = const Entity;
};

template <typename G, typename W, typename WO>
struct SystemParamInfoWrite<Query<G, W, WO>> {
    template <typename T>
    struct query_info_write {
        static void add(SystemParamInfo& src, SystemParamInfo& dst) {}
    };
    template <typename... Args>
    struct query_info_write<With<Args...>> {
        static void add(SystemParamInfo& src, SystemParamInfo& dst) {
            auto&& [mutable_types, const_types, exclude_types] =
                dst.query_types.back();
            (const_types.emplace(typeid(std::decay_t<Args>)), ...);
        }
    };
    template <typename... Args>
    struct query_info_write<Without<Args...>> {
        static void add(SystemParamInfo& src, SystemParamInfo& dst) {
            auto&& [mutable_types, const_types, exclude_types] =
                dst.query_types.back();
            (exclude_types.emplace(typeid(std::decay_t<Args>)), ...);
        }
    };
    template <typename... Args>
    struct query_info_write<Get<Args...>> {
        template <typename T>
        struct write_single {
            static void add(SystemParamInfo& src, SystemParamInfo& dst) {
                auto&& [mutable_types, const_types, exclude_types] =
                    dst.query_types.back();
                if constexpr (std::is_const_v<T> || external_thread_safe_v<T>) {
                    const_types.emplace(typeid(std::decay_t<T>));
                } else {
                    mutable_types.emplace(typeid(std::decay_t<T>));
                }
            }
        };
        static void add(SystemParamInfo& src, SystemParamInfo& dst) {
            (write_single<typename QueryTypeDecay<Args>::type>::add(src, dst),
             ...);
        }
    };
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        dst.query_types.emplace_back(
            entt::dense_set<std::type_index>{},
            entt::dense_set<std::type_index>{},
            entt::dense_set<std::type_index>{}
        );
        query_info_write<G>::add(src, dst);
        query_info_write<W>::add(src, dst);
        query_info_write<WO>::add(src, dst);
    }
};

template <>
struct SystemParamInfoWrite<Command> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        dst.has_command = true;
    }
};

template <typename T>
struct SystemParamInfoWrite<Extract<T>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        SystemParamInfoWrite<T>::add(dst, src);
    }
};

template <typename... Args>
struct SystemParamInfoWrite<std::tuple<Args...>> {
    static void add(SystemParamInfo& src, SystemParamInfo& dst) {
        (SystemParamInfoWrite<Args>::add(src, dst), ...);
    }
};

template <typename T>
struct param_decay {
    using type = std::decay_t<T>;
};

template <typename T>
    requires std::same_as<std::decay_t<T>, World>
struct param_decay<T> {
    static_assert(
        std::is_reference_v<T>, "World as param must be a reference type"
    );
    using type = World&;
};

template <typename T>
using param_decay_t = typename param_decay<T>::type;

template <typename T>
struct IsValidSystem {
    static constexpr bool value = false;
};

template <typename Ret, typename... Args>
struct IsValidSystem<std::function<Ret(Args...)>> {
    static constexpr bool value =
        (ValidSystemParam<param_decay_t<Args>> && ...);
};

template <typename T>
    requires requires(T t) {
        { std::function(t) };
    }
struct IsValidSystem<T> {
    static constexpr bool value =
        IsValidSystem<decltype(std::function(std::declval<T>()))>::value;
};

template <typename T>
concept ValidSystem = IsValidSystem<T>::value;

template <typename Ret>
struct BasicSystem {
   protected:
    LocalData m_locals;
    std::function<Ret(World*, World*, BasicSystem*)> m_func;
    double factor;
    double avg_time;  // in milliseconds
    SystemParamInfo system_param_src, system_param_dst;

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
    template <ValidSystem T>
    BasicSystem(T&& func) : BasicSystem(std::function(std::forward<T>(func))) {}
    template <typename... Args>
    // requires(ValidSystemParam<param_decay_t<Args>> && ...)
    BasicSystem(std::function<Ret(Args...)> func)
        : m_func([func](World* src, World* dst, BasicSystem* sys) {
              ParamResolver<std::tuple<param_decay_t<Args>...>> param_resolver(
                  src, dst, &sys->m_locals
              );
              auto resolved = param_resolver.resolve();
              return std::apply(func, resolved);
          }),
          factor(0.1),
          avg_time(1) {
        SystemParamInfoWrite<typename ParamResolve<std::tuple<param_decay_t<
            Args>...>>::root_params>::add(system_param_src, system_param_dst);
    }
    template <typename... Args>
    // requires(ValidSystemParam<param_decay_t<Args>> && ...)
    BasicSystem(Ret (*func)(Args...))
        : m_func([func](World* src, World* dst, BasicSystem* sys) {
              ParamResolver<std::tuple<param_decay_t<Args>...>> param_resolver(
                  src, dst, &sys->m_locals
              );
              auto resolved = param_resolver.resolve();
              return std::apply(func, resolved);
          }),
          factor(0.1),
          avg_time(1) {
        SystemParamInfoWrite<typename ParamResolve<std::tuple<param_decay_t<
            Args>...>>::root_params>::add(system_param_src, system_param_dst);
    }
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

struct SystemSet {
    std::type_index type;
    size_t value;

    template <typename T>
        requires std::is_enum_v<T>
    SystemSet(T value)
        : type(typeid(std::decay_t<T>)), value(static_cast<size_t>(value)) {}

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
        dense_set<FuncIndex> m_ptr_prevs;
        dense_set<FuncIndex> m_ptr_nexts;

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

    // set when building
    dense_set<std::weak_ptr<System>> m_prevs;
    dense_set<std::weak_ptr<System>> m_nexts;

    // set when baking
    dense_set<std::weak_ptr<System>> m_tmp_prevs;
    dense_set<std::weak_ptr<System>> m_tmp_nexts;

    // set when creationg
    dense_set<FuncIndex> m_ptr_prevs;
    dense_set<FuncIndex> m_ptr_nexts;

    // used when baking
    std::optional<double> m_reach_time;

    // used when running
    size_t m_prev_count;
    size_t m_next_count;
};
using SetMap = entt::dense_map<std::type_index, std::vector<SystemSet>>;
struct ScheduleId : Label {
    template <typename T>
    ScheduleId(T value) : Label(value) {}
    using Label::operator==;
    using Label::operator!=;
};
struct Executor {
   private:
    dense_map<std::string, std::shared_ptr<BS::thread_pool<BS::tp::priority>>>
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
    dense_map<FuncIndex, std::shared_ptr<System>> m_systems;
    bool m_run_once;

    std::type_index m_src_world;
    std::type_index m_dst_world;

    // set when building
    dense_set<std::weak_ptr<Schedule>> m_prev_schedules;
    dense_set<std::weak_ptr<Schedule>> m_next_schedules;

    // set when baking
    dense_set<std::weak_ptr<Schedule>> m_tmp_prevs;
    dense_set<std::weak_ptr<Schedule>> m_tmp_nexts;

    // set when creation
    dense_set<ScheduleId> m_prev_ids;
    dense_set<ScheduleId> m_next_ids;

    // used when running
    std::shared_ptr<epix::utils::async::ConQueue<std::shared_ptr<System>>>
        m_finishes;
    size_t m_prev_count = 0;

    // cached ops for runtime system adding and removal
    std::vector<FuncIndex> m_removes;
    std::vector<SystemAddInfo> m_adds;
    std::vector<std::pair<uint32_t, uint32_t>> m_cached_ops;
    std::unique_ptr<std::mutex> m_op_mutex;
    bool m_running = false;

    // used when baking
    double m_avg_time  = 1.0;
    double m_last_time = 0.0;
    std::optional<double> m_reach_time;

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
            std::type_index(typeid(std::decay_t<T>)),
            std::vector<SystemSet>{
                SystemSet(std::forward<T>(set)),
                SystemSet(std::forward<Args>(others))...
            }
        );
        return *this;
    }
    template <typename T>
    Schedule& set_src_world() {
        m_src_world = typeid(std::decay_t<T>);
        return *this;
    }
    template <typename T>
    Schedule& set_dst_world() {
        m_dst_world = typeid(std::decay_t<T>);
        return *this;
    }
    EPIX_API Schedule& add_system(SystemAddInfo&& info);
    EPIX_API Schedule& remove_system(FuncIndex index);
    EPIX_API void build();
    EPIX_API void bake();
    EPIX_API void run(World* src, World* dst, bool enable_tracy);
    EPIX_API void run(
        std::shared_ptr<System> system,
        World* src,
        World* dst,
        bool enable_tracy
    );
    EPIX_API Schedule& run_once(bool once = true);
    EPIX_API double get_avg_time() const;
    EPIX_API void clear_tmp();
    EPIX_API double reach_time();

    friend struct App;
};
struct ScheduleInfo : public ScheduleId {
    using ScheduleId::ScheduleId;
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
    requires std::same_as<SystemAddInfo, std::decay_t<T>>
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
    } || std::is_function_v<std::remove_pointer_t<std::decay_t<Func>>>
void info_append(SystemAddInfo& info, Func&& func) {
    if constexpr (std::is_function_v<
                      std::remove_pointer_t<std::decay_t<Func>>>) {
        info.m_systems.emplace_back(
            std::format("system:{:#016x}", (size_t)func), FuncIndex(func),
            std::make_unique<BasicSystem<void>>(func)
        );
    } else {
        auto ptr = std::make_unique<BasicSystem<void>>(func);
        FuncIndex index;
        index.set_index((size_t)ptr.get());
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
    bool enable_frame_mark                 = true;
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
    GraphId(T value) : std::type_index(typeid(std::decay_t<T>)) {}
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

struct ScheduleProfiles {
    struct ScheduleProfile {
        double time_last = 0.0;
        double time_avg  = 0.0;
    };

   private:
    dense_map<ScheduleId, ScheduleProfile> m_profiles;

   public:
    EPIX_API ScheduleProfile& profile(ScheduleId id);
    EPIX_API const ScheduleProfile& profile(ScheduleId id) const;
    EPIX_API ScheduleProfile* get_profile(ScheduleId id);
    EPIX_API const ScheduleProfile* get_profile(ScheduleId id) const;
    EPIX_API void for_each(
        const std::function<void(ScheduleId, ScheduleProfile&)>& func
    );
    EPIX_API void for_each(
        const std::function<void(ScheduleId, const ScheduleProfile&)>& func
    ) const;

    friend struct Schedule;
};

/**
 * @brief This is a struct that contains the setting data of the app.
 * This is not designed to be mutable, but maybe in the future we will
 * add some mutable data to it.
 */
struct AppInfo {
    bool enable_loop;
    bool enable_tracy;
    bool tracy_frame_mark;
};

struct AppSystems {
   private:
    App& app;

   public:
    AppSystems(App& app) : app(app) {}

    template <typename... Funcs>
    AppSystems& add_system(ScheduleInfo id, Funcs&&... funcs);
    template <typename... Args>
    AppSystems& remove_system(ScheduleId id, Args&&... args);
};

struct App {
    struct ScheduleGraph {
        dense_map<ScheduleId, std::shared_ptr<Schedule>> m_schedules;
        epix::utils::async::ConQueue<std::shared_ptr<Schedule>> m_finishes;
    };

   private:
    // plugin to build
    std::vector<std::pair<std::type_index, std::shared_ptr<Plugin>>> m_plugins;
    dense_set<std::type_index> m_built_plugins;

    // world data
    dense_map<std::type_index, std::unique_ptr<World>> m_worlds;

    // graph data
    dense_map<std::type_index, std::unique_ptr<ScheduleGraph>> m_graphs;
    dense_map<ScheduleId, std::type_index> m_graph_ids;

    // control and worker pools
    std::unique_ptr<BS::thread_pool<BS::tp::priority>> m_pool;
    std::shared_ptr<Executor> m_executor;

    // settings
    std::unique_ptr<bool> m_enable_loop;
    std::unique_ptr<bool> m_enable_tracy;
    std::unique_ptr<bool> m_tracy_frame_mark;

    // logger
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
    EPIX_API App& enable_frame_mark();
    EPIX_API App& disable_frame_mark();
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
        auto id = std::type_index(typeid(std::decay_t<T>));
        if (!m_worlds.contains(id)) {
            m_worlds.emplace(id, std::make_unique<World>());
        }
        return *this;
    };
    template <typename T>
    World& world() {
        auto id = std::type_index(typeid(std::decay_t<T>));
        if (auto it = m_worlds.find(id); it != m_worlds.end()) {
            return *it->second;
        }
        m_logger->error(
            "World {} not found. Please add it first.", typeid(T).name()
        );
        throw std::runtime_error("World not found.");
    };
    template <typename T>
    World* get_world() {
        auto id = std::type_index(typeid(std::decay_t<T>));
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
    App& add_system(ScheduleInfo info, Funcs&&... funcs) {
        ScheduleId& id = info;
        if (!m_graph_ids.contains(id)) {
            m_logger->warn(
                "Schedule {} not found. Ignoring add_system.", id.name()
            );
            return *this;
        }
        auto&& graph        = m_graphs.at(m_graph_ids.at(id));
        auto&& schedule     = graph->m_schedules[id];
        auto&& system_infos = std::array<SystemAddInfo, sizeof...(Funcs)>{
            into(std::forward<Funcs>(funcs))...
        };
        for (auto&& system_info : system_infos) {
            for (auto&& transform : info.transforms) {
                transform(system_info);
            }
        }
        for (auto&& system_info : system_infos) {
            schedule->add_system(std::move(system_info));
        }
        return *this;
    };
    template <typename... Args>
    App& remove_system(ScheduleId id, FuncIndex index, Args&&... args) {
        if (!m_graph_ids.contains(id)) {
            m_logger->warn(
                "Schedule {} not found. Ignoring remove_system.", id.name()
            );
            return *this;
        }
        auto&& graph    = m_graphs.at(m_graph_ids.at(id));
        auto&& schedule = graph->m_schedules[id];
        schedule->remove_system(index);
        if constexpr (sizeof...(Args)) {
            remove_system(id, args...);
        }
        return *this;
    }
    template <typename T>
    App& add_plugin(T&& plugin) {
        auto id = std::type_index(typeid(std::decay_t<T>));
        if (std::find_if(
                m_plugins.begin(), m_plugins.end(),
                [id](const auto& pair) { return pair.first == id; }
            ) == m_plugins.end()) {
            m_plugins.emplace_back(
                id, std::make_shared<std::decay_t<T>>(std::forward<T>(plugin))
            );
        }
        return *this;
    };

    template <typename T>
    T& plugin() {
        auto id = std::type_index(typeid(std::decay_t<T>));
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
        auto id = std::type_index(typeid(std::decay_t<T>));
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
        w.insert_resource<NextState<T>>(NextState<T>(std::forward<T>(state)));
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
        w.init_resource<NextState<T>>();
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
        w.init_resource<Events<T>>();
        // SystemAddInfo info;
        // auto func =
        //     std::make_unique<BasicSystem<void>>([](ResMut<Events<T>>
        //     event) {
        //         event->update();
        //     });
        // FuncIndex index;
        // index.func = func.get();
        // info.m_systems.emplace_back(
        //     std::format("update Event<{}>", typeid(T).name()), index,
        //     std::move(func)
        // );
        add_system(
            Last, into([](ResMut<Events<T>> event) { event->update(); }
                  ).set_label(std::format("update Event<{}>", typeid(T).name()))
        );
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
void WorldEntityCommand::insert(T&& obj) {
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
EntityCommand Command::spawn(Args&&... args) {
    auto id = dst_cmd->spawn(std::forward<Args>(args)...);
    return entity(id);
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

template <typename... Funcs>
AppSystems& AppSystems::add_system(ScheduleInfo id, Funcs&&... funcs) {
    app.add_system(id, std::forward<Funcs>(funcs)...);
    return *this;
};
template <typename... Args>
AppSystems& AppSystems::remove_system(ScheduleId id, Args&&... args) {
    app.remove_system(id, std::forward<Args>(args)...);
    return *this;
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
using app::Has;
using app::Local;
using app::NextState;
using app::Opt;
using app::Query;
using app::Res;
using app::ResMut;
using app::State;
using app::With;
using app::Without;

// APP UTILS
using app::AppInfo;
using app::AppSystems;

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