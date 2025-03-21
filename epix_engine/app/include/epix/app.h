#pragma once

// ----THIRD PARTY INCLUDES----
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

namespace epix::app {
struct World;
struct SubApp;
struct MainSubApp;
struct RenderSubApp;
struct App;
struct AppSettings;
struct AppExit;

struct Plugin;

struct FuncIndex;

struct SystemStage;
struct SystemSet;
struct SystemNode;
template <typename Ret, typename... Args>
struct SystemT;

using SetMap = entt::dense_map<std::type_index, std::vector<SystemSet>>;

template <typename T>
struct MsgQueueBase;
struct WorkerPool;
struct Runner;
struct StageRunner;
struct SubStageRunner;

template <typename T>
struct BasicSystem;

// STRUCTS THAT USED MOSTLY IN SYSTEMS

struct Entity;
struct EntityCommand;
struct Command;

template <typename T>
struct Res;
template <typename T>
struct ResMut;

template <typename T>
struct Local;

struct Bundle;
struct Parent;
struct Children;

template <typename T>
struct State;
template <typename T>
struct NextState;

struct EventQueueBase;
template <typename T>
struct EventQueue;
template <typename T>
struct EventReader;
template <typename T>
struct EventWriter;

template <typename... T>
struct Get;
template <typename... T>
struct With;
template <typename... T>
struct Without;
template <typename Get, typename With = With<>, typename Without = Without<>>
struct QueryBase;
template <typename Get, typename With = With<>, typename Without = Without<>>
struct Extract;
template <typename Get, typename With = With<>, typename Without = Without<>>
struct Query;

// UTILS
template <typename T>
concept is_enum   = std::is_enum_v<T>;
using thread_pool = BS::thread_pool<BS::tp::priority>;

namespace stages {
enum MainStartupStage {
    PreStartup,
    Startup,
    PostStartup,
};
enum MainTransitStage {
    Transit,
};
enum MainLoopStage {
    First,
    PreUpdate,
    Update,
    PostUpdate,
    Last,
};
enum MainExitStage {
    PreExit,
    Exit,
    PostExit,
};
enum RenderStartupStage {
    PreRenderStartup,
    RenderStartup,
    PostRenderStartup,
};
enum RenderLoopStage {
    Prepare,
    PreRender,
    Render,
    PostRender,
};
enum RenderExitStage {
    PreRenderExit,
    RenderExit,
    PostRenderExit,
};
enum ExtractStage {
    PreExtract,
    Extraction,
    PostExtract,
};
}  // namespace stages
}  // namespace epix::app

namespace epix::app_tools {
using namespace epix::app;
template <template <typename...> typename Template, typename T>
struct is_template_of : std::false_type {};

template <template <typename...> typename Template, typename... Args>
struct is_template_of<Template, Template<Args...>> : std::true_type {};

template <typename T, typename = void, typename = void>
struct is_bundle : std::false_type {};

template <typename T>
struct is_bundle<T, std::void_t<decltype(std::declval<T>().unpack())>>
    : std::true_type {};

template <typename... Args>
void registry_emplace_tuple(
    entt::registry* registry, entt::entity entity, std::tuple<Args...>&& tuple
);

template <typename T>
void registry_emplace_single(
    entt::registry* registry, entt::entity entity, T&& arg
) {
    if constexpr (is_bundle<std::remove_reference_t<T>>::value &&
                  std::is_base_of_v<Bundle, std::remove_reference_t<T>>) {
        registry_emplace_tuple(registry, entity, std::forward<T>(arg).unpack());
    } else if constexpr (!std::is_same_v<Bundle, std::remove_reference_t<T>>) {
        registry->emplace<std::remove_const_t<std::remove_reference_t<T>>>(
            entity, std::forward<T>(arg)
        );
    }
}

template <typename... Args>
void registry_emplace(
    entt::registry* registry, entt::entity entity, Args&&... args
) {
    auto&& arr = {(
        registry_emplace_single(registry, entity, std::forward<Args>(args)), 0
    )...};
}

template <typename... Args, size_t... I>
void registry_emplace_tuple(
    entt::registry* registry,
    entt::entity entity,
    std::tuple<Args...>&& tuple,
    std::index_sequence<I...> _
) {
    registry_emplace(
        registry, entity,
        std::forward<
            decltype(std::get<I>(std::forward<std::tuple<Args...>&&>(tuple)))>(
            std::get<I>(std::forward<std::tuple<Args...>&&>(tuple))
        )...
    );
}

template <typename... Args>
void registry_emplace_tuple(
    entt::registry* registry, entt::entity entity, std::tuple<Args...>&& tuple
) {
    registry_emplace_tuple(
        registry, entity, std::forward<decltype(tuple)>(tuple),
        std::make_index_sequence<sizeof...(Args)>()
    );
}

template <typename... Args>
void registry_erase(entt::registry* registry, entt::entity entity) {
    auto&& arr = {(registry->remove<Args>(entity), 0)...};
}

template <typename T, typename Tuple>
struct tuple_contain;

template <typename T>
struct tuple_contain<T, std::tuple<>> : std::false_type {};

template <typename T, typename U, typename... Ts>
struct tuple_contain<T, std::tuple<U, Ts...>>
    : tuple_contain<T, std::tuple<Ts...>> {};

template <typename T, typename... Ts>
struct tuple_contain<T, std::tuple<T, Ts...>> : std::true_type {};

template <template <typename...> typename T, typename Tuple>
struct tuple_contain_template;

template <template <typename...> typename T>
struct tuple_contain_template<T, std::tuple<>> : std::false_type {};

template <template <typename...> typename T, typename U, typename... Ts>
struct tuple_contain_template<T, std::tuple<U, Ts...>>
    : tuple_contain_template<T, std::tuple<Ts...>> {};

template <template <typename...> typename T, typename... Ts, typename... Temps>
struct tuple_contain_template<T, std::tuple<T<Temps...>, Ts...>>
    : std::true_type {};

template <template <typename...> typename T, typename Tuple>
struct tuple_template_index {};

template <template <typename...> typename T, typename U, typename... Args>
struct tuple_template_index<T, std::tuple<U, Args...>> {
    static constexpr int index() {
        if constexpr (is_template_of<T, U>::value) {
            return 0;
        } else {
            return 1 + tuple_template_index<T, std::tuple<Args...>>::index();
        }
    }
};

template <typename T, typename... Args>
T tuple_get(std::tuple<Args...> tuple) {
    if constexpr (tuple_contain<T, std::tuple<Args...>>::value) {
        return std::get<T>(tuple);
    } else {
        return T();
    }
}

template <template <typename...> typename T, typename... Args>
constexpr auto tuple_get_template(std::tuple<Args...> tuple) {
    if constexpr (tuple_contain_template<T, std::tuple<Args...>>::value) {
        return std::get<tuple_template_index<T, std::tuple<Args...>>::index()>(
            tuple
        );
    } else {
        return T();
    }
}

template <typename T>
struct t_weak_ptr : std::weak_ptr<T> {
    t_weak_ptr(std::shared_ptr<T> ptr) : std::weak_ptr<T>(ptr) {}
    t_weak_ptr(std::weak_ptr<T> ptr) : std::weak_ptr<T>(ptr) {}

    T* get_p() { return std::weak_ptr<T>::get(); }
};

template <typename T>
struct ::std::hash<std::weak_ptr<T>> {
    size_t operator()(const std::weak_ptr<T>& ptr) const {
        epix::app_tools::t_weak_ptr<T> tptr(ptr);
        return std::hash<T*>()(tptr.get_p());
    }
};

template <typename T>
struct ::std::equal_to<std::weak_ptr<T>> {
    bool operator()(const std::weak_ptr<T>& a, const std::weak_ptr<T>& b)
        const {
        epix::app_tools::t_weak_ptr<T> aptr(a);
        epix::app_tools::t_weak_ptr<T> bptr(b);
        return aptr.get_p() == bptr.get_p();
    }
};
}  // namespace epix::app_tools

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
        const epix::app::Entity& lhs, const epix::app::Entity& rhs
    ) const;
};

namespace epix::app {
struct World {
    entt::registry m_registry;
    entt::dense_map<std::type_index, std::shared_ptr<void>> m_resources;
    entt::dense_map<std::type_index, std::unique_ptr<EventQueueBase>>
        m_event_queues;
};
struct FuncIndex {
    std::type_index type;
    void* func;
    template <typename T, typename... Args>
    FuncIndex(T (*func)(Args...))
        : type(typeid(T)), func(static_cast<void*>(func)) {}
    EPIX_API bool operator==(const FuncIndex& other) const;
};
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
template <typename ResT>
struct Res {
   private:
    const ResT* m_res;

   public:
    Res(void* resource) : m_res(static_cast<const ResT*>(resource)) {}
    Res() : m_res(nullptr) {}

    /**
     * @brief Check if the resource has a value.
     */
    bool has_value() { return m_res != nullptr; }

    operator bool() { return has_value(); }
    bool operator!() { return !has_value(); }

    const ResT& operator*() { return *m_res; }
    const ResT* operator->() { return m_res; }

    const ResT* get() { return m_res; }
};

template <typename ResT>
struct ResMut {
   private:
    ResT* m_res;

   public:
    ResMut(void* resource) : m_res(static_cast<ResT*>(resource)) {}
    ResMut() : m_res(nullptr) {}

