#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <concepts>
#include <epix/ecs.hpp>
#include <epix/meta.hpp>
#include <optional>
#include <type_traits>
#include <unordered_map>
#endif

#include <epix/assets/handle.hpp>

namespace epix::assets {

/** @brief Customization point mapping a component type to its referenced asset ID.
 *
 * Specializations provide an `Asset` type and
 * `static AssetId<Asset> as_asset_id(const T&)`.
 */
EPIX_EXPORT template <typename T>
struct AsAssetId;

/** @brief Contract implemented by valid `AsAssetId<T>` specializations. */
EPIX_EXPORT template <typename T>
concept AsAssetIdImpl = requires(const T& value) {
    typename AsAssetId<T>::Asset;
    requires assets::Asset<typename AsAssetId<T>::Asset>;
    { AsAssetId<T>::as_asset_id(value) } -> std::same_as<AssetId<typename AsAssetId<T>::Asset>>;
};

namespace detail {
/** @brief Per-asset change ticks consumed by `AssetChanged`.
 *
 * Bevy keeps this resource private and updates it only while flushing asset
 * events. Epix follows the same ownership rule; it is public only as a C++
 * template implementation detail.
 */
template <assets::Asset A>
struct AssetChanges {
    std::unordered_map<AssetId<A>, ecs::Tick> change_ticks;
    ecs::Tick last_change_tick{};

    void insert(AssetId<A> id, ecs::Tick tick) {
        last_change_tick = tick;
        change_ticks.insert_or_assign(id, tick);
    }

    void remove(const AssetId<A>& id) { change_ticks.erase(id); }
};
}  // namespace detail

/** @brief Query filter selecting components whose referenced asset changed.
 *
 * Match this together with `ecs::Modified<T>` when both a replaced component
 * and changes to the referenced asset must be observed, as Bevy does with
 * `Or<(AssetChanged<T>, Changed<T>)>`.
 */
EPIX_EXPORT template <AsAssetIdImpl T>
struct AssetChanged;

}  // namespace epix::assets

namespace epix::ecs {

template <assets::AsAssetIdImpl T>
struct WorldQuery<assets::AssetChanged<T>> {
    using Asset = typename assets::AsAssetId<T>::Asset;

    struct Fetch {
        WorldQuery<Ref<T>>::Fetch component;
        const assets::detail::AssetChanges<Asset>* changes = nullptr;
        Tick last_run;
        Tick this_run;
        bool has_updates = false;
    };

    struct State {
        TypeId component_id;
        TypeId changes_id;
    };

    static Fetch init_fetch(World& world, const State& state, Tick last_run, Tick this_run) {
        const auto changes = internal::get_entity_resource_ref<assets::detail::AssetChanges<Asset>>(
            world, state.changes_id, last_run, this_run);
        const auto* value = changes ? changes->ptr() : nullptr;
        if (value == nullptr) {
            spdlog::error(
                "AssetChanges<{}> resource was removed; do not remove it while using the AssetChanged<{}> query",
                meta::type_id<Asset>::short_name(), meta::type_id<T>::short_name());
        }
        return Fetch{
            .component   = WorldQuery<Ref<T>>::init_fetch(world, state.component_id, last_run, this_run),
            .changes     = value,
            .last_run    = last_run,
            .this_run    = this_run,
            .has_updates = value && value->last_change_tick.newer_than(last_run, this_run),
        };
    }

    static void set_archetype(Fetch& fetch, const State& state, const Archetype& archetype, const Table& table) {
        WorldQuery<Ref<T>>::set_archetype(fetch.component, state.component_id, archetype, table);
    }

    static void set_access(State&, const FilteredAccess&) noexcept {}

    static void update_access(const State& state, FilteredAccess& access) {
        WorldQuery<Ref<T>>::update_access(state.component_id, access);
        access.add_resource_read(state.changes_id);
    }

    static State init_state(World& world) {
        return State{
            .component_id = internal::world_registrator(world).register_component<T>(),
            .changes_id   = world.init_resource<assets::detail::AssetChanges<Asset>>(),
        };
    }

    static std::optional<State> get_state(const Components& components) {
        const auto component_id = components.get_id<T>();
        const auto changes_id   = components.get_id<assets::detail::AssetChanges<Asset>>();
        if (!component_id || !changes_id) return std::nullopt;
        return State{.component_id = *component_id, .changes_id = *changes_id};
    }

    static bool matches_component_set(const State& state, internal::contains_component_fn auto&& contains_component) {
        return contains_component(state.component_id);
    }
};

template <assets::AsAssetIdImpl T>
struct QueryFilter<assets::AssetChanged<T>> {
    constexpr static inline bool archetypal = false;

    static bool filter_fetch(WorldQuery<assets::AssetChanged<T>>::Fetch& fetch, Entity entity, TableRow row) {
        if (!fetch.has_updates || fetch.changes == nullptr) return false;
        const auto component = QueryData<Ref<T>>::fetch(fetch.component, entity, row);
        const auto changed   = fetch.changes->change_ticks.find(assets::AsAssetId<T>::as_asset_id(*component));
        return changed != fetch.changes->change_ticks.end() &&
               changed->second.newer_than(fetch.last_run, fetch.this_run);
    }
};

}  // namespace epix::ecs
