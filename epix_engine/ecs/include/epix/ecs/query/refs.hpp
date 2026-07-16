#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <epix/common.hpp>
#include <limits>
#include <memory>
#include <type_traits>
#endif

#include <epix/ecs/query/fetch.hpp>
#include <epix/ecs/storage/resource.hpp>
#include <epix/ecs/system/param.hpp>
#include <epix/ecs/core/tick.hpp>
#include <epix/ecs/world/detail/access.hpp>

namespace epix::ecs {

/** @brief Immutable reference wrapper with change-detection tick metadata. */
EPIX_EXPORT template <internal::refable T>
struct Ref {
   private:
    const T* value;
    Ticks ticks;

   public:
    Ref(const T* value, Ticks ticks) noexcept : value(value), ticks(ticks) {}

    const T* ptr() const noexcept { return value; }
    const T& get() const noexcept { return *value; }
    const T* operator->() const noexcept { return value; }
    const T& operator*() const noexcept { return *value; }
    operator const T&() const noexcept { return *value; }
    bool is_added() const noexcept { return ticks.is_added(); }
    bool is_modified() const noexcept { return ticks.is_modified(); }
    Tick last_modified() const noexcept { return ticks.last_modified(); }
    Tick added_tick() const noexcept { return ticks.added_tick(); }
};
/** @brief Mutable reference wrapper with change-detection tick metadata. */
EPIX_EXPORT template <internal::refable T>
struct Mut {
   private:
    T* value;
    TicksMut ticks;

   public:
    Mut(T* value, TicksMut ticks) noexcept : value(value), ticks(ticks) {}

    const T* ptr() const noexcept { return value; }
    T* ptr_mut() noexcept {
        ticks.set_modified();
        return value;
    }
    const T& get() const noexcept { return *value; }
    T& get_mut() noexcept {
        ticks.set_modified();
        return *value;
    }
    const T* operator->() const noexcept { return value; }
    T* operator->() noexcept {
        ticks.set_modified();
        return value;
    }
    const T& operator*() const noexcept { return *value; }
    T& operator*() noexcept {
        ticks.set_modified();
        return *value;
    }
    operator T&() noexcept {
        ticks.set_modified();
        return *value;
    }
    operator const T&() const noexcept { return *value; }
    bool is_added() const noexcept { return ticks.is_added(); }
    bool is_modified() const noexcept { return ticks.is_modified(); }
    Tick last_modified() const noexcept { return ticks.last_modified(); }
    Tick added_tick() const noexcept { return ticks.added_tick(); }
    operator Ref<T>() const { return Ref<T>(value, ticks); }
};

/** @brief Immutable resource reference, extending Ref<T>. */
EPIX_EXPORT template <internal::refable T>
struct Res : public Ref<T> {
   public:
    using Ref<T>::Ref;
};
/** @brief Mutable resource reference, extending Mut<T>. */
EPIX_EXPORT template <internal::refable T>
struct ResMut : public Mut<T> {
   public:
    using Mut<T>::Mut;
};

// implements for Ref<T>
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<Ref<T>> {
    struct Fetch {
        union {
            const Dense* table_dense = nullptr;
            const ComponentSparseSet* sparse_set;
        };
        TypeId component_id;
        bool is_sparse_set = false;
        Tick last_run;
        Tick this_run;
    };
    using State = TypeId;
    static Fetch init_fetch(World& world, const State& state, Tick last_run, Tick this_run) {
        auto result = Fetch{.table_dense   = nullptr,
                            .component_id  = state,
                            .is_sparse_set = storage_type_of<T>() == StorageType::SparseSet,
                            .last_run      = last_run,
                            .this_run      = this_run};
        if (result.is_sparse_set) {
            result.sparse_set = internal::world_storage(world)
                                    .sparse_sets.get(state)
                                    .transform([](const ComponentSparseSet& ref) { return &ref; })
                                    .value_or(nullptr);
        }
        return result;
    }
    static void set_archetype(Fetch& fetch, const State& state, const Archetype& archetype, const Table& table) {
        if (storage_type_of<T>() == StorageType::Table) {
            fetch.table_dense   = &table.get_dense(state).value().get();
            fetch.is_sparse_set = false;
        }
    }
    // static void set_table(Fetch& fetch, State& state, const Table& table) {
    //     if (state.storage_type == StorageType::Table) {
    //         fetch.table_dense = &table.get_dense(state.component_id).value().get();
    //     }
    // }
    static void set_access(State& state, const FilteredAccess& access) {}
    static void update_access(const State& state, FilteredAccess& access) { access.add_component_read(state); }
    static State init_state(World& world) { return internal::world_registrator(world).register_component<T>(); }
    static std::optional<State> get_state(const Components& components) { return components.get_id<T>(); }
    static bool matches_component_set(const State& state, internal::contains_component_fn auto&& contains_component) {
        return contains_component(state);
    }
};
static_assert(internal::world_query<Ref<int>>);
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryData<Ref<T>> {
    using Item                            = Ref<T>;
    using ReadOnly                        = Ref<T>;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<Ref<T>>::Fetch& fetch, Entity entity, TableRow row) {
        if (fetch.is_sparse_set) {
            return fetch.sparse_set->template get_as<T>(entity)
                .transform([&](const T& value) {
                    return Ref<T>(&value, Ticks::from_refs(fetch.sparse_set->get_tick_refs(entity).value(),
                                                           fetch.last_run, fetch.this_run));
                })
                .value();
        } else {
            return fetch.table_dense->template get_as<T>(row)
                .transform([&](const T& value) {
                    return Ref<T>(&value, Ticks::from_refs(fetch.table_dense->get_tick_refs(row).value(),
                                                           fetch.last_run, fetch.this_run));
                })
                .value();
        }
    }
};
static_assert(query_data<Ref<int>>);