    /**
     * @brief Check if the resource has a value.
     */
    bool has_value() { return m_res != nullptr; }

    operator bool() { return has_value(); }
    bool operator!() { return !has_value(); }

    operator Res<ResT>() { return Res<ResT>(m_res); }

    ResT& operator*() { return *m_res; }
    ResT* operator->() { return m_res; }

    ResT* get() { return m_res; }
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
struct Bundle {};
struct Parent {
    Entity id;
};
struct Children {
    entt::dense_set<Entity> children;
};
template <typename T>
struct State {
    friend class app::App;
    friend class app::SubApp;

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
struct EventQueueBase {
    virtual void tick() = 0;
};
template <typename T>
struct EventQueue : public EventQueueBase {
    struct elem {
        T event;
        uint8_t count;
    };
    void tick() override {
        while (!m_queue.empty()) {
            auto& front = m_queue.front();
            if (front.count == 0) {
                m_queue.pop_front();
            } else {
                break;
            }
        }
        for (auto it = m_queue.begin(); it != m_queue.end();) {
            it->count--;
            it++;
        }
    }
    void push(const T& event) { m_queue.push_back({event, 1}); }
    void push(T&& event) { m_queue.push_back({std::forward<T>(event), 1}); }
    void clear() { m_queue.clear(); }
    bool empty() const { return m_queue.empty(); }
    struct iterator {
        std::deque<elem>::iterator it;
        iterator operator++() {
            it++;
            return *this;
        }
        bool operator!=(const iterator& other) { return it != other.it; }
        const T& operator*() { return it->event; }
    };
    iterator begin() { return iterator{m_queue.begin()}; }
    iterator end() { return iterator{m_queue.end()}; }

   private:
    std::deque<elem> m_queue;
};
template <typename T>
struct EventReader {
    EventReader(EventQueueBase* queue)
        : m_queue(dynamic_cast<EventQueue<T>*>(queue)) {}

    struct iter {
        iter(EventQueue<T>* queue) : m_queue(queue) {}
        auto begin() { return m_queue->begin(); }
        auto end() { return m_queue->end(); }

       private:
        EventQueue<T>* m_queue;
    };

    iter read() { return iter(m_queue); }
    void clear() { m_queue->clear(); }
    bool empty() { return m_queue->empty(); }

   private:
    EventQueue<T>* m_queue;
};

template <typename T>
struct EventWriter {
    EventWriter(EventQueueBase* queue)
        : m_queue(dynamic_cast<EventQueue<T>*>(queue)) {}

    EventWriter& write(const T& event) {
        m_queue->push(event);
        return *this;
    }
    EventWriter& write(T&& event) {
        m_queue->push(std::forward<T>(event));
        return *this;
    }

   private:
    EventQueue<T>* m_queue;
};
struct EntityCommand {
   private:
    entt::registry* const m_registry;
    const Entity m_entity;
    entt::dense_set<Entity>* m_despawns;
    entt::dense_set<Entity>* m_recursive_despawns;

   public:
    EPIX_API EntityCommand(
        entt::registry* registry,
        Entity entity,
        entt::dense_set<Entity>* despawns,
        entt::dense_set<Entity>* recursive_despawns
    );
    /*! @brief Spawn an entity.
     * Note that the components to be added should not be type that is
     * inherited or a reference.
     * @brief Accepted types are:
     * @brief - Any pure struct.
     * @brief - std::tuple<Args...> where Args... are pure structs.
     * @brief - A pure struct that contains a Bundle, which means it is
     * a bundle, and all its fields will be components.
     * @tparam Args The types of the components to be added to the
     * child entity.
     * @param args The components to be added to the child entity.
     * @return The entity command for the child entity.
     */
    template <typename... Args>
    EntityCommand spawn(Args&&... args) {
        auto child = m_registry->create();
        app_tools::registry_emplace(
            m_registry, child, Parent{.id = m_entity}, args...
        );
        auto& children = m_registry->get_or_emplace<Children>(m_entity.id);
        children.children.insert({child});
        return {m_registry, {child}, m_despawns, m_recursive_despawns};
    }
    /*! @brief Emplace new components to the entity.
     * @tparam Args The types of the components to be added to the
     * entity.
     * @param args The components to be added to the entity.
     */
    template <typename... Args>
    void emplace(Args&&... args) {
        app_tools::registry_emplace(
            m_registry, m_entity.id, std::forward<Args>(args)...
        );
    }
    template <typename... Args>
    void erase() {
        app_tools::registry_erase<Args...>(m_registry, m_entity.id);
    }

    /*! @brief Despawn an entity.
     */
    EPIX_API void despawn();
    /*! @brief Despawn an entity and all its children.
     */
    EPIX_API void despawn_recurse();
    EPIX_API Entity id();
    EPIX_API operator Entity();
};
struct Command {
   private:
    World* const m_world;
    World* const m_src;
    std::shared_ptr<entt::dense_set<Entity>> m_despawns;
    std::shared_ptr<entt::dense_set<Entity>> m_recursive_despawns;
    std::shared_ptr<std::vector<std::function<void(World*)>>>
        m_resource_removers;

   public:
    EPIX_API Command(World* world, World* src);
    /**
     * @brief Get the entity command for the entity.
     *
     * @param entity The entity id.
     * @return `EntityCommand` The entity command.
     */
    EPIX_API EntityCommand entity(Entity entity);
    /*! @brief Spawn an entity.
     * Note that the components to be added should not be type that is
     * inherited or a reference.
     * @brief Accepted types are:
     * @brief - Any pure struct.
     * @brief - std::tuple<Args...> where Args... are pure structs.
     * @brief - A pure struct that contains a Bundle, which means it is
     * a bundle, and all its fields will be components.
     * @tparam Args The types of the components to be added to the
     * child entity.
     * @param args The components to be added to the child entity.
     * @return The entity command for new entity.
     */
    template <typename... Args>
    EntityCommand spawn(Args&&... args) {
        auto m_registry = &m_world->m_registry;
        auto entity     = m_registry->create();
        app_tools::registry_emplace(
            m_registry, entity, std::forward<Args>(args)...
        );
        return EntityCommand(
            m_registry, {entity}, m_despawns.get(), m_recursive_despawns.get()
        );
    }
    /*! @brief Insert a resource.
     * If the resource already exists, nothing will happen.
     * @tparam T The type of the resource.
     * @tparam Args The types of the arguments to be passed to the
     * @param args The arguments to be passed to the constructor of T
     */
    template <typename T, typename... Args>
    void emplace_resource(Args&&... args) {
        auto m_resources = &m_world->m_resources;
        if (m_resources->find(std::type_index(typeid(std::remove_reference_t<T>)
            )) == m_resources->end()) {
            m_resources->emplace(
                std::type_index(typeid(std::remove_reference_t<T>)),
                std::static_pointer_cast<void>(
                    std::make_shared<std::remove_reference_t<T>>(
                        std::forward<Args>(args)...
                    )
                )
            );
        }
    }
    template <typename T>
    void insert_resource(T&& res) {
        auto m_resources = &m_world->m_resources;
        if (m_resources->find(std::type_index(typeid(std::remove_reference_t<T>)
            )) == m_resources->end()) {
            m_resources->emplace(
                std::type_index(typeid(std::remove_reference_t<T>)),
                std::static_pointer_cast<void>(
                    std::make_shared<std::remove_reference_t<T>>(
                        std::forward<T>(res)
                    )
                )
            );
        }
    }
    template <typename T>
    void add_resource(std::shared_ptr<T> res) {
        auto m_resources = &m_world->m_resources;
        if (m_resources->find(std::type_index(typeid(std::remove_reference_t<T>)
            )) == m_resources->end()) {
            m_resources->emplace(
                std::type_index(typeid(std::remove_reference_t<T>)),
                std::static_pointer_cast<void>(res)
            );
        }
    }
    template <typename T>
    void add_resource(T* res) {
        auto m_resources = &m_world->m_resources;
        if (m_resources->find(std::type_index(typeid(std::remove_reference_t<T>)
            )) == m_resources->end()) {
            m_resources->emplace(
                std::type_index(typeid(std::remove_reference_t<T>)),
                std::static_pointer_cast<void>(std::shared_ptr<T>(res))
            );
        }
    }
    template <typename T>
    void remove_resource() {
        m_resource_removers->push_back([](World* world) {
            auto& resources = world->m_resources;
            resources.erase(std::type_index(typeid(std::remove_reference_t<T>))
            );
        });
    }
    /*! @brief Insert Resource using default values.
     * If the resource already exists, nothing will happen.
     * @tparam T The type of the resource.
     */
    template <typename T>
    void init_resource() {
        auto m_resources = &m_world->m_resources;
        if (m_resources->find(std::type_index(typeid(std::remove_reference_t<T>)
            )) == m_resources->end()) {
            auto res = std::static_pointer_cast<void>(
                std::make_shared<std::remove_reference_t<T>>()
            );
            m_resources->emplace(
                std::type_index(typeid(std::remove_reference_t<T>)), res
            );
        }
    }
    /*! @brief Share a resource from the source world.
     * If the resource already exists or the source world does not have
     * the resource, nothing will happen.
     * @tparam T The type of the resource.
     */
    template <typename T>
    void share_resource(Res<T>&) {
        auto& src_res = m_src->m_resources;
        auto& dst_res = m_world->m_resources;
        auto src_it   = src_res.find(std::type_index(typeid(T)));
        auto dst_it   = dst_res.find(std::type_index(typeid(T)));
        if (src_it != src_res.end() && dst_it == dst_res.end()) {
            dst_res.emplace(src_it->first, src_it->second);
        }
    }
    /*! @brief Share a resource from the source world.
     * If the resource already exists or the source world does not have
     * the resource, nothing will happen.
     * @tparam T The type of the resource.
     */
    template <typename T>
    void share_resource(ResMut<T>&) {
        auto& src_res = m_src->m_resources;
        auto& dst_res = m_world->m_resources;
        auto src_it   = src_res.find(std::type_index(typeid(T)));
        auto dst_it   = dst_res.find(std::type_index(typeid(T)));
        if (src_it != src_res.end() && dst_it == dst_res.end()) {
            dst_res.emplace(src_it->first, src_it->second);
        }
    }

