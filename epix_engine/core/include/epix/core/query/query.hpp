#pragma once

#include "fetch.hpp"
#include "filter.hpp"
#include "fwd.hpp"
#include "iter.hpp"
#include "state.hpp"

namespace epix::core::query {
template <typename D, typename F>
    requires(valid_query_data<QueryData<D>> && valid_query_filter<QueryFilter<F>>)
struct Query {
   public:
    Query(World& world, const QueryState<D, F>& state, Tick last_run, Tick this_run)
        : world_(&world), state_(&state), last_run_(last_run), this_run_(this_run) {}

    Query<typename QueryData<D>::ReadOnly, F> as_readonly() const {
        return Query<typename QueryData<D>::ReadOnly, F>(
            *world_, state_->template as_readonly<typename QueryData<D>::ReadOnly>(), last_run_, this_run_);
    }

    QueryIter<D, F> iter() const { return state_->create_iter(*world_, last_run_, this_run_); }

    typename AddOptional<typename QueryData<D>::Item>::type get(Entity entity) {
        return world_->entities().get(entity).and_then(
            [this, entity](EntityLocation location) -> typename AddOptional<typename QueryData<D>::Item>::type {
                if (!state_->contains_archetype(location.archetype_id)) return std::nullopt;
                auto& archetype = world_->archetypes().get(location.archetype_id).value().get();
                auto fetch      = WorldQuery<D>::init_fetch(*world_, state_->fetch_state(), last_run_, this_run_);
                auto filter     = WorldQuery<F>::init_fetch(*world_, state_->filter_state(), last_run_, this_run_);
                auto& table     = world_->storage_mut().tables.get_mut(archetype.table_id()).value().get();

                WorldQuery<D>::set_archetype(fetch, state_->fetch_state(), archetype, table);
                WorldQuery<F>::set_archetype(filter, state_->filter_state(), archetype, table);
                if (!QueryFilter<F>::filter_fetch(filter, entity, location.table_idx)) return std::nullopt;
                return QueryData<D>::fetch(fetch, entity, location.table_idx);
            });
    }
    typename AddOptional<typename QueryData<typename QueryData<D>::ReadOnly>::Item>::type get_ro(Entity entity) const {
        return as_readonly().get(entity);
    }

    typename AddOptional<typename QueryData<D>::Item>::type single() {
        QueryIter<D, F> iter = this->iter();
        bool has_value       = iter.next();
        if (!has_value) return std::nullopt;
        return *iter;
    }
    typename AddOptional<typename QueryData<typename QueryData<D>::ReadOnly>::Item>::type single_ro() const {
        return as_readonly().single();
    }

    bool contains(Entity entity) const {
        return world_->entities()
            .get(entity)
            .transform([this, entity](EntityLocation location) -> bool {
                if (!state_->contains_archetype(location.archetype_id)) return false;
                auto& archetype = world_->archetypes().get(location.archetype_id).value().get();
                auto filter     = WorldQuery<F>::init_fetch(*world_, state_->filter_state(), last_run_, this_run_);
                auto& table     = world_->storage_mut().tables.get_mut(archetype.table_id()).value().get();

                WorldQuery<F>::set_archetype(filter, state_->filter_state(), archetype, table);
                return QueryFilter<F>::filter_fetch(filter, entity, location.table_idx);
            })
            .value_or(false);
    }
    bool empty() const { return !iter().next(); }

   private:
    World* world_;
    const QueryState<D, F>* state_;
    Tick last_run_;
    Tick this_run_;
};

template <typename D, typename F>
    requires(valid_query_data<QueryData<D>> && valid_query_filter<QueryFilter<F>>)
struct Single {
   public:
    Single(QueryData<D>::Item item) : _item(std::move(item)) {}
    operator typename QueryData<D>::Item &() { return _item; }
    QueryData<D>::Item& get() { return _item; }
    auto operator->() { return &_item; }  // use auto here to avoid error when Item is a reference
    QueryData<D>::Item& operator*() { return _item; }

   private:
    typename QueryData<D>::Item _item;
};
}  // namespace epix::core::query

namespace epix::core {
using query::Query;
using query::QueryState;
}  // namespace epix::core