#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <expected>
#include <ranges>
#include <utility>

#include "../label.hpp"
#include "../system/system.hpp"
#include "system_dispatcher.hpp"
#include "system_set.hpp"

namespace epix::core::schedule {
EPIX_MAKE_LABEL(ScheduleLabel)
struct Node;
struct Edges {
    std::unordered_set<SystemSetLabel> depends;
    std::unordered_set<SystemSetLabel> successors;
    std::unordered_set<SystemSetLabel> parents;
    std::unordered_set<SystemSetLabel> children;

    void merge(Edges other) {
#ifdef __cpp_lib_containers_ranges
        depends.insert_range(std::move(other.depends));
        successors.insert_range(std::move(other.successors));
        parents.insert_range(std::move(other.parents));
        children.insert_range(std::move(other.children));
#else
        depends.insert(std::make_move_iterator(other.depends.begin()),
                      std::make_move_iterator(other.depends.end()));
        successors.insert(std::make_move_iterator(other.successors.begin()),
                         std::make_move_iterator(other.successors.end()));
        parents.insert(std::make_move_iterator(other.parents.begin()),
                      std::make_move_iterator(other.parents.end()));
        children.insert(std::make_move_iterator(other.children.begin()),
                       std::make_move_iterator(other.children.end()));
#endif
    }
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
    storage::bit_vector finished_nodes;
    storage::bit_vector entered_nodes;
    std::vector<storage::bit_vector> dependencies;
    std::vector<storage::bit_vector> children;
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

    template <typename T>
    T&& after(this T&& self, const SystemSetLabel& label) {
        self.edges.depends.insert(label);
        std::ranges::for_each(self.sub_configs, [&](SetConfig& config) { config.after(label); });
        return std::forward<T>(self);
    }
    template <typename T>
    T&& before(this T&& self, const SystemSetLabel& label) {
        self.edges.successors.insert(label);
        std::ranges::for_each(self.sub_configs, [&](SetConfig& config) { config.before(label); });
        return std::forward<T>(self);
    }
    template <typename T>
    T&& in_set(this T&& self, const SystemSetLabel& label) {
        self.edges.parents.insert(label);
        std::ranges::for_each(self.sub_configs, [&](SetConfig& config) { config.in_set(label); });
        return std::forward<T>(self);
    }
    template <typename T>
    T&& set_name(this T&& self, std::string_view name) {
        if (self.system) self.system->set_name(name);
        size_t index = 0;
        std::ranges::for_each(self.sub_configs,
                              [&](SetConfig& config) { config.set_name(std::format("{}#{}", name, index)); });
        return std::forward<T>(self);
    }
    template <typename T, typename Rng>
    T&& set_names(this T&& self, Rng&& names)
        requires std::ranges::range<Rng> && std::convertible_to<std::ranges::range_value_t<Rng>, std::string_view>
    {
        for (auto&& [config, name] : std::views::zip(self.sub_configs, names)) {
            config.set_name(name);
        }
        return std::forward<T>(self);
    }
    template <typename T, system::valid_function_system F>
    T&& run_if(this T&& self, F&& func)
        requires std::same_as<typename system::function_system_traits<F>::Input, std::tuple<>> &&
                 std::same_as<typename system::function_system_traits<F>::Output, bool>
    {
        self.conditions.push_back(system::make_system_unique(std::forward<F>(func)));
        std::ranges::for_each(self.sub_configs, [&](SetConfig& config) { config.run_if(std::forward<F>(func)); });
        return std::forward<T>(self);
    }
    template <typename T>
    T&& chain(this T&& self) {
        for (auto&& [c1, c2] : self.sub_configs | std::views::adjacent<2>) {
            c1.before(c2);
        }
        return std::forward<T>(self);
    }

    SetConfig clone() const {
        SetConfig config;
        config.label = label;
        if (system) config.system.reset(system->clone());
#ifdef __cpp_lib_containers_ranges
        config.conditions.insert_range(
            config.conditions.end(),
            conditions | std::views::transform([](const system::SystemUnique<std::tuple<>, bool>& cond) {
                return system::SystemUnique<std::tuple<>, bool>(cond->clone());
            }));
#else
        auto transformed_conditions = conditions | std::views::transform([](const system::SystemUnique<std::tuple<>, bool>& cond) {
                return system::SystemUnique<std::tuple<>, bool>(cond->clone());
            });
        config.conditions.insert(config.conditions.end(), 
                                std::ranges::begin(transformed_conditions),
                                std::ranges::end(transformed_conditions));
#endif
        config.edges = edges;
#ifdef __cpp_lib_containers_ranges
        config.sub_configs.insert_range(
            config.sub_configs.end(),
            sub_configs | std::views::transform([](const SetConfig& sub_config) { return sub_config.clone(); }));
#else
        auto transformed_sub_configs = sub_configs | std::views::transform([](const SetConfig& sub_config) { return sub_config.clone(); });
        config.sub_configs.insert(config.sub_configs.end(),
                                 std::ranges::begin(transformed_sub_configs),
                                 std::ranges::end(transformed_sub_configs));
#endif
        return std::move(config);
    }