   private:
    EPIX_API void end();

    friend struct SubApp;
};
template <typename... T>
struct Get {};
template <typename... T>
struct With {};
template <typename... T>
struct Without {};

template <typename... Qus, typename... Ins, typename... Exs>
class QueryBase<Get<Qus...>, With<Ins...>, Without<Exs...>> {
    using view_type =
        decltype(std::declval<entt::registry>().view<Qus..., Ins...>(
            entt::exclude_t<Exs...>{}
        ));
    using iterable_type = decltype(std::declval<view_type>().each());
    using iterator_type = decltype(std::declval<iterable_type>().begin());

   private:
    entt::registry& registry;
    view_type m_view;

   public:
    QueryBase(entt::registry& registry) : registry(registry) {
        m_view = registry.view<Qus..., Ins...>(entt::exclude_t<Exs...>{});
    }

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
                return std::tuple<Qus&...>(std::get<Qus&>(*m_iter)...);
            }
            auto wrap() {
                return Wrapper<Qus...>(
                    Entity{std::get<entt::entity>(*m_iter)}, registry
                );
            }
        };

       public:
        iterable(iterable_type full) : m_full(full) {}
        auto begin() { return iterator(m_full.begin()); }
        auto end() { return iterator(m_full.end()); }
    };

    /*! @brief Get the iterator for the query.
     * @return The iterator for the query.
     */
    auto iter() { return iterable(m_view.each()); }
    std::tuple<Qus&...> get(entt::entity id) { return m_view.get<Qus...>(id); }
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

    auto wrap(Entity entity) { return Wrapper<Qus...>(entity, registry); }

    auto size_hint() { return m_view.size_hint(); }

    template <typename Func>
    void for_each(Func func) {
        m_view.each(func);
    }
};

template <typename... Qus, typename... Ins, typename... Exs>
class QueryBase<Get<Entity, Qus...>, With<Ins...>, Without<Exs...>> {
    using view_type =
        decltype(std::declval<entt::registry>().view<Qus..., Ins...>(
            entt::exclude_t<Exs...>{}
        ));
    using iterable_type = decltype(std::declval<view_type>().each());
    using iterator_type = decltype(std::declval<iterable_type>().begin());

   private:
    entt::registry& registry;
    view_type m_view;

   public:
    QueryBase(entt::registry& registry) : registry(registry) {
        m_view = registry.view<Qus..., Ins...>(entt::exclude_t<Exs...>{});
    }

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
                return std::tuple<Entity, Qus&...>(
                    Entity{std::get<entt::entity>(*m_iter)},
                    std::get<Qus&>(*m_iter)...
                );
            }
            auto wrap() {
                return Wrapper<Qus...>(
                    Entity{std::get<entt::entity>(*m_iter)}, registry
                );
            }
        };

       public:
        iterable(decltype(m_full) full) : m_full(full) {}
        auto begin() { return iterator(m_full.begin()); }
        auto end() { return iterator(m_full.end()); }
    };

    /*! @brief Get the iterator for the query.
     * @return The iterator for the query.
     */
    auto iter() { return iterable(m_view.each()); }

    std::tuple<Qus&...> get(Entity id) { return m_view.get<Qus...>(id); }

    bool contains(Entity id) { return m_view.contains(id); }

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

    auto wrap(Entity entity) { return Wrapper<Qus...>(entity, registry); }

    auto size_hint() { return m_view.size_hint(); }

    template <typename Func>
    void for_each(Func func) {
        m_view.each(func);
    }
};

template <typename... Gets, typename... Withouts, typename W>
struct QueryBase<Get<Gets...>, Without<Withouts...>, W>
    : QueryBase<Get<Gets...>, With<>, Without<Withouts...>> {};
template <typename... Gets, typename... Withs, typename... Withouts>
struct Extract<Get<Gets...>, With<Withs...>, Without<Withouts...>> {
    using type = QueryBase<Get<Gets...>, With<Withs...>, Without<Withouts...>>;

   private:
    type query;

   public:
    Extract(entt::registry& registry) : query(registry) {}
    Extract(type query) : query(query) {}
    Extract(type&& query) : query(query) {}
    auto iter() { return query.iter(); }
    auto single() { return query.single(); }
    operator bool() { return query; }
    bool operator!() { return !query; }
    template <typename Func>
    void for_each(Func func) {
        query.for_each(func);
    }
    auto get(entt::entity id) { return query.get(id); }
    auto wrap(Entity entity) { return query.wrap(entity); }
    auto size_hint() { return query.size_hint(); }
    bool contains(entt::entity id) { return query.contains(id); }
};
template <typename... Gets, typename... Withs, typename... Withouts>
struct Extract<Get<Entity, Gets...>, With<Withs...>, Without<Withouts...>> {
    using type =
        QueryBase<Get<Entity, Gets...>, With<Withs...>, Without<Withouts...>>;

   private:
    type query;

   public:
    Extract(entt::registry& registry) : query(registry) {}
    Extract(type query) : query(query) {}
    Extract(type&& query) : query(query) {}
    auto iter() { return query.iter(); }
    auto single() { return query.single(); }
    operator bool() { return query; }
    bool operator!() { return !query; }
    template <typename Func>
    void for_each(Func func) {
        query.for_each(func);
    }
    auto get(Entity id) { return query.get(id); }
    auto wrap(Entity entity) { return query.wrap(entity); }
    auto size_hint() { return query.size_hint(); }
    bool contains(Entity id) { return query.contains(id); }
};
template <typename... Gets, typename... Withouts, typename W>
struct Extract<Get<Gets...>, Without<Withouts...>, W>
    : Extract<Get<Gets...>, With<>, Without<Withouts...>> {};
template <typename G, typename W, typename WO>
struct Query {
    using type = QueryBase<G, W, WO>;

   private:
    type query;

   public:
    Query(entt::registry& registry) : query(registry) {}
    Query(type query) : query(query) {}
    Query(type&& query) : query(query) {}
    auto iter() { return query.iter(); }
    auto single() { return query.single(); }
    operator bool() { return query; }
    bool operator!() { return !query; }
    template <typename Func>
    void for_each(Func func) {
        query.for_each(func);
    }
    auto get(Entity id) { return query.get(id); }
    auto wrap(Entity entity) { return query.wrap(entity); }
    auto size_hint() { return query.size_hint(); }
    bool contains(Entity id) { return query.contains(id); }
};
struct SubApp {
    template <typename T>
    struct value_type {
        static T get(SubApp& app) {
            if constexpr (app_tools::is_template_of<Query, T>::value) {
                return T(app.m_world.m_registry);
            } else if constexpr (app_tools::is_template_of<Extract, T>::value) {
                return T(app.m_world.m_registry);
            } else {
                static_assert(false, "Not allowed type");
            }
        }
        static T get(SubApp& src, SubApp& dst) {
            if constexpr (app_tools::is_template_of<Query, T>::value) {
                return T(dst.m_world.m_registry);
            } else if constexpr (app_tools::is_template_of<Extract, T>::value) {
                return T(src.m_world.m_registry);
            } else {
                static_assert(false, "Not allowed type");
            }
        }
    };

    template <typename ResT>
    struct value_type<Res<ResT>> {
        static Res<ResT> get(SubApp& app) {
            return Res<ResT>(app.m_world
                                 .m_resources[std::type_index(
                                     typeid(std::remove_const_t<ResT>)
                                 )]
                                 .get());
        }
        static Res<ResT> get(SubApp& src, SubApp& dst) { return get(src); }
    };

    template <typename ResT>
    struct value_type<ResMut<ResT>> {
        static ResMut<ResT> get(SubApp& app) {
            return ResMut<ResT>(app.m_world
                                    .m_resources[std::type_index(
                                        typeid(std::remove_const_t<ResT>)
                                    )]
                                    .get());
        }
        static ResMut<ResT> get(SubApp& src, SubApp& dst) { return get(src); }
    };

    template <>
    struct value_type<Command> {
        static Command get(SubApp& app) {
            app.m_command_cache.emplace_back(&app.m_world, &app.m_world);
            return app.m_command_cache.back();
        }
        static Command get(SubApp& src, SubApp& dst) {
            dst.m_command_cache.emplace_back(&dst.m_world, &src.m_world);
            return dst.m_command_cache.back();
        }
    };