// implements for Mut<T>
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<Mut<T>> {
    struct Fetch {
        union {
            Dense* table_dense = nullptr;
            ComponentSparseSet* sparse_set;
        };
        TypeId component_id;
        bool is_sparse_set = false;
        Tick last_run;
        Tick this_run;
    };
    using State = TypeId;
    static Fetch init_fetch(World& world, const State& state, Tick last_run, Tick this_run) {
        auto result = Fetch{.table_dense   = nullptr,
                            .component_id  = state,
                            .is_sparse_set = storage_type_of<T>() == StorageType::SparseSet,
                            .last_run      = last_run,
                            .this_run      = this_run};
        if (result.is_sparse_set) {
            result.sparse_set = internal::world_storage_mut(world)
                                    .sparse_sets.get_mut(state)
                                    .transform([](ComponentSparseSet& ref) { return &ref; })
                                    .value_or(nullptr);
        }
        return result;
    }
    static void set_archetype(Fetch& fetch, const State& state, const Archetype& archetype, Table& table) {
        if (storage_type_of<T>() == StorageType::Table) {
            fetch.table_dense   = &table.get_dense_mut(state).value().get();
            fetch.is_sparse_set = false;
        }
    }
    // static void set_table(Fetch& fetch, State& state, const Table& table) {
    //     if (state.storage_type == StorageType::Table) {
    //         fetch.table_dense   = &table.get_dense_mut(state.component_id).value().get();
    //         fetch.is_sparse_set = false;
    //     }
    // }
    static void set_access(State& state, const FilteredAccess& access) {}
    static void update_access(const State& state, FilteredAccess& access) { access.add_component_write(state); }
    static State init_state(World& world) { return internal::world_registrator(world).register_component<T>(); }
    static std::optional<State> get_state(const Components& components) { return components.get_id<T>(); }
    static bool matches_component_set(const State& state, internal::contains_component_fn auto&& contains_component) {
        return contains_component(state);
    }
};
static_assert(internal::world_query<Mut<int>>);
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryData<Mut<T>> {
    using Item                            = Mut<T>;
    using ReadOnly                        = Ref<T>;
    static inline constexpr bool readonly = false;
    static Item fetch(WorldQuery<Mut<T>>::Fetch& fetch, Entity entity, TableRow row) {
        if (fetch.is_sparse_set) {
            return fetch.sparse_set->template get_as_mut<T>(entity)
                .transform([&](T& value) {
                    return Mut<T>(&value, TicksMut::from_refs(fetch.sparse_set->get_tick_refs(entity).value(),
                                                              fetch.last_run, fetch.this_run));
                })
                .value();
        } else {
            return fetch.table_dense->template get_as_mut<T>(row)
                .transform([&](T& value) {
                    return Mut<T>(&value, TicksMut::from_refs(fetch.table_dense->get_tick_refs(row).value(),
                                                              fetch.last_run, fetch.this_run));
                })
                .value();
        }
    }
};
static_assert(query_data<Mut<int>>);

// implements for const T&, in this case, WorldQuery<const T&> is the same as WorldQuery<Ref<T>>
template <typename T>
    requires(!std::is_reference_v<T>)
struct WorldQuery<const T&> : WorldQuery<Ref<std::remove_const_t<T>>> {};
static_assert(internal::world_query<const int&>);
template <typename T>
    requires(!std::is_reference_v<T>)
