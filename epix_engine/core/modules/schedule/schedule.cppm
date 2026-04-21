module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
export module epix.core:schedule.schedule;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import BS.thread_pool;
import epix.meta;

export import :schedule.queue;

import :label;
import :labels;
import :storage;

namespace epix::core {
struct Node;
struct Edges {
    std::unordered_set<SystemSetLabel> depends;
    std::unordered_set<SystemSetLabel> successors;
    std::unordered_set<SystemSetLabel> parents;
    std::unordered_set<SystemSetLabel> children;

    void merge(Edges other) {
        depends.insert_range(std::move(other.depends));
        successors.insert_range(std::move(other.successors));
        parents.insert_range(std::move(other.parents));
        children.insert_range(std::move(other.children));
    }
};
struct Node {
    Node(const SystemSetLabel& label) : label(label) {}

    SystemSetLabel label;
    SystemUnique<> system;
    FilteredAccessSet system_access;
    std::vector<SystemUnique<std::tuple<>, bool>> conditions;
    std::vector<FilteredAccessSet> condition_access;

    Edges edges;
    Edges validated_edges;
};
struct CachedNode {
    std::shared_ptr<Node> node;  // stores shared_ptr so you can safely add, remove nodes while executing
    std::vector<std::size_t> depends;
    std::vector<std::size_t> successors;
    std::vector<std::size_t> parents;
    std::vector<std::size_t> children;
};
struct ScheduleCache {
    std::vector<CachedNode> nodes;
    std::unordered_map<SystemSetLabel, std::size_t> node_map;
};
struct ExecutionState {
    std::size_t running_count   = 0;
    std::size_t remaining_count = 0;
    bit_vector ready_nodes;
    bit_vector finished_nodes;
    bit_vector entered_nodes;
    std::vector<bit_vector> dependencies;
    std::vector<bit_vector> children;
    bit_vector condition_met_nodes;
    std::vector<bit_vector> untest_conditions;
    std::vector<std::size_t> wait_count;  // number of dependencies + parents not yet satisfied
    std::vector<std::size_t> child_count;
    async_queue finished_queue;
    std::vector<std::size_t> ready_stack;
};
export struct SchedulePrepareError {
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
/** @brief Configuration for a set of systems, including ordering constraints,
 *  conditions, and sub-configs. Built via `into()` or `sets()` helpers. */
export struct SetConfig {
   public:
    SetConfig() = default;

    /** @brief Add an ordering dependency: this set runs after `label`. */
    template <typename T>
    T&& after(this T&& self, const SystemSetLabel& label) {
        self.edges.depends.insert(label);
        std::ranges::for_each(self.sub_configs, [&](SetConfig& config) { config.after(label); });
        return std::forward<T>(self);
    }
    /** @brief Add an ordering dependency: this set runs before `label`. */
    template <typename T>
    T&& before(this T&& self, const SystemSetLabel& label) {
        self.edges.successors.insert(label);
        std::ranges::for_each(self.sub_configs, [&](SetConfig& config) { config.before(label); });
        return std::forward<T>(self);
    }
    /** @brief Add this set as a child of the given parent set. */
    template <typename T>
    T&& in_set(this T&& self, const SystemSetLabel& label) {
        self.edges.parents.insert(label);
        std::ranges::for_each(self.sub_configs, [&](SetConfig& config) { config.in_set(label); });
        return std::forward<T>(self);
    }
    /** @brief Set a display name for the system. */
    template <typename T>
    T&& set_name(this T&& self, std::string_view name) {
        if (self.system) self.system->set_name(name);
        std::size_t index = 0;
        std::ranges::for_each(self.sub_configs,
                              [&](SetConfig& config) { config.set_name(std::format("{}#{}", name, index++)); });
        return std::forward<T>(self);
    }
    /** @brief Set display names for this set and its sub-configs from a range. */
    template <typename T, typename Rng>
    T&& set_names(this T&& self, Rng&& names)
        requires std::ranges::range<Rng> && std::convertible_to<std::ranges::range_value_t<Rng>, std::string_view>
    {
        if (self.system) {
            auto it = std::ranges::begin(names);
            if (it != std::ranges::end(names)) {
                self.system->set_name(*it);
                ++it;
            }
        }
        for (auto&& [config, name] : std::views::zip(self.sub_configs, names)) {
            config.set_name(name);
        }
        return std::forward<T>(self);
    }
    /** @brief Add a run condition: the system only runs when `func` returns true. */
    template <typename T, valid_function_system F>
    T&& run_if(this T&& self, F&& func)
        requires std::same_as<typename function_system_traits<F>::Input, std::tuple<>> &&
                 std::same_as<typename function_system_traits<F>::Output, bool>
    {
        self.conditions.push_back(make_system_unique(std::forward<F>(func)));
        std::ranges::for_each(self.sub_configs, [&](SetConfig& config) { config.run_if(std::forward<F>(func)); });
        return std::forward<T>(self);
    }
    /** @brief Chain all sub-configs in order: each runs after the previous. */
    template <typename T>
    T&& chain(this T&& self) {
        for (auto&& [c1, c2] : std::views::adjacent<2>(self.sub_configs)) {
            c1.before(c2);
        }
        return std::forward<T>(self);
    }