    template <typename T>
    struct value_type<EventReader<T>> {
        static EventReader<T> get(SubApp& app) {
            auto it =
                app.m_world.m_event_queues.find(std::type_index(typeid(T)));
            if (it == app.m_world.m_event_queues.end()) {
                app.m_world.m_event_queues.emplace(
                    std::type_index(typeid(T)),
                    std::make_unique<EventQueue<T>>()
                );
                return EventReader<T>(
                    app.m_world.m_event_queues[std::type_index(typeid(T))].get()
                );
            }
            return EventReader<T>(it->second.get());
        }
        static EventReader<T> get(SubApp& src, SubApp& dst) { return get(src); }
    };

    template <typename T>
    struct value_type<EventWriter<T>> {
        static EventWriter<T> get(SubApp& app) {
            auto it =
                app.m_world.m_event_queues.find(std::type_index(typeid(T)));
            if (it == app.m_world.m_event_queues.end()) {
                app.m_world.m_event_queues.emplace(
                    std::type_index(typeid(T)),
                    std::make_unique<EventQueue<T>>()
                );
                return EventWriter<T>(
                    app.m_world.m_event_queues[std::type_index(typeid(T))].get()
                );
            }
            return EventWriter<T>(it->second.get());
        }
        static EventWriter<T> get(SubApp& src, SubApp& dst) { return get(dst); }
    };

    template <typename... Gs, typename... Ws, typename... WOs>
    struct value_type<Query<Get<Gs...>, With<Ws...>, Without<WOs...>>> {
        static Query<Get<Gs...>, With<Ws...>, Without<WOs...>> get(SubApp& app
        ) {
            return Query<Get<Gs...>, With<Ws...>, Without<WOs...>>(
                app.m_world.m_registry
            );
        }
        static Query<Get<Gs...>, With<Ws...>, Without<WOs...>> get(
            SubApp& src, SubApp& dst
        ) {
            return get(dst);
        }
    };

    template <typename... Gs, typename... Ws, typename... WOs>
    struct value_type<Extract<Get<Gs...>, With<Ws...>, Without<WOs...>>> {
        static Extract<Get<Gs...>, With<Ws...>, Without<WOs...>> get(SubApp& app
        ) {
            return Extract<Get<Gs...>, With<Ws...>, Without<WOs...>>(
                app.m_world.m_registry
            );
        }
        static Extract<Get<Gs...>, With<Ws...>, Without<WOs...>> get(
            SubApp& src, SubApp& dst
        ) {
            return get(src);
        }
    };

   public:
    EPIX_API void tick_events();
    EPIX_API void end_commands();
    EPIX_API void update_states();

    template <typename T, typename... Args>
    void emplace_resource(Args&&... args) {
        Command command(&m_world);
        command.emplace_resource<T>(std::forward<Args>(args)...);
    }
    template <typename T>
    void insert_resource(T&& res) {
        Command command(&m_world);
        command.insert_resource(std::forward<T>(res));
    }
    template <typename T>
    void add_resource(std::shared_ptr<T> res) {
        Command command(&m_world);
        command.add_resource(res);
    }
    template <typename T>
    void add_resource(T* res) {
        Command command(&m_world);
        command.add_resource(res);
    }
    template <typename T>
    void init_resource() {
        Command command(&m_world);
        command.init_resource<T>();
    }
    template <typename T>
    void insert_state(T&& state) {
        if (m_world.m_resources.find(std::type_index(typeid(State<T>))) !=
                m_world.m_resources.end() ||
            m_world.m_resources.find(std::type_index(typeid(NextState<T>))) !=
                m_world.m_resources.end()) {
            spdlog::warn("State {} already exists.", typeid(T).name());
            return;
        }
        Command command(&m_world, &m_world);
        command.insert_resource(State<T>(std::forward<T>(state)));
        command.insert_resource(NextState<T>(std::forward<T>(state)));
        m_state_updates.emplace_back(std::make_unique<BasicSystem<void>>(
            [](ResMut<State<T>> state, Res<NextState<T>> next_state) {
                state->m_state      = next_state->m_state;
                state->just_created = false;
            }
        ));
    }
    template <typename T>
    void init_state() {
        if (m_world.m_resources.find(std::type_index(typeid(State<T>))) !=
                m_world.m_resources.end() ||
            m_world.m_resources.find(std::type_index(typeid(NextState<T>))) !=
                m_world.m_resources.end()) {
            spdlog::warn("State {} already exists.", typeid(T).name());
            return;
        }
        Command command(&m_world, &m_world);
        command.init_resource<State<T>>();
        command.init_resource<NextState<T>>();
        m_state_updates.emplace_back(std::make_unique<BasicSystem<void>>(
            [](ResMut<State<T>> state, Res<NextState<T>> next_state) {
                state->m_state      = next_state->m_state;
                state->just_created = false;
            }
        ));
    }
    template <typename T>
    void add_event() {
        if (m_world.m_event_queues.find(std::type_index(typeid(T))) !=
            m_world.m_event_queues.end())
            return;
        m_world.m_event_queues.emplace(
            std::type_index(typeid(T)), std::make_unique<EventQueue<T>>()
        );
    }

   private:
    World m_world;
    std::vector<Command> m_command_cache;
    std::vector<std::unique_ptr<BasicSystem<void>>> m_state_updates;

    friend struct App;
};

template <typename Ret>
struct BasicSystem {
   protected:
    entt::dense_map<std::type_index, std::shared_ptr<void>> m_locals;
    std::function<Ret(SubApp*, SubApp*)> m_func;
    double factor;
    double avg_time;  // in milliseconds
    struct system_info {
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
        entt::dense_set<std::type_index> state_types;
        entt::dense_set<std::type_index> next_state_types;
    } system_infos;

    template <typename Arg>
    struct info_add {
        static void add(entt::dense_set<std::type_index>& infos) {
            infos.emplace(typeid(std::remove_const_t<Arg>));
        }
    };

    template <typename Arg>
    struct const_infos_adder {
        static void add(entt::dense_set<std::type_index>& infos) {
            if constexpr (std::is_const_v<Arg>)
                infos.emplace(typeid(std::remove_const_t<Arg>));
        }
    };

    template <typename Arg>
    struct mutable_infos_adder {
        static void add(entt::dense_set<std::type_index>& infos) {
            if constexpr (!std::is_const_v<Arg>) infos.emplace(typeid(Arg));
        }
    };

    template <typename T>
    struct infos_adder {
        static void add(system_info& info) {};
    };

    template <typename... Includes, typename... Withs, typename... Excludes>
    struct infos_adder<
        Query<Get<Includes...>, With<Withs...>, Without<Excludes...>>> {
        static void add(system_info& info) {
            auto& query_types = info.query_types;
            entt::dense_set<std::type_index> query_include_types,
                query_exclude_types, query_include_const;
            (mutable_infos_adder<Includes>::add(query_include_types), ...);
            (const_infos_adder<Includes>::add(query_include_const), ...);
            (info_add<Withs>::add(query_include_const), ...);
            (info_add<Excludes>::add(query_exclude_types), ...);
            query_types.emplace_back(
                std::move(query_include_types), std::move(query_include_const),
                std::move(query_exclude_types)
            );
        }
    };

    template <typename... Includes, typename... Excludes, typename T>
    struct infos_adder<Query<Get<Includes...>, Without<Excludes...>, T>> {
        static void add(system_info& info) {
            auto& query_types = info.query_types;
            entt::dense_set<std::type_index> query_include_types,
                query_exclude_types, query_include_const;
            (mutable_infos_adder<Includes>::add(query_include_types), ...);
            (const_infos_adder<Includes>::add(query_include_const), ...);
            (info_add<Excludes>::add(query_exclude_types), ...);
            query_types.emplace_back(
                std::move(query_include_types), std::move(query_include_const),
                std::move(query_exclude_types)
            );
        }
    };

    template <typename T>
    struct infos_adder<Res<T>> {
        static void add(system_info& info) {
            auto& resource_const = info.resource_const;
            info_add<T>().add(resource_const);
        }
    };

    template <typename T>
    struct infos_adder<ResMut<T>> {
        static void add(system_info& info) {
            auto& resource_types = info.resource_types;
            auto& resource_const = info.resource_const;
            if constexpr (std::is_const_v<T>)
                info_add<T>().add(resource_const);
            else
                info_add<T>().add(resource_types);
        }
    };

    template <typename T>
    struct infos_adder<EventReader<T>> {
        static void add(system_info& info) {
            auto& event_read_types = info.event_read_types;
            info_add<T>().add(event_read_types);
        }
    };

    template <typename T>
    struct infos_adder<EventWriter<T>> {
        static void add(system_info& info) {
            auto& event_write_types = info.event_write_types;
            info_add<T>().add(event_write_types);
        }
    };

    template <typename T>
    struct infos_adder<State<T>> {
        static void add(system_info& info) {
            auto& state_types = info.state_types;
            info_add<T>().add(state_types);
        }
    };

    template <typename T>
    struct infos_adder<NextState<T>> {
        static void add(system_info& info) {
            auto& next_state_types = info.next_state_types;
            info_add<T>().add(next_state_types);
        }
    };

    template <typename Arg, typename... Args>
    void add_infos_inernal() {
        using namespace app_tools;
        if constexpr (std::is_same_v<Arg, Command>) {
            system_infos.has_command = true;
        } else if constexpr (is_template_of<Query, Arg>::value) {
            system_infos.has_query = true;
            infos_adder<Arg>().add(system_infos);
        } else {
            infos_adder<Arg>().add(system_infos);
        }
    }