   private:
    void before(const SetConfig& other) {
        if (other.label) before(*other.label);
        std::ranges::for_each(other.sub_configs, [&](const SetConfig& config) { before(config); });
    }

    std::optional<SystemSetLabel> label;
    system::SystemUnique<> system;
    std::vector<system::SystemUnique<std::tuple<>, bool>> conditions;
    Edges edges;

    std::vector<SetConfig> sub_configs;

    friend struct Schedule;
    template <bool require_system, typename F>
    friend SetConfig single_set(F&& func)
        requires(!require_system || requires {
            requires system::valid_function_system<F>;
            requires std::same_as<typename system::function_system_traits<F>::Input, std::tuple<>>;
            requires std::same_as<typename system::function_system_traits<F>::Output, void>;
        } || std::same_as<std::decay_t<F>, SetConfig>);
    template <bool require_system, typename... Ts>
    friend SetConfig make_sets(Ts&&... ts)
        requires(sizeof...(Ts) >= 1) &&
                (... && (!require_system || requires {
                     requires system::valid_function_system<Ts>;
                     requires std::same_as<typename system::function_system_traits<Ts>::Input, std::tuple<>>;
                     requires std::same_as<typename system::function_system_traits<Ts>::Output, void>;
                 } || std::same_as<std::decay_t<Ts>, SetConfig>));
};
template <bool require_system, typename F>
SetConfig single_set(F&& func)
    requires(!require_system ||
             requires {
                 requires system::valid_function_system<F>;
                 requires std::same_as<typename system::function_system_traits<F>::Input, std::tuple<>>;
                 requires std::same_as<typename system::function_system_traits<F>::Output, void>;
             } || std::same_as<std::decay_t<F>, SetConfig>)
{
    if constexpr (std::same_as<std::decay_t<F>, SetConfig>) {
        return std::forward<F>(func);
    }
    SetConfig config;
    if constexpr (std::constructible_from<SystemSetLabel, const F&>) {
        config.label.emplace(func);
    } else {
        config.label = SystemSetLabel::from_type<std::decay_t<F>>();
    }
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
SetConfig make_sets(Ts&&... ts)
    requires(sizeof...(Ts) >= 1) &&
            (... && (!require_system ||
                     requires {
                         requires system::valid_function_system<Ts>;
                         requires std::same_as<typename system::function_system_traits<Ts>::Input, std::tuple<>>;
                         requires std::same_as<typename system::function_system_traits<Ts>::Output, void>;
                     } || std::same_as<std::decay_t<Ts>, SetConfig>))
{
    if constexpr (sizeof...(Ts) == 1) {
        return single_set<require_system>(std::get<0>(std::forward_as_tuple(std::forward<Ts>(ts)...)));
    } else {
        SetConfig config;
        config.sub_configs.reserve(sizeof...(Ts));
        (config.sub_configs.push_back(single_set<require_system>(std::forward<Ts>(ts))), ...);
        return config;
    }
}
struct ExecuteConfig {
    bool apply_direct    = false;  // should call System::apply right after System::run or at the end of the schedule
    bool queue_deferred  = false;  // call System::queue for deferred systems instead of apply at the end
    bool handle_deferred = true;   // whether to handle deferred systems in this schedule
    bool run_once        = false;  // systems in this schedule will only run once and be removed

    bool is_defer_handled() const { return handle_deferred; }
    bool is_apply_direct() const { return handle_deferred && apply_direct; }
    bool is_queue_deferred() const { return handle_deferred && queue_deferred && !apply_direct; }
    bool is_apply_end() const { return handle_deferred && !apply_direct && !queue_deferred; }
};
struct Schedule {
   private:
    ScheduleLabel _label;
    std::unordered_map<SystemSetLabel, std::shared_ptr<Node>> nodes;
    std::shared_ptr<ScheduleCache> cache;
    ExecuteConfig _default_execute_config;

    std::optional<std::vector<std::shared_ptr<Node>>> pending_applies;

    /// Add sets and systems in config to the schedule. If accept_system is false, this only adds the configured edges
    /// and conditions to the target node, and remain the existing config if any.
    /// If accept_system is true, the config will be replaced if contains system, or merged if only contains set.
    void add_config(SetConfig config, bool accept_system = true);

   public:
    Schedule(const ScheduleLabel& label) : _label(label) {}
    Schedule(const Schedule&)            = delete;
    Schedule(Schedule&&)                 = default;
    Schedule& operator=(const Schedule&) = delete;
    Schedule& operator=(Schedule&&)      = default;

    ScheduleLabel label() const { return _label; }

    template <std::invocable<Schedule&> F>
    Schedule&& then(F&& func) && {
        func(*this);
        return std::move(*this);
    }
    template <std::invocable<Schedule&> F>
    Schedule& then(F&& func) & {
        func(*this);
        return *this;
    }

    /// Check if the schedule contains a set with the given label.
    bool contains_set(const SystemSetLabel& label) const { return nodes.contains(label); }
    /// Check if the schedule contains a set with the given label and has an associated system.
    bool contains_system(const SystemSetLabel& label) const {
        if (auto it = nodes.find(label); it != nodes.end()) {
            return (bool)it->second->system;
        }
        return false;
    }
    /// Add systems and sets in config to the schedule. Old config will be replaced if contains system, or merged if
    /// only contains set.
    void add_systems(SetConfig&& config) { add_config(std::move(config), true); }
    /// Configure sets in config to the schedule. The existing systems will remain, existing edges and conditions will
    /// be updated.
    void configure_sets(SetConfig&& config) { add_config(std::move(config), false); }
    void add_systems(SetConfig& config) { add_config(std::move(config), true); }
    void configure_sets(SetConfig& config) { add_config(std::move(config), false); }
    /// Remove the system associated with the set label. The set will remain.
    bool remove_system(const SystemSetLabel& label) {
        auto it = nodes.find(label);
        if (it != nodes.end()) {
            // remove the system, remain set
            it->second->system.reset();
            return true;
        }
        return false;
    }
    /// Remove the set and its associated system.
    bool remove_set(const SystemSetLabel& label) {
        auto it = nodes.find(label);
        if (it != nodes.end()) {
            nodes.erase(it);
            return true;
        }
        return false;
    }

    void set_default_execute_config(const ExecuteConfig& config) { _default_execute_config = config; }
    Schedule&& with_execute_config(const ExecuteConfig& config) && {
        set_default_execute_config(config);
        return std::move(*this);
    }
    Schedule& with_execute_config(const ExecuteConfig& config) & {
        set_default_execute_config(config);
        return *this;
    }
    const ExecuteConfig& default_execute_config() const { return _default_execute_config; }
    ExecuteConfig& default_execute_config() { return _default_execute_config; }

    // prepare the schedule (validate and build cache), if check_error is true, will check for errors
    // otherwise the error will cause skipped nodes during execution
    std::expected<void, SchedulePrepareError> prepare(bool check_error = true);
    void initialize_systems(World& world, bool force = false);
    void check_change_tick(Tick tick);
    void execute(SystemDispatcher& dispatcher) { execute(dispatcher, _default_execute_config); }
    void execute(SystemDispatcher& dispatcher, ExecuteConfig config);
    void apply_deferred(World& world);
};
static_assert(std::constructible_from<Schedule, Schedule&&>);
}  // namespace epix::core::schedule

namespace epix::core {
template <typename... Ts>
schedule::SetConfig into(Ts&&... ts)
    requires(sizeof...(Ts) >= 1) &&
            (... && (requires {
                 requires system::valid_function_system<Ts>;
                 requires std::same_as<typename system::function_system_traits<Ts>::Input, std::tuple<>>;
                 requires std::same_as<typename system::function_system_traits<Ts>::Output, void>;
             } || std::same_as<std::decay_t<Ts>, schedule::SetConfig>))
{
    return schedule::make_sets<true, Ts...>(std::forward<Ts>(ts)...);
}
template <typename... Ts>
schedule::SetConfig sets(Ts&&... ts)
    requires(sizeof...(Ts) >= 1)
{
    return schedule::make_sets<false, Ts...>(std::forward<Ts>(ts)...);
}
}  // namespace epix::core