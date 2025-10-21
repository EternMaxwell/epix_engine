#pragma once

#include <algorithm>
#include <cstddef>
#include <expected>
#include <ranges>
#include <utility>

#include "../system/system.hpp"
#include "system_dispatcher.hpp"
#include "system_set.hpp"

namespace epix::core::schedule {
struct Node;
struct Edges {
    std::unordered_set<SystemSetLabel> depends;
    std::unordered_set<SystemSetLabel> successors;
    std::unordered_set<SystemSetLabel> parents;
    std::unordered_set<SystemSetLabel> children;
};
struct Node {
    Node(const SystemSetLabel& label) : label(label) {}

    SystemSetLabel label;
    system::SystemUnique<> system;
    query::FilteredAccessSet system_access;
    std::vector<system::SystemUnique<std::tuple<>, bool>> conditions;
    std::vector<query::FilteredAccessSet> condition_access;

    Edges edges;
    Edges validated_edges;
};
struct CachedNode {
    std::shared_ptr<Node> node;  // stores shared_ptr so you can safely add, remove nodes while executing
    std::vector<size_t> depends;
    std::vector<size_t> successors;
    std::vector<size_t> parents;
    std::vector<size_t> children;
};
struct ScheduleCache {
    std::vector<CachedNode> nodes;
    std::unordered_map<SystemSetLabel, size_t> node_map;
};
struct ExecutionState {
    size_t running_count   = 0;
    size_t remaining_count = 0;
    storage::bit_vector ready_nodes;
    storage::bit_vector running_nodes;
    storage::bit_vector finished_nodes;
    storage::bit_vector entered_nodes;
    storage::bit_vector condition_met_nodes;
    std::vector<storage::bit_vector> untest_conditions;
    std::vector<size_t> wait_count;  // number of dependencies + parents not yet satisfied
    std::vector<size_t> child_count;
    async_queue finished_queue;
    std::vector<size_t> ready_stack;
};
struct SchedulePrepareError {
    // labels involved in the error, 0 will be the same child if type is ParentsWithDeps
    std::vector<SystemSetLabel> associated_labels;
    // for ParentsWithDeps, the parents the have dependencies
    std::vector<SystemSetLabel> conflict_parents;
    enum class Type {
        CyclicDependency,
        CyclicHierarchy,
        ParentsWithDeps,
    } type;
};
struct SetConfig {
   public:
    SetConfig() = default;

    SetConfig& after(const SystemSetLabel& label) {
        edges.depends.insert(label);
        std::ranges::for_each(sub_configs, [&](SetConfig& config) { config.after(label); });
        return *this;
    }
    SetConfig& before(const SystemSetLabel& label) {
        edges.successors.insert(label);
        std::ranges::for_each(sub_configs, [&](SetConfig& config) { config.before(label); });
        return *this;
    }
    SetConfig& in_set(const SystemSetLabel& label) {
        edges.parents.insert(label);
        std::ranges::for_each(sub_configs, [&](SetConfig& config) { config.in_set(label); });
        return *this;
    }
    SetConfig& set_name(std::string name) {
        if (system) system->set_name(name);
        size_t index = 0;
        std::ranges::for_each(sub_configs,
                              [&](SetConfig& config) { config.set_name(std::format("{}#{}", name, index)); });
        return *this;
    }
    template <typename F>
    SetConfig& run_if(F&& func)
        requires(requires { system::make_system<std::tuple<>, bool>(std::forward<F>(func)); })
    {
        conditions.push_back(system::make_system<std::tuple<>, bool>(std::forward<F>(func)));
        std::ranges::for_each(sub_configs, [&](SetConfig& config) { config.run_if(std::forward<F>(func)); });
        return *this;
    }
    SetConfig& chain() {
        for (auto&& [c1, c2] : sub_configs | std::views::adjacent<2>) {
            c1.before(c2.label);
            std::ranges::for_each(c2.sub_configs, [&](SetConfig& config) { c1.before(config.label); });
        }
        return *this;
    }