    template <typename... Args>
    void add_infos() {
        (add_infos_inernal<Args>(), ...);
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

        auto& has_command       = system_infos.has_command;
        auto& has_query         = system_infos.has_query;
        auto& query_types       = system_infos.query_types;
        auto& resource_types    = system_infos.resource_types;
        auto& resource_const    = system_infos.resource_const;
        auto& event_read_types  = system_infos.event_read_types;
        auto& event_write_types = system_infos.event_write_types;
        auto& state_types       = system_infos.state_types;
        auto& next_state_types  = system_infos.next_state_types;

        if (has_command && (other->system_infos.has_command ||
                            other->system_infos.has_query)) {
            m_contrary_to.insert(other);
            other->m_contrary_to.insert(this);
            return true;
        }
        if (has_query && other->system_infos.has_command) {
            m_contrary_to.insert(other);
            other->m_contrary_to.insert(this);
            return true;
        }
        for (auto& [query_include_types, query_include_const, query_exclude_types] :
             query_types) {
            for (auto& [other_query_include_types, other_query_include_const, other_query_exclude_types] :
                 other->system_infos.query_types) {
                bool this_exclude_other = false;
                for (auto type : query_exclude_types) {
                    if (std::find(
                            other_query_include_types.begin(),
                            other_query_include_types.end(), type
                        ) != other_query_include_types.end()) {
                        this_exclude_other = true;
                    }
                    if (std::find(
                            other_query_include_const.begin(),
                            other_query_include_const.end(), type
                        ) != other_query_include_const.end()) {
                        this_exclude_other = true;
                    }
                }
                if (this_exclude_other) continue;
                bool other_exclude_this = false;
                for (auto type : other_query_exclude_types) {
                    if (std::find(
                            query_include_types.begin(),
                            query_include_types.end(), type
                        ) != query_include_types.end()) {
                        other_exclude_this = true;
                    }
                    if (std::find(
                            query_include_const.begin(),
                            query_include_const.end(), type
                        ) != query_include_const.end()) {
                        other_exclude_this = true;
                    }
                }
                if (other_exclude_this) continue;
                for (auto type : query_include_types) {
                    if (std::find(
                            other_query_include_types.begin(),
                            other_query_include_types.end(), type
                        ) != other_query_include_types.end()) {
                        m_contrary_to.insert(other);
                        other->m_contrary_to.insert(this);
                        return true;
                    }
                    if (std::find(
                            other_query_include_const.begin(),
                            other_query_include_const.end(), type
                        ) != other_query_include_const.end()) {
                        m_contrary_to.insert(other);
                        other->m_contrary_to.insert(this);
                        return true;
                    }
                }
            }
        }

        bool resource_one_empty = resource_types.empty() ||
                                  other->system_infos.resource_types.empty();
        bool resource_contrary = false;
        if (!resource_one_empty) {
            for (auto type : resource_types) {
                if (std::find(
                        other->system_infos.resource_const.begin(),
                        other->system_infos.resource_const.end(), type
                    ) != other->system_infos.resource_const.end()) {
                    resource_contrary = true;
                }
                if (std::find(
                        other->system_infos.resource_types.begin(),
                        other->system_infos.resource_types.end(), type
                    ) != other->system_infos.resource_types.end()) {
                    resource_contrary = true;
                }
            }
            for (auto type : other->system_infos.resource_types) {
                if (std::find(
                        resource_const.begin(), resource_const.end(), type
                    ) != resource_const.end()) {
                    resource_contrary = true;
                }
                if (std::find(
                        resource_types.begin(), resource_types.end(), type
                    ) != resource_types.end()) {
                    resource_contrary = true;
                }
            }
        }
        if (resource_contrary) {
            m_contrary_to.insert(other);
            other->m_contrary_to.insert(this);
            return true;
        }

        bool event_contrary = false;
        for (auto type : event_write_types) {
            if (std::find(
                    other->system_infos.event_write_types.begin(),
                    other->system_infos.event_write_types.end(), type
                ) != other->system_infos.event_write_types.end()) {
                event_contrary = true;
            }
            if (std::find(
                    other->system_infos.event_read_types.begin(),
                    other->system_infos.event_read_types.end(), type
                ) != other->system_infos.event_read_types.end()) {
                event_contrary = true;
            }
        }
        for (auto type : other->system_infos.event_write_types) {
            if (std::find(
                    event_write_types.begin(), event_write_types.end(), type
                ) != event_write_types.end()) {
                event_contrary = true;
            }
            if (std::find(
                    event_read_types.begin(), event_read_types.end(), type
                ) != event_read_types.end()) {
                event_contrary = true;
            }
        }
        if (event_contrary) {
            m_contrary_to.insert(other);
            other->m_contrary_to.insert(this);
            return true;
        }

        bool state_contrary = false;
        for (auto type : next_state_types) {
            if (std::find(
                    other->system_infos.next_state_types.begin(),
                    other->system_infos.next_state_types.end(), type
                ) != other->system_infos.next_state_types.end()) {
                state_contrary = true;
            }
            if (std::find(
                    other->system_infos.state_types.begin(),
                    other->system_infos.state_types.end(), type
                ) != other->system_infos.state_types.end()) {
                state_contrary = true;
            }
        }
        for (auto type : other->system_infos.next_state_types) {
            if (std::find(
                    next_state_types.begin(), next_state_types.end(), type
                ) != next_state_types.end()) {
                state_contrary = true;
            }
            if (std::find(state_types.begin(), state_types.end(), type) !=
                state_types.end()) {
                state_contrary = true;
            }
        }
        if (state_contrary) {
            m_contrary_to.insert(other);
            other->m_contrary_to.insert(this);
            return true;
        }
        m_not_contrary_to.insert(other);
        other->m_not_contrary_to.insert(this);
        return false;
    }
    const double get_avg_time() const { return avg_time; }
    struct ParaRetriever {
        template <typename T>
        static T retrieve(
            BasicSystem<Ret>* basic_sys, SubApp* src, SubApp* dst
        ) {
            if constexpr (app_tools::is_template_of<Local, T>::value) {
                return BasicSystem<Ret>::LocalRetriever<T>::get(basic_sys);
            } else {
                return SubApp::value_type<T>::get(*src, *dst);
            }
        }
    };
    template <typename... Args>
    BasicSystem(std::function<Ret(Args...)> func)
        : m_func([func, this](SubApp* src, SubApp* dst) {
              return func(ParaRetriever::retrieve<Args>(this, src, dst)...);
          }),
          factor(0.1),
          avg_time(1) {
        add_infos<Args...>();
    }
    template <typename... Args>
    BasicSystem(Ret (*func)(Args...))
        : m_func([func, this](SubApp* src, SubApp* dst) {
              return func(ParaRetriever::retrieve<Args>(this, src, dst)...);
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
    BasicSystem(const BasicSystem& other)            = delete;
    BasicSystem(BasicSystem&& other)                 = delete;
    BasicSystem& operator=(const BasicSystem& other) = delete;
    BasicSystem& operator=(BasicSystem&& other)      = delete;
    Ret run(SubApp* src, SubApp* dst) {
        auto start = std::chrono::high_resolution_clock::now();
        if constexpr (std::is_same_v<Ret, void>) {
            m_func(src, dst);
            auto end = std::chrono::high_resolution_clock::now();
            auto delta =
                (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    end - start
                )
                    .count() /
                1000000.0;
            avg_time = delta * 0.1 + avg_time * 0.9;
        } else {
            auto&& ret = m_func(src, dst);
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

struct SystemStage {
    template <typename T>
    SystemStage(T stage)
        : m_stage(typeid(T)), m_sub_stage(static_cast<size_t>(stage)) {}
    EPIX_API bool operator==(const SystemStage& other) const;
    EPIX_API bool operator!=(const SystemStage& other) const;

    std::type_index m_stage;
    size_t m_sub_stage;
};
struct SystemSet {
    template <typename T>
    SystemSet(T set) : m_type(typeid(T)), m_value(static_cast<size_t>(set)) {}
    EPIX_API bool operator==(const SystemSet& other) const;
    EPIX_API bool operator!=(const SystemSet& other) const;

    std::type_index m_type;
    size_t m_value;
};
struct SystemAddInfo {
    struct each_t {
        std::string name;
        FuncIndex index;
        std::unique_ptr<BasicSystem<void>> system;
        std::vector<std::unique_ptr<BasicSystem<bool>>> conditions;

        each_t(
            const std::string& name,
            FuncIndex index,
            std::unique_ptr<BasicSystem<void>> system
        )
            : name(name), index(index), system(std::move(system)) {}
        each_t(each_t&& other)
            : name(other.name),
              index(other.index),
              system(std::move(other.system)) {}
        each_t& operator=(each_t&& other) {
            name   = other.name;
            index  = other.index;
            system = std::move(other.system);
            return *this;
        }
    };
    std::vector<each_t> m_systems;

    std::vector<SystemSet> m_in_sets;
    std::string m_worker = "default";
    entt::dense_set<FuncIndex> m_ptr_prevs;
    entt::dense_set<FuncIndex> m_ptr_nexts;

    bool is_state_transition = false;
    bool m_chain             = false;

    SystemAddInfo()                                      = default;
    SystemAddInfo(const SystemAddInfo& other)            = delete;
    SystemAddInfo(SystemAddInfo&& other)                 = default;
    SystemAddInfo& operator=(const SystemAddInfo& other) = delete;
    SystemAddInfo& operator=(SystemAddInfo&& other)      = default;

    SystemAddInfo& chain() {
        m_chain = true;
        return *this;
    }

    template <typename T, typename... Args>
    SystemAddInfo& before(const SystemT<T, Args...>& system) {
        m_ptr_nexts.emplace(system.index);
        return *this;
    }
    template <typename T, typename... Args>
    SystemAddInfo& before(T (*func)(Args...)) {
        m_ptr_nexts.emplace(FuncIndex(func));
        return *this;
    }
    template <typename T, typename... Args>
    SystemAddInfo& after(const SystemT<T, Args...>& system) {
        m_ptr_prevs.emplace(system.index);
        return *this;
    }
    template <typename T, typename... Args>
    SystemAddInfo& after(T (*func)(Args...)) {
        m_ptr_prevs.emplace(FuncIndex(func));
        return *this;
    }
    template <typename T>
    SystemAddInfo& in_set(T t) {
        m_in_sets.emplace_back(SystemSet(t));
        return *this;
    }
    SystemAddInfo& worker(const std::string& worker) {
        m_worker = worker;
        return *this;
    }
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
    SystemAddInfo& on_enter(T state) {
        is_state_transition = true;
        run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
            return (cur->is_state(state) && cur->is_just_created()) ||
                   (!cur->is_state(state) && next->is_state(state));
        });
        return *this;
    };
    template <typename T>
    SystemAddInfo& on_exit(T state) {
        is_state_transition = true;
        run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
            return cur->is_state(state) && !next->is_state(state);
        });
        return *this;
    };
    template <typename T>
    SystemAddInfo& on_change() {
        is_state_transition = true;
        run_if([](Res<State<T>> cur, Res<NextState<T>> next) {
            return !cur->is_state(next->m_state);
        });
        return *this;
    };
    template <typename T>
    SystemAddInfo& in_state(T state) {
        is_state_transition = true;
        run_if([state](Res<State<T>> cur) { return cur->is_state(state); });
        return *this;
    };
};

template <typename Ret, typename... Args>
struct SystemT {
    using return_type    = Ret;
    using argument_types = std::tuple<Args...>;
    using func_type      = Ret (*)(Args...);
    std::string name;
    FuncIndex index;
    std::function<Ret(Args...)> func;

