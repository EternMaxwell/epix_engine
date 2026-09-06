#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/common.hpp>
#include <iterator>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#endif

#include <epix/ecs/query/decl.hpp>
#include <epix/ecs/query/fetch.hpp>
#include <epix/ecs/query/filter.hpp>
#include <epix/ecs/query/state.hpp>
#include <epix/ecs/storage.hpp>
#include <epix/ecs/world/detail/access.hpp>

namespace epix::ecs {
/** @brief Input iterator over query results across matched archetypes.
 *  @tparam D Query data descriptor.
 *  @tparam F Query filter. */
EPIX_EXPORT template <query_data D, query_filter F>
struct QueryIterCursor {
   public:
    using iterator_concept  = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type        = std::remove_cvref_t<typename QueryData<D>::Item>;
    using difference_type   = std::ptrdiff_t;

    /** @brief Fetch the current query item. */
    QueryData<D>::Item operator*() const {
        if (!current()) {
            throw std::out_of_range("QueryIterCursor::operator*() called at the end");
        }
        const auto entity = archetype_entities[current_idx].entity;
        const auto row    = TableRow(archetype_entities[current_idx].table_idx);
        return QueryData<D>::fetch(fetch, entity, row);
    }

    /** @brief Advance to the next matching entity. */
    QueryIterCursor& operator++() {
        advance();
        return *this;
    }
    void operator++(int) { advance(); }

    /** @brief Compare this iterator with its lightweight end sentinel. */
    friend bool operator==(const QueryIterCursor& iter, std::default_sentinel_t) noexcept { return iter.at_end(); }
    friend bool operator==(std::default_sentinel_t sentinel, const QueryIterCursor& iter) noexcept {
        return iter == sentinel;
    }

    /** @brief Check whether the cursor points to a valid element. */
    bool current() const noexcept { return current_idx < archetype_entities.size(); }
    /** @brief Get an upper bound on remaining elements, including the current item. */
    std::size_t max_remaining() const {
        return std::accumulate(archetype_ids.begin(), archetype_ids.end(), std::size_t(0),
                               [&](std::size_t count, ArchetypeId id) {
                                   return count + archetypes->get(id).value().get().size();
                               }) -
               current_idx;
    }

   private:
    QueryIterCursor(World* world,
                    Tables* tables,
                    const Archetypes* archetypes,
                    const QueryState<D, F>* state,
                    Tick last_run,
                    Tick this_run)
        : archetype_ids(state->matched_archetype_ids()),
          archetype_entities(),
          fetch(WorldQuery<D>::init_fetch(*world, state->fetch_state(), last_run, this_run)),
          filter(WorldQuery<F>::init_fetch(*world, state->filter_state(), last_run, this_run)),
          current_idx(0),
          tables(tables),
          archetypes(archetypes),
          state(state) {
        advance();
    }

    bool advance() {
        while (true) {
            if (!archetype_ids.empty() && archetype_entities.data() == nullptr) {
                const auto& archetype = archetypes->get(archetype_ids.front()).value().get();
                if (archetype.empty()) {
                    archetype_ids = archetype_ids.subspan(1);
                    continue;
                }
                archetype_entities = archetype.entities();
                auto& table        = tables->get_mut(archetype.table_id()).value().get();
                WorldQuery<D>::set_archetype(fetch, state->fetch_state(), archetype, table);
                if constexpr (!QueryFilter<F>::archetypal) {
                    WorldQuery<F>::set_archetype(filter, state->filter_state(), archetype, table);
                }
                current_idx = 0;
            } else if (current_idx + 1 >= archetype_entities.size()) {
                if (!archetype_ids.empty()) archetype_ids = archetype_ids.subspan(1);
                if (archetype_ids.empty()) {
                    archetype_entities = {};
                    current_idx        = 0;
                    return false;
                }

                const auto& archetype = archetypes->get(archetype_ids.front()).value().get();
                if (archetype.empty()) continue;
                archetype_entities = archetype.entities();
                auto& table        = tables->get_mut(archetype.table_id()).value().get();
                WorldQuery<D>::set_archetype(fetch, state->fetch_state(), archetype, table);
                if constexpr (!QueryFilter<F>::archetypal) {
                    WorldQuery<F>::set_archetype(filter, state->filter_state(), archetype, table);
                }
                current_idx = 0;
            } else {
                ++current_idx;
            }

            if constexpr (!QueryFilter<F>::archetypal) {
                const auto archetype_entity = archetype_entities[current_idx];
                if (!QueryFilter<F>::filter_fetch(filter, archetype_entity.entity, archetype_entity.table_idx)) {
                    continue;
                }
            }
            return true;
        }
    }
    bool at_end() const noexcept { return archetype_ids.empty() && !current(); }

    std::span<const ArchetypeId> archetype_ids;
    std::span<const internal::ArchetypeEntity> archetype_entities;
    mutable WorldQuery<D>::Fetch fetch;
    WorldQuery<F>::Fetch filter;
    std::size_t current_idx;
    Tables* tables;
    const Archetypes* archetypes;
    const QueryState<D, F>* state;

    friend struct QueryIter<D, F>;
};

/** @brief Non-owning ranges view over query results.
 *
 *  `begin()` creates an independent input cursor and `end()` is a lightweight
 *  sentinel. Archetypal filters are resolved entirely by `QueryState`, so their
 *  cursors omit per-entity filter evaluation and the view is a sized range.
 *  @tparam D Query data descriptor.
 *  @tparam F Query filter. */
EPIX_EXPORT template <query_data D, query_filter F>
struct QueryIter : std::ranges::view_interface<QueryIter<D, F>> {
   public:
    using iterator = QueryIterCursor<D, F>;
    using sentinel = std::default_sentinel_t;

    /** @brief Construct a query-result view from world, state, and tick range. */
    QueryIter(World* world, const QueryState<D, F>* state, Tick last_run, Tick this_run)
        : world_(world),
          tables_(&internal::world_storage_mut(*world).tables),
          archetypes_(&internal::world_archetypes(*world)),
          state_(state),
          last_run_(last_run),
          this_run_(this_run) {}

    /** @brief Create a cursor positioned at the first matching item. */
    iterator begin() { return iterator(world_, tables_, archetypes_, state_, last_run_, this_run_); }
    iterator begin() const
        requires readonly_query_data<D>
    {
        return iterator(world_, tables_, archetypes_, state_, last_run_, this_run_);
    }
    /** @brief Return the lightweight end sentinel. */
    sentinel end() const noexcept { return {}; }

    /** @brief Return an upper bound on the number of produced items. */
    std::size_t max_remaining() const {
        return std::accumulate(
            state_->matched_archetype_ids().begin(), state_->matched_archetype_ids().end(), std::size_t(0),
            [&](std::size_t count, ArchetypeId id) { return count + archetypes_->get(id).value().get().size(); });
    }

    /** @brief Return the exact number of items for an archetypal query. */
    std::size_t size() const
        requires(QueryFilter<F>::archetypal)
    {
        return max_remaining();
    }

   private:
    World* world_;
    Tables* tables_;
    const Archetypes* archetypes_;
    const QueryState<D, F>* state_;
    Tick last_run_;
    Tick this_run_;
};
}  // namespace epix::ecs

template <epix::ecs::query_data D, epix::ecs::query_filter F>
constexpr bool ::std::ranges::enable_view<epix::ecs::QueryIter<D, F>> = true;

template <epix::ecs::query_data D, epix::ecs::query_filter F>
constexpr bool ::std::ranges::enable_borrowed_range<epix::ecs::QueryIter<D, F>> = true;