   private:
    std::optional<SystemSetLabel> label;
    system::SystemUnique<> system;
    std::vector<system::SystemUnique<std::tuple<>, bool>> conditions;
    Edges edges;

    std::vector<SetConfig> sub_configs;

    friend struct Schedule;
    template <bool require_system, typename F>
    friend SetConfig single_set(F&& func)
        requires(!require_system || requires {
            { system::make_system(std::forward<F>(func)) } -> std::same_as<system::System<std::tuple<>, void>*>;
        } || std::same_as<std::decay_t<F>, SetConfig>);
    template <bool, typename... Ts>
    friend SetConfig make_sets(Ts&&... ts);
};
template <bool require_system = false, typename F>
SetConfig single_set(F&& func)
    requires(!require_system ||
             requires {
                 { system::make_system(std::forward<F>(func)) } -> std::same_as<system::System<std::tuple<>, void>*>;
             } || std::same_as<std::decay_t<F>, SetConfig>)
{
    if constexpr (std::same_as<std::decay_t<F>, SetConfig>) {
        return std::forward<F>(func);
    }
    SetConfig config;
    config.label.emplace(func);
    if constexpr (requires {
                      {
                          system::make_system(std::forward<F>(func))
                      } -> std::same_as<system::System<std::tuple<>, void>*>;
                  }) {
        config.system.reset(system::make_system(std::forward<F>(func)));
    }
    return config;
}
template <bool require_system = false, typename... Ts>
SetConfig make_sets(Ts&&... ts) {
    if constexpr (sizeof...(Ts) == 1) {
        return single_set<require_system>(std::get<0>(std::forward_as_tuple(std::forward<Ts>(ts)...)));
    } else {
        SetConfig config;
        config.sub_configs.reserve(sizeof...(Ts));
        (config.sub_configs.push_back(make_sets<require_system>(std::forward<Ts>(ts))), ...);
        return config;
    }
}
struct ExecuteConfig {
    bool apply_direct   = false;  // should call System::apply right after System::run or at the end of the schedule
    bool queue_deferred = false;  // call System::queue for deferred systems instead of apply at the end

    bool is_apply_direct() const { return apply_direct && !queue_deferred; }
    bool is_queue_deferred() const { return queue_deferred; }
    bool is_apply_end() const { return !apply_direct && !queue_deferred; }
};
struct Schedule {
   private:
    std::unordered_map<SystemSetLabel, std::shared_ptr<Node>> nodes;
    std::shared_ptr<ScheduleCache> cache;

    void add_config(SetConfig config, bool accept_system = true) {
        // create node
        if (config.label) {
            auto node = std::make_shared<Node>(*config.label);
            if (accept_system && config.system) node->system = std::move(config.system);
            node->conditions = std::move(config.conditions);
            node->edges      = std::move(config.edges);
            nodes.emplace(node->label, node);
        }
        std::ranges::for_each(config.sub_configs,
                              [&](SetConfig& sub_config) { add_config(std::move(sub_config), accept_system); });
        cache.reset();
    }

   public:
    Schedule() = default;

    void add_systems(SetConfig&& config) { add_config(std::move(config), true); }
    void configure_sets(SetConfig&& config) { add_config(std::move(config), false); }
    void add_systems(SetConfig& config) { add_config(std::move(config), true); }
    void configure_sets(SetConfig& config) { add_config(std::move(config), false); }

    // prepare the schedule (validate and build cache), if check_error is true, will check for errors
    // otherwise the error will cause skipped nodes during execution
    std::expected<void, SchedulePrepareError> prepare(bool check_error = true);
    void initialize_systems(World& world, bool force = false);
    void execute(SystemDispatcher& dispatcher, const ExecuteConfig& config = {});
};
}  // namespace epix::core::schedule

namespace epix {
template <typename... Ts>
core::schedule::SetConfig into(Ts&&... ts) {
    return core::schedule::make_sets<true>(std::forward<Ts>(ts)...);
}
template <typename... Ts>
core::schedule::SetConfig sets(Ts&&... ts) {
    return core::schedule::make_sets(std::forward<Ts>(ts)...);
}
}  // namespace epix