    SystemT(const std::string& name, Ret (*func)(Args...))
        : name(name), index(FuncIndex(func)), func(func) {}
    SystemT(Ret (*func)(Args...))
        : name(std::format("{:#016x}", (size_t)func)),
          index(FuncIndex(func)),
          func(func) {}

    operator FuncIndex&() { return index; }
    operator const FuncIndex&() const { return index; }
    operator SystemAddInfo() const {
        SystemAddInfo info;
        info.m_systems.emplace_back(
            name, index, std::make_unique<BasicSystem<void>>(func)
        );
        return std::move(info);
    }

    template <typename T>
    SystemAddInfo before(T&& system) {
        return std::move(operator SystemAddInfo().before(system));
    }
    template <typename T>
    SystemAddInfo after(T&& system) {
        return std::move(operator SystemAddInfo().after(system));
    }
    template <typename T>
    SystemAddInfo in_set(T t) {
        return std::move(operator SystemAddInfo().in_set(t));
    }
    SystemAddInfo worker(const std::string& worker) {
        return std::move(operator SystemAddInfo().worker(worker));
    }
    template <typename... Args>
    SystemAddInfo run_if(const std::function<bool(Args...)>& func) {
        return std::move(operator SystemAddInfo().run_if(func));
    }
    template <typename... Args>
    SystemAddInfo run_if(bool (*func)(Args...)) {
        return std::move(operator SystemAddInfo().run_if(func));
    }
    template <typename T>
    SystemAddInfo on_enter(T state) {
        return std::move(operator SystemAddInfo().on_enter(state));
    };
    template <typename T>
    SystemAddInfo on_exit(T state) {
        return std::move(operator SystemAddInfo().on_exit(state));
    };
    template <typename T>
    SystemAddInfo on_change() {
        return std::move(operator SystemAddInfo().on_change());
    };
    template <typename T>
    SystemAddInfo in_state(T state) {
        return std::move(operator SystemAddInfo().in_state(state));
    };
};

template <typename T>
auto&& into(T&& t) {
    return std::forward<T>(t);
}

template <typename Ret, typename... Args>
SystemT<Ret, Args...> into(Ret (*func)(Args...), const std::string& name) {
    return SystemT<Ret, Args...>(name, func);
}

template <typename Ret, typename... Args>
SystemT<Ret, Args...> into(Ret (*func)(Args...)) {
    return SystemT<Ret, Args...>(std::format("{:#016x}", (size_t)func), func);
}

template <typename T>
concept SystemLike = requires(T t) {
    { t.name };
    { t.index };
    { std::make_unique<BasicSystem<void>>(t.func) };
};

template <typename... Systems>
    requires(SystemLike<std::remove_cvref_t<Systems>> && ...)
SystemAddInfo bundle(Systems&&... systems) {
    SystemAddInfo info;
    (info.m_systems.emplace_back(
         systems.name, systems.index,
         std::make_unique<BasicSystem<void>>(systems.func)
     ),
     ...);
    return std::move(info);
}
template <typename... Systems, typename... Args, typename Ret>
SystemAddInfo bundle(Ret (*systems)(Args...), Systems&&... rest) {
    return std::move(bundle(into(systems), into(rest)...));
}
template <typename... Systems>
    requires(SystemLike<std::remove_cvref_t<Systems>> && ...)
SystemAddInfo chain(Systems&&... systems) {
    return std::move(bundle(systems...).chain());
}
template <typename... Systems, typename... Args, typename Ret>
SystemAddInfo chain(Ret (*systems)(Args...), Systems&&... rest) {
    return std::move(chain(into(systems), into(rest)...));
}

struct SystemNode {
    template <typename StageT, typename... Args>
    SystemNode(
        StageT stage,
        const std::string& id,
        FuncIndex index,
        std::unique_ptr<BasicSystem<void>>&& system
    )
        : m_stage(stage),
          m_system(std::move(system)),
          m_id(id),
          m_sys_addr(index) {}
    EPIX_API bool run(SubApp* src, SubApp* dst);
    EPIX_API void clear_tmp();
    EPIX_API double reach_time();

    SystemStage m_stage;
    std::string m_id;
    FuncIndex m_sys_addr;
    std::unique_ptr<BasicSystem<void>> m_system;
    std::vector<SystemSet> m_in_sets;
    std::string m_worker = "default";
    entt::dense_set<std::weak_ptr<SystemNode>> m_strong_prevs;
    entt::dense_set<std::weak_ptr<SystemNode>> m_strong_nexts;
    entt::dense_set<std::weak_ptr<SystemNode>> m_weak_prevs;
    entt::dense_set<std::weak_ptr<SystemNode>> m_weak_nexts;
    entt::dense_set<FuncIndex> m_ptr_prevs;
    entt::dense_set<FuncIndex> m_ptr_nexts;
    std::vector<std::unique_ptr<BasicSystem<bool>>> m_conditions;

    std::optional<double> m_reach_time;
    size_t m_prev_count;
    size_t m_next_count;
};

template <typename T>
struct MsgQueueBase {
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    MsgQueueBase() = default;
    MsgQueueBase(MsgQueueBase&& other) : m_queue(std::move(other.m_queue)) {}
    auto& operator=(MsgQueueBase&& other) {
        m_queue = std::move(other.m_queue);
        return *this;
    }
    void push(T elem) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.push(elem);
        m_cv.notify_one();
    }
    T pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        T system = m_queue.front();
        m_queue.pop();
        return system;
    }
    T front() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.front();
    }
    bool empty() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty(); });
    }
};
struct WorkerPool {
    EPIX_API thread_pool* get_pool(const std::string& name);
    EPIX_API void add_pool(const std::string& name, uint32_t num_threads);

    entt::dense_map<std::string, std::unique_ptr<thread_pool>> m_pools;
};
struct SubStageRunner {
    template <typename StageT>
    SubStageRunner(
        StageT stage, SubApp* src, SubApp* dst, WorkerPool* pools, SetMap* sets
    )
        : m_src(src),
          m_dst(dst),
          m_pools(pools),
          m_sets(sets),
          m_sub_stage(stage) {
        m_logger = spdlog::default_logger()->clone(
            std::string("sub_stage: ") + typeid(StageT).name() + "-" +
            std::to_string(static_cast<size_t>(stage))
        );
    }
    SubStageRunner(SubStageRunner&& other)            = default;
    SubStageRunner& operator=(SubStageRunner&& other) = default;
    template <typename StageT>
    SubStageRunner& add_system(StageT stage, SystemAddInfo&& system) {
        for (size_t i = 0; i < system.m_systems.size(); i++) {
            auto&& each = system.m_systems[i];
            if (m_systems.find(each.index) != m_systems.end()) {
                m_logger->warn(
                    "system-{:#016x} is already present, ignoring this add."
                );
                continue;
            }
            std::shared_ptr<SystemNode> target = std::make_shared<SystemNode>(
                stage, each.name, each.index, std::move(each.system)
            );
            std::move(
                each.conditions.begin(), each.conditions.end(),
                std::back_inserter(target->m_conditions)
            );
            target->m_ptr_nexts = system.m_ptr_nexts;
            for (size_t j = i + 1; j < system.m_systems.size(); j++) {
                target->m_ptr_nexts.emplace(system.m_systems[j].index);
            }
            target->m_ptr_prevs = system.m_ptr_prevs;
            target->m_worker    = system.m_worker;
            target->m_in_sets   = system.m_in_sets;
            m_systems.emplace(each.index, target);
        }
        return *this;
    }
    EPIX_API void build();
    EPIX_API void bake();
    EPIX_API void run(std::shared_ptr<SystemNode> node);
    EPIX_API void run();
    EPIX_API void set_log_level(spdlog::level::level_enum level);

