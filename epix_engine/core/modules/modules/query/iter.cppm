module;

export module epix.core:query.iter;

import std;

import :query.decl;
import :query.state;
import :query.fetch;
import :query.filter;
import :storage;
import :world.decl;

namespace core {
export template <query_data D, query_filter F>
struct QueryIterCursor {
   public:
    QueryIterCursor(World* world, const QueryState<D, F>* state, Tick last_run, Tick this_run)
        : archetype_ids(state->matched_archetype_ids()),
          archetype_entities(),
          fetch(WorldQuery<D>::init_fetch(*world, state->fetch_state(), last_run, this_run)),
          filter(WorldQuery<F>::init_fetch(*world, state->filter_state(), last_run, this_run)),
          current_idx(0) {}
    void to_end() {
        archetype_ids      = archetype_ids.subspan(archetype_ids.size());
        archetype_entities = {};
        current_idx        = 0;
    }
    void reset(const QueryState<D, F>* state) {
        archetype_ids      = state->matched_archetype_ids();
        archetype_entities = {};
        current_idx        = 0;
    }

    QueryData<D>::Item retrieve() {
        if (!current()) {
            throw std::out_of_range("QueryIterCursor::retrieve() called out of range");
        }
        auto entity = archetype_entities[current_idx].entity;
        auto row    = TableRow(archetype_entities[current_idx].table_idx);
        return QueryData<D>::fetch(fetch, entity, row);
    }
    bool current() const { return current_idx < archetype_entities.size(); }
    bool next(Tables& tables, const Archetypes& archetypes, const QueryState<D, F>& state) {
        while (true) {
            if (!archetype_ids.empty() && archetype_entities.data() == nullptr) {
                // first time initialization
                auto& archetype = archetypes.get(archetype_ids.front()).value().get();
                if (archetype.empty()) {
                    archetype_ids = archetype_ids.subspan(1);
                    continue;
                }
                archetype_entities = archetype.entities();
                auto& table        = tables.get_mut(archetype.table_id()).value().get();
                WorldQuery<D>::set_archetype(fetch, state.fetch_state(), archetype, table);
                WorldQuery<F>::set_archetype(filter, state.filter_state(), archetype, table);
                current_idx = 0;
            } else if (current_idx + 1 >= archetype_entities.size()) {
                // go to next archetype
                if (archetype_ids.size() > 0) archetype_ids = archetype_ids.subspan(1);
                if (archetype_ids.empty()) {
                    archetype_entities = {};
                    current_idx        = 0;  // reset to 0 for equality comparison at end.
                    return false;
                }

                auto& archetype = archetypes.get(archetype_ids.front()).value().get();
                if (archetype.empty()) continue;
                archetype_entities = archetype.entities();
                auto& table        = tables.get_mut(archetype.table_id()).value().get();
                WorldQuery<D>::set_archetype(fetch, state.fetch_state(), archetype, table);
                WorldQuery<F>::set_archetype(filter, state.filter_state(), archetype, table);
                current_idx = 0;
            } else {
                ++current_idx;
            }

            auto archetype_entity = archetype_entities[current_idx];
            if (!QueryFilter<F>::filter_fetch(filter, archetype_entity.entity, archetype_entity.table_idx)) {
                continue;
            }
            break;
        }
        return true;
    }
    bool end() const { return archetype_ids.empty() && !current(); }
    std::size_t max_remaining(const Archetypes& archetypes) const {
        return std::accumulate(
                   archetype_ids.begin(), archetype_ids.end(), std::size_t(0),
                   [&](std::size_t acc, ArchetypeId id) { return acc + archetypes.get(id).value().get().size(); }) -
               current_idx;
    }

    bool operator==(const QueryIterCursor& other) const {
        return archetype_ids.data() == other.archetype_ids.data() && current_idx == other.current_idx;
    }
    bool operator!=(const QueryIterCursor& other) const { return !(*this == other); }

   private:
    std::span<const ArchetypeId> archetype_ids;
    std::span<const ArchetypeEntity> archetype_entities;
    WorldQuery<D>::Fetch fetch;
    WorldQuery<F>::Fetch filter;
    std::size_t current_idx;  // index in current archetype_entities

    friend struct QueryIter<D, F>;
};
export template <query_data D, query_filter F>
struct QueryIter : std::ranges::view_interface<QueryIter<D, F>> {
   public:
    QueryIter(World* world, const QueryState<D, F>* state, Tick last_run, Tick this_run)
        : world(world), tables(&world_storage_mut(*world).tables), archetypes(&world_archetypes(*world)), state(state) {
        cursor.emplace(world, state, last_run, this_run);
    }
    static QueryIter create_begin(World* world, const QueryState<D, F>* state, Tick last_run, Tick this_run) {
        QueryIter iter(world, state, last_run, this_run);
        // the iter's cursor is not initialized to the first valid element when constructed
        iter.cursor->next(*iter.tables, *iter.archetypes, *iter.state);
        return iter;
    }
    static QueryIter create_end(World* world, const QueryState<D, F>* state) {
        QueryIter iter(world, state, Tick(0), Tick(0));
        iter.cursor->to_end();
        return iter;
    }

    //? Should make this a iterable, since it has the data needed to create new QueryIter for begin and end
    QueryIter begin() {
        auto copy_cursor = cursor.value();
        copy_cursor.reset(state);
        copy_cursor.next(*tables, *archetypes, *state);  // reset to first valid
        return QueryIter(world, state, copy_cursor);
    }
    QueryIter end() {
        auto copy_cursor = cursor.value();
        copy_cursor.to_end();
        return QueryIter(world, state, copy_cursor);
    }

    QueryData<D>::Item operator*() { return cursor->retrieve(); }
    bool next() { return cursor->next(*tables, *archetypes, *state); }
    bool current() const { return cursor->current(); }
    QueryIter& operator++() {
        cursor->next(*tables, *archetypes, *state);
        return *this;
    }
    QueryIter operator++(int) {
        QueryIter temp = *this;
        ++(*this);
        return temp;
    }
    bool operator==(const QueryIter& other) const {
        // tables and archetypes are always the same for the same world
        return world == other.world && state == other.state && cursor == other.cursor;
    }
    bool operator!=(const QueryIter& other) const { return !(*this == other); }

    QueryIter() = default;

   private:
    QueryIter(World* world, const QueryState<D, F>* state, QueryIterCursor<D, F> cursor)
        : world(world),
          tables(&world_storage_mut(*world).tables),
          archetypes(&world_archetypes(*world)),
          state(state),
          cursor(std::move(cursor)) {}
    World* world;
    Tables* tables;
    const Archetypes* archetypes;
    const QueryState<D, F>* state;
    std::optional<QueryIterCursor<D, F>> cursor;  // use optional to allow default construction
                                                  // this is needed for ranges view
};
}  // namespace core

export template <core::query_data D, core::query_filter F>
struct std::iterator_traits<core::QueryIter<D, F>> {
    using iterator_category = std::input_iterator_tag;
    using value_type        = core::QueryData<D>::Item;
    using difference_type   = std::ptrdiff_t;
};
template <core::query_data D, core::query_filter F>
constexpr bool ::std::ranges::enable_view<core::QueryIter<D, F>> = true;

static_assert(std::ranges::range<core::QueryIter<int&, core::Filter<>>>);
static_assert(std::ranges::view<core::QueryIter<int&, core::Filter<>>>);