    /** @brief Deep-copy this SetConfig, cloning all systems and conditions. */
    SetConfig clone() const {
        SetConfig config;
        config.label = label;
        if (system) config.system.reset(system->clone());
        config.conditions.insert_range(
            config.conditions.end(),
            std::views::transform(conditions, [](const SystemUnique<std::tuple<>, bool>& cond) {
                return SystemUnique<std::tuple<>, bool>(cond->clone());
            }));
        config.edges = edges;
        config.sub_configs.insert_range(
            config.sub_configs.end(),
            std::views::transform(sub_configs, [](const SetConfig& sub_config) { return sub_config.clone(); }));
        return std::move(config);
    }

   private:
    void before(const SetConfig& other) {
        if (other.label) before(*other.label);
        std::ranges::for_each(other.sub_configs, [&](const SetConfig& config) { before(config); });
    }

    std::optional<SystemSetLabel> label;
    SystemUnique<> system;
    std::vector<SystemUnique<std::tuple<>, bool>> conditions;
    Edges edges;

    std::vector<SetConfig> sub_configs;

    friend struct Schedule;
    template <bool require_system, typename F>
    friend SetConfig single_set(F&& func)
        requires(!require_system || requires {
            requires valid_function_system<F>;
            requires std::same_as<typename function_system_traits<F>::Input, std::tuple<>>;
            requires std::same_as<typename function_system_traits<F>::Output, void>;
        } || std::same_as<std::decay_t<F>, SetConfig>);
    template <bool require_system, typename... Ts>
    friend SetConfig make_sets(Ts&&... ts)
        requires(sizeof...(Ts) >= 1) &&
                (... && (!require_system || requires {
                     requires valid_function_system<Ts>;
                     requires std::same_as<typename function_system_traits<Ts>::Input, std::tuple<>>;
                     requires std::same_as<typename function_system_traits<Ts>::Output, void>;
                 } || std::same_as<std::decay_t<Ts>, SetConfig>));
};
template <bool require_system, typename F>
SetConfig single_set(F&& func)
    requires(!require_system ||
             requires {
                 requires valid_function_system<F>;
                 requires std::same_as<typename function_system_traits<F>::Input, std::tuple<>>;
                 requires std::same_as<typename function_system_traits<F>::Output, void>;
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
                      { make_system(std::forward<F>(func)) } -> std::same_as<System<std::tuple<>, void>*>;
                  }) {
        config.system.reset(make_system(std::forward<F>(func)));
    }
    return config;
}
template <bool require_system = false, typename... Ts>
SetConfig make_sets(Ts&&... ts)
    requires(sizeof...(Ts) >= 1) &&
            (... && (!require_system ||
                     requires {
                         requires valid_function_system<Ts>;
                         requires std::same_as<typename function_system_traits<Ts>::Input, std::tuple<>>;
                         requires std::same_as<typename function_system_traits<Ts>::Output, void>;
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
/** @brief How deferred systems are applied during schedule execution. */
export enum class DeferredApply {
    ApplyDirect,    // apply deferred commands immediately after each system runs
    QueueDeferred,  // queue deferred commands for batched apply later
    ApplyEnd,       // collect and apply all deferred commands at end of schedule
    Ignore,         // do not handle deferred systems at all
};
/** @brief Configuration passed to the executor controlling deferred handling and error callbacks. */
export struct ExecutorConfig {
    DeferredApply deferred = DeferredApply::ApplyEnd;
    std::function<void(const RunSystemError&)> on_error;  // callback for handling system run errors
};
/** @brief Configuration controlling how the schedule executes its systems. */
export struct ScheduleConfig {
    ExecutorConfig executor_config;
    bool run_once = false;  // systems in this schedule will only run once and be removed

    /** @brief Schedule-level run conditions. All must return true for the schedule to execute.
     *  Evaluated with exclusive world access before execution begins.
     *  If any returns false, the entire schedule (including pre/post systems) is skipped. */
    std::vector<std::function<bool(World&)>> conditions;

    /** @brief Loop condition for repeated execution. When set, the schedule's main body
     *  (system execution + deferred handling) loops while this returns true.
     *  Pre-systems run once before the loop, post-systems run once after.
     *  The condition is evaluated with exclusive world access before each iteration. */
    std::function<bool(World&)> loop_condition;
};
export struct ScheduleSystems {
    std::unordered_map<SystemSetLabel, std::shared_ptr<Node>> nodes;
    std::shared_ptr<ScheduleCache> cache;
    std::optional<std::vector<std::shared_ptr<Node>>> pending_applies;
};
export struct ScheduleExecutor {
    ScheduleLabel label;
    virtual ~ScheduleExecutor()                                                                 = default;
    virtual void execute(ScheduleSystems& schedule, World& world, const ExecutorConfig& config) = 0;
    virtual meta::type_index type() const                                                       = 0;
};
/** @brief A named collection of systems with dependency ordering and parallel execution support.
 *  Systems are organized into sets with before/after/in_set relationships. */
export struct Schedule {
   private:
    ScheduleLabel _label;
    ScheduleConfig _default_schedule_config;
    ScheduleSystems _data;
    std::unique_ptr<ScheduleExecutor> executor;

    static std::unique_ptr<ScheduleExecutor> default_executor();

    /** @brief Add sets and systems from config. If accept_system is false, only edges/conditions are merged;
     *  if true, existing systems are replaced when the config carries a system. */
    void add_config(SetConfig config, bool accept_system = true);

    struct PrePostSystem {
        SystemUnique<> system;
        FilteredAccessSet access;
        bool initialized = false;
    };
    std::vector<PrePostSystem> m_pre_systems;
    std::vector<PrePostSystem> m_post_systems;

    static void extract_systems_from_config(SetConfig& config, std::vector<PrePostSystem>& target);

   public:
    Schedule(const ScheduleLabel& label) : _label(label) {}
    Schedule(const Schedule&)            = delete;
    Schedule(Schedule&&)                 = default;
    Schedule& operator=(const Schedule&) = delete;
    Schedule& operator=(Schedule&&)      = default;

    static std::optional<ScheduleLabel> current_label();

    /** @brief Get the schedule's label. */
    ScheduleLabel label() const { return _label; }

    /** @brief Chain a configuration function on an rvalue schedule. */
    template <std::invocable<Schedule&> F>
    Schedule&& then(F&& func) && {
        func(*this);
        return std::move(*this);
    }
    /** @brief Chain a configuration function on an lvalue schedule. */
    template <std::invocable<Schedule&> F>
    Schedule& then(F&& func) & {
        func(*this);
        return *this;
    }

    /** @brief Check if the schedule contains a set with the given label. */
    bool contains_set(const SystemSetLabel& label) const { return _data.nodes.contains(label); }
    /** @brief Check if the schedule contains a set with the given label and has a system. */
    bool contains_system(const SystemSetLabel& label) const {
        if (auto it = _data.nodes.find(label); it != _data.nodes.end()) {
            return (bool)it->second->system;
        }
        return false;
    }
    /** @brief Add systems and sets from config. Existing systems are replaced. */
    void add_systems(SetConfig&& config) { add_config(std::move(config), true); }
    /** @brief Configure sets from config. Existing systems remain; edges/conditions are updated. */
    void configure_sets(SetConfig&& config) { add_config(std::move(config), false); }
    void add_systems(SetConfig& config) { add_config(std::move(config), true); }
    void configure_sets(SetConfig& config) { add_config(std::move(config), false); }
    /** @brief Remove the system associated with the set label. The set remains. */
    bool remove_system(const SystemSetLabel& label) {
        auto it = _data.nodes.find(label);
        if (it != _data.nodes.end()) {
            // remove the system, remain set
            it->second->system.reset();
            return true;
        }
        return false;
    }
    /** @brief Remove the set and its associated system entirely. */
    bool remove_set(const SystemSetLabel& label) {
        auto it = _data.nodes.find(label);
        if (it != _data.nodes.end()) {
            _data.nodes.erase(it);
            return true;
        }
        return false;
    }

    /** @brief Set the default schedule configuration. */
    void set_default_schedule_config(const ScheduleConfig& config) { _default_schedule_config = config; }
    /** @brief Set the default schedule configuration (rvalue chain). */
    Schedule&& with_schedule_config(const ScheduleConfig& config) && {
        set_default_schedule_config(config);
        return std::move(*this);
    }
    /** @brief Set the default schedule configuration (lvalue chain). */
    Schedule& with_schedule_config(const ScheduleConfig& config) & {
        set_default_schedule_config(config);
        return *this;
    }
    /** @brief Get the default schedule configuration (const). */
    const ScheduleConfig& default_schedule_config() const { return _default_schedule_config; }
    /** @brief Get the default schedule configuration (mutable). */
    ScheduleConfig& default_schedule_config() { return _default_schedule_config; }

    void set_executor(std::unique_ptr<ScheduleExecutor> exec) {
        executor        = std::move(exec);
        executor->label = label();
    }
    Schedule& with_executor(std::unique_ptr<ScheduleExecutor> exec) & {
        set_executor(std::move(exec));
        return *this;
    }
    Schedule&& with_executor(std::unique_ptr<ScheduleExecutor> exec) && {
        set_executor(std::move(exec));
        return std::move(*this);
    }

    /** @brief Validate dependencies and build the execution cache.
     *  @param check_error If true, returns an error on validation failure. */
    std::expected<void, SchedulePrepareError> prepare(bool check_error = true);
    /** @brief Initialize all systems in this schedule by calling their initialize() methods.
     *  @param force If true, re-initializes already-initialized systems. */
    void initialize_systems(World& world, bool force = false);
    /** @brief Clamp stale change ticks on all systems. */
    void check_change_tick(Tick tick);
    /** @brief Execute the schedule using the default configuration. */
    void execute(World& world) { execute(world, _default_schedule_config); }
    /** @brief Execute the schedule with the given configuration. */
    void execute(World& world, const ScheduleConfig& config);
    /** @brief Apply all pending deferred commands from systems. */
    void apply_deferred(World& world);
    /** @brief Add systems that run before all scheduled systems in this schedule.
     *  Pre-systems run sequentially with exclusive world access. */
    void add_pre_systems(SetConfig&& config);
    /** @brief Add systems that run after all scheduled systems in this schedule.
     *  Post-systems run sequentially with exclusive world access. */
    void add_post_systems(SetConfig&& config);
};
static_assert(std::constructible_from<Schedule, Schedule&&>);

/** @brief Create a SetConfig from one or more system functions.
 *  All arguments must be valid system functions or existing SetConfig objects. */
export template <typename... Ts>
SetConfig into(Ts&&... ts)
    requires(sizeof...(Ts) >= 1) &&
            (... && (requires {
                 requires valid_function_system<Ts>;
                 requires std::same_as<typename function_system_traits<Ts>::Input, std::tuple<>>;
                 requires std::same_as<typename function_system_traits<Ts>::Output, void>;
             } || std::same_as<std::decay_t<Ts>, SetConfig>))
{
    return make_sets<true, Ts...>(std::forward<Ts>(ts)...);
}
/** @brief Create a SetConfig from one or more label-only sets (no systems required). */
export template <typename... Ts>
SetConfig sets(Ts&&... ts)
    requires(sizeof...(Ts) >= 1)
{
    return make_sets<false, Ts...>(std::forward<Ts>(ts)...);
}
}  // namespace epix::core