struct QueryData<const T&> : QueryData<Ref<std::remove_const_t<T>>> {
    using Item                            = const T&;
    using ReadOnly                        = const T&;
    static inline constexpr bool readonly = true;
    static Item fetch(WorldQuery<const T&>::Fetch& fetch, Entity entity, TableRow row) noexcept {
        return QueryData<Ref<std::remove_const_t<T>>>::fetch(fetch, entity, row).get();
    }
};
static_assert(query_data<const int&>);

// implements for T&, in this case, WorldQuery<T&> is the same as WorldQuery<Mut<T>>
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct WorldQuery<T&> : WorldQuery<Mut<T>> {};
static_assert(internal::world_query<int&>);
template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct QueryData<T&> : QueryData<Mut<T>> {
    using Item                            = T&;
    using ReadOnly                        = const T&;
    static inline constexpr bool readonly = false;
    static Item fetch(WorldQuery<T&>::Fetch& fetch, Entity entity, TableRow row) noexcept {
        return QueryData<Mut<T>>::fetch(fetch, entity, row).get_mut();
    }
};
static_assert(query_data<int&>);

// system param for res and mut

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct SystemParam<Res<T>> : ParamBase {
    using State                    = TypeId;
    using Item                     = Res<T>;
    static constexpr bool readonly = true;
    static State init_state(World& world) { return internal::world_registrator(world).register_resource<T>(); }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        if (access.combined_access().has_resource_write(state)) {
            throw std::runtime_error(
                std::format("Res<{}> in system [{}] has access conflicts of id {} with a previous ResMut<{}>. Consider "
                            "removing this param.",
                            meta::type_id<T>().name(), meta.name, state.get(), meta::type_id<T>().name()));
        }
        access.add_unfiltered_resource_read(state);
    }
    static std::expected<void, ValidateParamError> validate_param(State& state, const SystemMeta&, World& world) {
        return internal::world_storage(world)
            .resources.get(state)
            .transform([](const ResourceData& res) -> std::expected<void, ValidateParamError> {
                if (res.is_present()) return {};
                return std::unexpected(ValidateParamError{
                    .param_type = meta::type_id<Res<T>>(),
                    .message    = "Res storage exists, value not present.",
                });
            })
            .value_or(std::unexpected(ValidateParamError{
                .param_type = meta::type_id<Res<T>>(),
                .message    = "Res storage do not exists.",
            }));
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return internal::world_storage(world)
            .resources.get(state)
            .and_then([&](const ResourceData& res) {
                return res.get_as<T>().transform([&](const T& value) {
                    return Res<T>(std::addressof(value),
                                  Ticks::from_refs(res.get_tick_refs().value(), meta.last_run, tick));
                });
            })
            .value();
    }
};
static_assert(system_param<Res<int>>);

template <typename T>
    requires(!std::is_reference_v<T> && !std::is_const_v<T>)
struct SystemParam<ResMut<T>> : ParamBase {
    using State                    = TypeId;
    using Item                     = ResMut<T>;
    static constexpr bool readonly = false;
    static State init_state(World& world) { return internal::world_registrator(world).register_resource<T>(); }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        if (access.combined_access().has_resource_read(state)) {
            throw std::runtime_error(std::format(
                "ResMut<{}> in system [{}] has access conflicts of id {} with a previous Res<{}> or ResMut<{}>.",
                meta::type_id<T>().name(), meta.name, state.get(), meta::type_id<T>().name(),
                meta::type_id<T>().name()));
        }
        access.add_unfiltered_resource_write(state);
    }
    static std::expected<void, ValidateParamError> validate_param(State& state, const SystemMeta&, World& world) {
        return internal::world_storage(world)
            .resources.get(state)
            .transform([](const ResourceData& res) -> std::expected<void, ValidateParamError> {
                if (res.is_present()) return {};
                return std::unexpected(ValidateParamError{
                    .param_type = meta::type_id<Res<T>>(),
                    .message    = "ResMut storage exists, value not present.",
                });
            })
            .value_or(std::unexpected(ValidateParamError{
                .param_type = meta::type_id<Res<T>>(),
                .message    = "ResMut storage do not exists.",
            }));
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return internal::world_storage_mut(world)
            .resources.get_mut(state)
            .and_then([&](ResourceData& res) {
                return res.get_as_mut<T>().transform([&](T& value) {
                    return ResMut<T>(std::addressof(value),
                                     TicksMut::from_refs(res.get_tick_refs().value(), meta.last_run, tick));
                });
            })
            .value();
    }
};
static_assert(system_param<ResMut<int>>);
}  // namespace epix::ecs