   protected:
    SubApp* m_src;
    SubApp* m_dst;
    WorkerPool* m_pools;

    SystemStage m_sub_stage;
    MsgQueueBase<std::shared_ptr<SystemNode>> msg_queue;
    entt::dense_map<FuncIndex, std::shared_ptr<SystemNode>> m_systems;
    std::vector<std::shared_ptr<SystemNode>> m_heads;
    SetMap* m_sets;

    std::shared_ptr<spdlog::logger> m_logger;
};
struct StageRunner {
    EPIX_API StageRunner(
        std::type_index stage,
        SubApp* src,
        SubApp* dst,
        WorkerPool* pools,
        SetMap* sets
    );
    EPIX_API StageRunner(StageRunner&& other)            = default;
    EPIX_API StageRunner& operator=(StageRunner&& other) = default;

    template <typename StageT>
    void add_sub_stage(StageT sub_stage) {
        if (std::type_index(typeid(StageT)) != m_stage) {
            spdlog::warn(
                "Stage {} cannot add sub stage {} - {}", m_stage.name(),
                typeid(StageT).name(), static_cast<size_t>(sub_stage)
            );
            return;
        }
        m_sub_stages.emplace(
            static_cast<size_t>(sub_stage),
            std::make_unique<SubStageRunner>(
                sub_stage, m_src, m_dst, m_pools, m_sets
            )
        );
    }

    template <typename StageT, typename... Subs>
    void configure_sub_stage(StageT sub_stage, Subs... sub_stages) {
        add_sub_stage(sub_stage);
        (add_sub_stage(sub_stages), ...);
        m_sub_stage_order = {
            static_cast<size_t>(sub_stage), static_cast<size_t>(sub_stages)...
        };
    }

    template <typename StageT>
    StageRunner& add_system(StageT stage, SystemAddInfo&& systems) {
        if (auto it = m_sub_stages.find(static_cast<size_t>(stage));
            it == m_sub_stages.end()) {
            m_sub_stages.emplace(
                static_cast<size_t>(stage),
                std::make_unique<SubStageRunner>(
                    stage, m_src, m_dst, m_pools, m_sets
                )
            );
        }
        m_sub_stages[static_cast<size_t>(stage)]->add_system(
            stage, std::move(systems)
        );
        return *this;
    }

    EPIX_API bool conflict(const StageRunner* other) const;

    EPIX_API void build();
    EPIX_API void bake();
    EPIX_API void run();
    EPIX_API void set_log_level(spdlog::level::level_enum level);

   protected:
    SubApp* m_src;
    SubApp* m_dst;
    WorkerPool* m_pools;
    SetMap* m_sets;

    std::type_index m_stage;
    entt::dense_map<size_t, std::unique_ptr<SubStageRunner>> m_sub_stages;
    std::vector<size_t> m_sub_stage_order;
    std::shared_ptr<spdlog::logger> m_logger;
};
struct Runner {
    EPIX_API Runner(
        entt::dense_map<std::type_index, std::unique_ptr<SubApp>>* sub_apps
    );
    EPIX_API Runner(Runner&& other)            = default;
    EPIX_API Runner& operator=(Runner&& other) = default;

    struct StageNode {
        std::type_index stage;
        std::unique_ptr<StageRunner> runner;
        entt::dense_set<std::weak_ptr<StageNode>> strong_prev_stages;
        entt::dense_set<std::weak_ptr<StageNode>> strong_next_stages;
        entt::dense_set<std::weak_ptr<StageNode>> weak_prev_stages;
        entt::dense_set<std::weak_ptr<StageNode>> weak_next_stages;
        entt::dense_set<std::type_index> prev_stages;
        entt::dense_set<std::type_index> next_stages;
        size_t prev_count;
        std::optional<size_t> depth;
        EPIX_API StageNode(
            std::type_index stage, std::unique_ptr<StageRunner>&& runner
        );
        template <typename T>
        StageNode& add_prev_stage() {
            prev_stages.insert(std::type_index(typeid(T)));
            return *this;
        }
        template <typename T>
        StageNode& add_next_stage() {
            next_stages.insert(std::type_index(typeid(T)));
            return *this;
        }
        EPIX_API void clear_tmp();
        EPIX_API size_t get_depth();
    };

    template <typename SrcT, typename DstT, typename StageT, typename... Subs>
    StageNode& assign_startup_stage(StageT stage, Subs... sub_stages) {
        auto& src   = m_sub_apps->at(std::type_index(typeid(SrcT)));
        auto& dst   = m_sub_apps->at(std::type_index(typeid(DstT)));
        auto runner = std::make_unique<StageRunner>(
            std::type_index(typeid(StageT)), src.get(), dst.get(),
            m_pools.get(), m_sets.get()
        );
        runner->configure_sub_stage(stage, sub_stages...);
        auto node = std::make_shared<StageNode>(
            std::type_index(typeid(StageT)), std::move(runner)
        );
        m_startup_stages.emplace(std::type_index(typeid(StageT)), node);
        return *node.get();
    }
    template <typename SrcT, typename DstT, typename StageT, typename... Subs>
    StageNode& assign_loop_stage(StageT stage, Subs... sub_stages) {
        auto& src   = m_sub_apps->at(std::type_index(typeid(SrcT)));
        auto& dst   = m_sub_apps->at(std::type_index(typeid(DstT)));
        auto runner = std::make_unique<StageRunner>(
            std::type_index(typeid(StageT)), src.get(), dst.get(),
            m_pools.get(), m_sets.get()
        );
        runner->configure_sub_stage(stage, sub_stages...);
        auto node = std::make_shared<StageNode>(
            std::type_index(typeid(StageT)), std::move(runner)
        );
        m_loop_stages.emplace(std::type_index(typeid(StageT)), node);
        return *node.get();
    }
    template <typename SrcT, typename DstT, typename StageT, typename... Subs>
    StageNode& assign_state_transition_stage(StageT stage, Subs... sub_stages) {
        auto& src   = m_sub_apps->at(std::type_index(typeid(SrcT)));
        auto& dst   = m_sub_apps->at(std::type_index(typeid(DstT)));
        auto runner = std::make_unique<StageRunner>(
            std::type_index(typeid(StageT)), src.get(), dst.get(),
            m_pools.get(), m_sets.get()
        );
        runner->configure_sub_stage(stage, sub_stages...);
        auto node = std::make_shared<StageNode>(
            std::type_index(typeid(StageT)), std::move(runner)
        );
        m_state_transition_stages.emplace(
            std::type_index(typeid(StageT)), node
        );
        return *node.get();
    }
    template <typename SrcT, typename DstT, typename StageT, typename... Subs>
    StageNode& assign_exit_stage(StageT stage, Subs... sub_stages) {
        auto& src   = m_sub_apps->at(std::type_index(typeid(SrcT)));
        auto& dst   = m_sub_apps->at(std::type_index(typeid(DstT)));
        auto runner = std::make_unique<StageRunner>(
            std::type_index(typeid(StageT)), src.get(), dst.get(),
            m_pools.get(), m_sets.get()
        );
        runner->configure_sub_stage(stage, sub_stages...);
        auto node = std::make_shared<StageNode>(
            std::type_index(typeid(StageT)), std::move(runner)
        );
        m_exit_stages.emplace(std::type_index(typeid(StageT)), node);
        return *node.get();
    }

    template <typename StageT>
    bool stage_startup() {
        return m_startup_stages.find(std::type_index(typeid(StageT))) !=
               m_startup_stages.end();
    }
    EPIX_API bool stage_startup(std::type_index stage);
    template <typename StageT>
    bool stage_loop() {
        return m_loop_stages.find(std::type_index(typeid(StageT))) !=
               m_loop_stages.end();
    }
    EPIX_API bool stage_loop(std::type_index stage);
    template <typename StageT>
    bool stage_state_transition() {
        return m_state_transition_stages.find(std::type_index(typeid(StageT))
               ) != m_state_transition_stages.end();
    }
    EPIX_API bool stage_state_transition(std::type_index stage);
    template <typename StageT>
    bool stage_exit() {
        return m_exit_stages.find(std::type_index(typeid(StageT))) !=
               m_exit_stages.end();
    }
    EPIX_API bool stage_exit(std::type_index stage);

    template <typename StageT>
    bool stage_present() {
        return stage_startup<StageT>() || stage_loop<StageT>() ||
               stage_state_transition<StageT>() || stage_exit<StageT>();
    }
    EPIX_API bool stage_present(std::type_index stage);

