#pragma once

// Query parameter adapters are included after Query and Single are defined by
// epix/ecs/query.hpp. Keeping them here prevents the generic system-parameter
// framework from owning query-specific access and validation policy.
namespace epix::ecs {

template <query_data D, query_filter F>
struct SystemParam<Query<D, F>> : ParamBase {
    using State                    = QueryState<D, F>;
    using Item                     = Query<D, F>;
    static constexpr bool readonly = readonly_query_data<D>;

    static State init_state(World& world) { return QueryState<D, F>::create(world); }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        AccessConflicts conflicts = access.get_conflicts(state.component_access());
        if (!conflicts.empty()) {
            throw std::runtime_error(std::format(
                "Query<{}, {}> in system [{}] has access conflicts with previous params, with conflicts on ids: {}.",
                meta::type_id<D>().name(), meta::type_id<F>().name(), meta.name, conflicts.to_string()));
        }
        access.add(state.component_access());
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return state.query_with_ticks(world, meta.last_run, tick);
    }
};

template <query_data D, query_filter F>
struct SystemParam<Single<D, F>> : SystemParam<Query<D, F>> {
    using Base                     = SystemParam<Query<D, F>>;
    using State                    = typename Base::State;
    using Item                     = Single<D, F>;
    static constexpr bool readonly = Base::readonly;

    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return Single<D, F>(Base::get_param(state, meta, world, tick).single().value());
    }
    static std::expected<void, ValidateParamError> validate_param(const State& state,
                                                                  const SystemMeta& meta,
                                                                  World& world) {
        Query<D, F> query = Base::get_param(const_cast<State&>(state), meta, world, internal::world_change_tick(world));
        if (!query.single().has_value()) {
            return std::unexpected(ValidateParamError{
                .param_type = meta::type_id<Single<D, F>>(),
                .message    = "Associated Query for Single system param is empty.",
            });
        }
        return {};
    }
};

}  // namespace epix::ecs
