#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <optional>
#include <utility>
#endif

#include <epix/ecs/query/access.hpp>
#include <epix/ecs/query/decl.hpp>
#include <epix/ecs/query/fetch.hpp>
#include <epix/ecs/query/filter.hpp>
#include <epix/ecs/query/iter.hpp>
#include <epix/ecs/query/state.hpp>
#include <epix/ecs/system/param.hpp>
#include <epix/ecs/world/detail/access.hpp>

namespace epix::ecs {
/** @brief High-level query handle providing iteration, single-entity lookup, and existence checks.
 *  @tparam D Query data descriptor.
 *  @tparam F Query filter. */
EPIX_EXPORT template <query_data D, query_filter F>
struct Query {
   public:
    Query(World& world, const QueryState<D, F>& state, Tick last_run, Tick this_run) noexcept
        : world_(&world), state_(&state), last_run_(last_run), this_run_(this_run) {}

    /** @brief Get a read-only version of this query. */
    Query<typename QueryData<D>::ReadOnly, F> as_readonly() const {
        return Query<typename QueryData<D>::ReadOnly, F>(
            *world_, state_->template as_readonly<typename QueryData<D>::ReadOnly>(), last_run_, this_run_);
    }

    /** @brief Create an iterator over all matching entities. */
    QueryIter<D, F> iter() const { return state_->create_iter(*world_, last_run_, this_run_); }

    /** @brief Fetch query data for a specific entity, if it matches. */
    typename internal::AddOptional<typename QueryData<D>::Item>::type get(Entity entity) {
        return internal::world_entities(*world_).get(entity).and_then(
            [this, entity](EntityLocation location) ->
            typename internal::AddOptional<typename QueryData<D>::Item>::type {
                if (!state_->contains_archetype(location.archetype_id)) return std::nullopt;
                auto& archetype = internal::world_archetypes(*world_).get(location.archetype_id).value().get();
                auto fetch      = WorldQuery<D>::init_fetch(*world_, state_->fetch_state(), last_run_, this_run_);
                auto filter     = WorldQuery<F>::init_fetch(*world_, state_->filter_state(), last_run_, this_run_);
                auto& table = internal::world_storage_mut(*world_).tables.get_mut(archetype.table_id()).value().get();

                WorldQuery<D>::set_archetype(fetch, state_->fetch_state(), archetype, table);
                WorldQuery<F>::set_archetype(filter, state_->filter_state(), archetype, table);
                if (!QueryFilter<F>::filter_fetch(filter, entity, location.table_idx)) return std::nullopt;
                return QueryData<D>::fetch(fetch, entity, location.table_idx);
            });
    }
    /** @brief Fetch read-only query data for a specific entity, if it matches. */
    typename internal::AddOptional<typename QueryData<typename QueryData<D>::ReadOnly>::Item>::type get_ro(
        Entity entity) const {
        return as_readonly().get(entity);
    }

    /** @brief Get the first matching entity's data, or std::nullopt if no match. */
    typename internal::AddOptional<typename QueryData<D>::Item>::type single() {
        QueryIter<D, F> iter = this->iter();
        bool has_value       = iter.next();
        if (!has_value) return std::nullopt;
        return *iter;
    }
    /** @brief Get the first matching entity's read-only data, or std::nullopt. */
    typename internal::AddOptional<typename QueryData<typename QueryData<D>::ReadOnly>::Item>::type single_ro() const {
        return as_readonly().single();
    }

    /** @brief Check if a specific entity matches this query's filters. */
    bool contains(Entity entity) const {
        return internal::world_entities(*world_)
            .get(entity)
            .transform([this, entity](EntityLocation location) -> bool {
                if (!state_->contains_archetype(location.archetype_id)) return false;
                auto& archetype = internal::world_archetypes(*world_).get(location.archetype_id).value().get();
                auto filter     = WorldQuery<F>::init_fetch(*world_, state_->filter_state(), last_run_, this_run_);
                auto& table = internal::world_storage_mut(*world_).tables.get_mut(archetype.table_id()).value().get();

                WorldQuery<F>::set_archetype(filter, state_->filter_state(), archetype, table);
                return QueryFilter<F>::filter_fetch(filter, entity, location.table_idx);
            })
            .value_or(false);
    }
    /** @brief Check whether the query has no matching entities. */
    bool empty() const { return !iter().next(); }

   private:
    World* world_;
    const QueryState<D, F>* state_;
    Tick last_run_;
    Tick this_run_;
};

/** @brief Wrapper for a query that expects exactly one matching entity.
 *  @tparam D Query data descriptor.
 *  @tparam F Query filter. */
EPIX_EXPORT template <query_data D, query_filter F>
struct Single {
   public:
    Single(QueryData<D>::Item item) noexcept : _item(std::move(item)) {}
    operator typename QueryData<D>::Item&() noexcept { return _item; }
    QueryData<D>::Item& get() noexcept { return _item; }
    auto operator->() noexcept { return &_item; }  // use auto here to avoid error when Item is a reference
    QueryData<D>::Item& operator*() noexcept { return _item; }

   private:
    typename QueryData<D>::Item _item;
};

}  // namespace epix::ecs

#include <epix/ecs/query/system_param.hpp>