    template <typename SetT, typename... Sets>
    void configure_sets(SetT set, Sets... sets) {
        m_sets->emplace(
            std::type_index(typeid(SetT)), std::vector<SystemSet>{set, sets...}
        );
    }

    template <typename StageT>
    Runner& add_system(StageT stage, SystemAddInfo&& systems) {
        if (auto it =
                m_state_transition_stages.find(std::type_index(typeid(StageT)));
            it != m_state_transition_stages.end()) {
            it->second->runner->add_system(stage, std::move(systems));
            return *this;
        } else if (systems.is_state_transition) {
            m_logger->warn(
                "Trying to add systems with state transition requirements to "
                "non-state-transition stages."
            );
        }
        if (auto it = m_startup_stages.find(std::type_index(typeid(StageT)));
            it != m_startup_stages.end()) {
            it->second->runner->add_system(stage, std::move(systems));
            return *this;
        }
        if (auto it = m_loop_stages.find(std::type_index(typeid(StageT)));
            it != m_loop_stages.end()) {
            it->second->runner->add_system(stage, std::move(systems));
            return *this;
        }
        if (auto it = m_exit_stages.find(std::type_index(typeid(StageT)));
            it != m_exit_stages.end()) {
            it->second->runner->add_system(stage, std::move(systems));
            return *this;
        }
        spdlog::warn("Stage {} not found", typeid(StageT).name());
        return *this;
    }

    EPIX_API void build();
    EPIX_API void bake_all();
    EPIX_API void run(std::shared_ptr<StageNode> node);
    EPIX_API void run_startup();
    EPIX_API void bake_loop();
    EPIX_API void run_loop();
    EPIX_API void bake_state_transition();
    EPIX_API void run_state_transition();
    EPIX_API void run_exit();
    EPIX_API void tick_events();
    EPIX_API void end_commands();
    EPIX_API void update_states();
    EPIX_API void add_worker(const std::string& name, uint32_t num_threads);
    EPIX_API void set_log_level(spdlog::level::level_enum level);

   protected:
    MsgQueueBase<std::shared_ptr<StageNode>> msg_queue;
    entt::dense_map<std::type_index, std::unique_ptr<SubApp>>* m_sub_apps;
    entt::dense_map<std::type_index, std::shared_ptr<StageNode>>
        m_startup_stages;
    entt::dense_map<std::type_index, std::shared_ptr<StageNode>> m_loop_stages;
    entt::dense_map<std::type_index, std::shared_ptr<StageNode>>
        m_state_transition_stages;
    entt::dense_map<std::type_index, std::shared_ptr<StageNode>> m_exit_stages;
    std::unique_ptr<WorkerPool> m_pools;
    std::unique_ptr<thread_pool> m_control_pool;
    std::unique_ptr<SetMap> m_sets;

    std::shared_ptr<spdlog::logger> m_logger;
};
struct AppExit {};
struct MainSubApp {};
struct RenderSubApp {};
struct Plugin {
    virtual void build(App&) = 0;
};
struct AppSettings : public Plugin {
    bool parrallel_rendering = false;
    void build(App& app) override {}
};
struct App {
    EPIX_API static App create();
    EPIX_API static App create2();
    EPIX_API static App create(const AppSettings& settings);

    template <typename StageT>
    App& add_system(StageT stage, SystemAddInfo&& systems) {
        m_runner->add_system(stage, std::move(systems));
        return *this;
    }
    template <typename StageT, typename... Funcs>
        requires(
            std::convertible_to<
                std::remove_cvref_t<decltype(into(std::declval<Funcs>()))>,
                SystemAddInfo> &&
            ...
        )
    App& add_system(StageT stage, Funcs&&... systems) {
        (m_runner->add_system(stage, std::move(into(systems))), ...);
        return *this;
    }
    template <typename T>
    App& add_plugin(T&& plugin) {
        if (m_plugin_types.find(
                std::type_index(typeid(std::remove_reference_t<T>))
            ) != m_plugin_types.end()) {
            spdlog::warn(
                "Plugin {} already exists, skipping",
                typeid(std::remove_reference_t<T>).name()
            );
            return *this;
        }
        m_plugins.emplace_back(
            std::type_index(typeid(std::remove_reference_t<T>)),
            std::make_shared<std::remove_reference_t<T>>(std::forward<T>(plugin)
            )
        );
        return *this;
    }
    template <typename T>
    std::shared_ptr<T> get_plugin() {
        auto&& iter = std::find_if(
            m_plugins.begin(), m_plugins.end(),
            [](const auto& pair) {
                return pair.first == std::type_index(typeid(T));
            }
        );
        if (iter != m_plugins.end()) {
            return std::static_pointer_cast<T>(iter->second);
        }
        return nullptr;
    }
    template <typename T, typename Target = MainSubApp, typename... Args>
    App& emplace_resource(Args&&... args) {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add resource to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->emplace_resource<T>(std::forward<Args>(args)...);
        return *this;
    }
    template <typename Target = MainSubApp, typename T>
    App& insert_resource(T&& res) {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add resource to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->insert_resource(std::forward<T>(res)...);
        return *this;
    }
    template <typename T, typename Target = MainSubApp>
    App& init_resource() {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add resource to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->init_resource<T>();
        return *this;
    }
    template <typename T, typename Target = MainSubApp>
    App& insert_state(T&& state) {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add state to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->insert_state(std::forward<T>(state));
        return *this;
    }
    template <typename T, typename Target = MainSubApp>
    App& init_state() {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add state to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->init_state<T>();
        return *this;
    }
    template <typename EventT, typename Target = MainSubApp>
    App& add_event() {
        auto it = m_sub_apps->find(std::type_index(typeid(Target)));
        if (it == m_sub_apps->end()) {
            spdlog::warn(
                "Add state to sub app: {}, which does not exist.",
                typeid(Target).name()
            );
            return *this;
        }
        auto& target = it->second;
        target->add_event<EventT>();
        return *this;
    }
    template <typename... Sets>
    App& configure_sets(Sets... sets) {
        m_runner->configure_sets(sets...);
        return *this;
    }

    EPIX_API void run();
    EPIX_API void set_log_level(spdlog::level::level_enum level);
    EPIX_API App& enable_loop();
    EPIX_API App& disable_loop();
    EPIX_API App* operator->();
    template <typename T>
    bool has_sub_app() {
        return m_sub_apps->find(std::type_index(typeid(T))) !=
               m_sub_apps->end();
    }

   protected:
    EPIX_API App();
    EPIX_API App(const App&)            = delete;
    EPIX_API App& operator=(const App&) = delete;
    EPIX_API App(App&&)                 = default;
    EPIX_API App& operator=(App&&)      = default;

    template <typename T>
    void add_sub_app() {
        if (has_sub_app<T>()) {
            spdlog::warn(
                "Sub app {} already exists, skipping", typeid(T).name()
            );
            return;
        }
        m_sub_apps->emplace(
            std::type_index(typeid(T)), std::make_unique<SubApp>()
        );
    }

    EPIX_API void build_plugins();
    EPIX_API void build();
    EPIX_API void end_commands();
    EPIX_API void tick_events();
    EPIX_API void update_states();
    EPIX_API Runner& runner();

    std::unique_ptr<entt::dense_map<std::type_index, std::unique_ptr<SubApp>>>
        m_sub_apps;
    std::unique_ptr<Runner> m_runner;
    std::vector<std::pair<std::type_index, std::shared_ptr<Plugin>>> m_plugins;
    entt::dense_set<std::type_index> m_plugin_types;
    std::unique_ptr<BasicSystem<bool>> m_check_exit_func;
    bool m_loop_enabled = false;
    AppSettings m_settings;

    std::shared_ptr<spdlog::logger> m_logger;
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

namespace epix {

// ENTITY PART
using app::App;
using app::Bundle;
using app::Children;
using app::Entity;
using app::Parent;
using app::Plugin;

// STAGES
using namespace app::stages;

// EVENTS
using app::AppExit;

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
using epix::app::bundle;
using epix::app::chain;
using epix::app::into;
using epix::app::thread_pool;
using epix::utility::time_scope;

}  // namespace epix
namespace epix::prelude {
using namespace epix;
}

#define __EPIX_STRINGIZE2(x) #x
#define __EPIX_STRINGIZE(x) __EPIX_STRINGIZE2(##x)
#define EPIX_CONCAT2(a, b) a##b
#define EPIX_CONCAT(a, b) EPIX_CONCAT2(a, b)
#define EPIX_SYSTEMT(type, sys_name, body)                                     \
    type fn_##sys_name##body;                                                  \
    constexpr auto EPIX_CONCAT(get_##sys_name, _name)() {                      \
        auto fn_name =                                                         \
            std::string_view(std::source_location::current().function_name()); \
        auto namespace_name = fn_name.substr(0, fn_name.rfind("::"));          \
        return std::string(namespace_name) + "::" + #sys_name;                 \
    }                                                                          \
    inline auto sys_name = epix::app::SystemT(                                 \
        EPIX_CONCAT(get_##sys_name, _name)(), fn_##sys_name                    \
    );
#define EPIX_SYSTEM(sys_name, body) EPIX_SYSTEMT(auto, ##sys_name, ##body)
#define EPIX_INTO(function) epix::app::into(##function, #function)

#ifndef into2
#define into2(x) EPIX_INTO(##x)
#endif