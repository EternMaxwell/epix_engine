#pragma once

#include <spdlog/spdlog.h>

#define BS_THREAD_POOL_NATIVE_EXTENSIONS
#include <BS_thread_pool.hpp>
#include <expected>
#include <queue>
#include <random>

#include "commands.h"
#include "query.h"
#include "res.h"
#include "run_state.h"
#include "system.h"
#include "tracy.h"

namespace epix::app {
struct SystemSetLabel : public Label {
    template <typename T>
    SystemSetLabel(T t) {
        set_type(meta::type_id<T>{});
        if constexpr (std::is_enum_v<T>) {
            set_index(static_cast<size_t>(t));
        }
    }
    template <typename... Args>
    SystemSetLabel(void (*func)(Args...))
        : Label(meta::type_id<decltype(func)>{}, (size_t)func) {}
    SystemSetLabel() noexcept : Label(meta::type_id<void>{}, 0) {}
    // using SystemLabel::operator==;
    // using SystemLabel::operator!=;
};
struct SystemSet {
    SystemSetLabel label;
    std::string name;
    ExecutorLabel executor;
    std::unique_ptr<BasicSystem<void>> system;
    std::vector<std::unique_ptr<BasicSystem<bool>>> conditions;

    // these dependencies only describes `this system set`'s dependency.
    entt::dense_set<SystemSetLabel> in_sets;
    entt::dense_set<SystemSetLabel> depends;
    entt::dense_set<SystemSetLabel> succeeds;

    // the built dependencies;
    entt::dense_set<SystemSetLabel> built_in_sets;
    entt::dense_set<SystemSetLabel> built_depends;
    entt::dense_set<SystemSetLabel> built_succeeds;

    entt::dense_map<SystemSetLabel, bool>
        conflicts;  // Store system labels that are created from function
    entt::dense_map<SystemSetLabel, bool>
        conflicts_dyn;  // Store system labels that are created from
                        // unique_ptr<BasicSystem>
    static constexpr size_t max_conflict_cache = 4096;

    EPIX_API bool conflict_with(const SystemSet& system) noexcept;
    EPIX_API void detach(const SystemSetLabel& label) noexcept {
        built_in_sets.erase(label);
        built_depends.erase(label);
        built_succeeds.erase(label);
    };
};
struct SystemSetConfig {
    std::optional<SystemSetLabel> label;
    std::string name;
    ExecutorLabel executor;
    std::unique_ptr<BasicSystem<void>> system;

    std::vector<std::unique_ptr<BasicSystem<bool>>> conditions;

    entt::dense_set<SystemSetLabel> in_sets;
    entt::dense_set<SystemSetLabel> depends;
    entt::dense_set<SystemSetLabel> succeeds;

    std::vector<SystemSetConfig> sub_configs;

    SystemSetConfig() noexcept = default;
    SystemSetConfig(const SystemSetConfig& other) noexcept {
        label    = other.label;
        name     = other.name;
        executor = other.executor;
        system   = other.system ? other.system->clone_unique() : nullptr;
        conditions.reserve(other.conditions.size());
        for (const auto& cond : other.conditions) {
            conditions.emplace_back(cond->clone());
        }
        in_sets  = other.in_sets;
        depends  = other.depends;
        succeeds = other.succeeds;
        sub_configs.reserve(other.sub_configs.size());
        for (const auto& sub : other.sub_configs) {
            sub_configs.emplace_back(sub);
        }
    }
    SystemSetConfig(SystemSetConfig&& other) noexcept = default;
    SystemSetConfig& operator=(const SystemSetConfig& other) noexcept {
        if (this != &other) {
            label    = other.label;
            name     = other.name;
            executor = other.executor;
            system   = other.system ? other.system->clone_unique() : nullptr;
            conditions.reserve(other.conditions.size());
            for (const auto& cond : other.conditions) {
                conditions.emplace_back(cond->clone());
            }
            in_sets  = other.in_sets;
            depends  = other.depends;
            succeeds = other.succeeds;
            sub_configs.reserve(other.sub_configs.size());
            for (const auto& sub : other.sub_configs) {
                sub_configs.emplace_back(sub);
            }
        }
        return *this;
    }
    SystemSetConfig& operator=(SystemSetConfig&& other) noexcept = default;

    template <typename... Args>
    SystemSetConfig& after(Args&&... args) noexcept {
        (after_internal(args), ...);
        return *this;
    };
    template <typename... Args>
    SystemSetConfig& before(Args&&... args) noexcept {
        (before_internal(args), ...);
        return *this;
    };
    template <typename... Args>
    SystemSetConfig& in_set(Args&&... args) noexcept {
        (in_set_internal(args), ...);
        return *this;
    };
    template <typename... Funcs>
    SystemSetConfig& run_if(Funcs&&... conds) noexcept {
        (run_if_internal(conds), ...);
        return *this;
    }
    EPIX_API SystemSetConfig& set_executor(const ExecutorLabel& label) noexcept;
    EPIX_API SystemSetConfig& set_name(const std::string& name) noexcept;
    EPIX_API SystemSetConfig& set_name(size_t index,
                                       const std::string& name) noexcept;
    EPIX_API SystemSetConfig& set_names(
        epix::util::ArrayProxy<std::string> names) noexcept;
    EPIX_API SystemSetConfig& chain() noexcept;

   private:
    EPIX_API SystemSetConfig& after_internal(
        const SystemSetLabel& label) noexcept;
    EPIX_API SystemSetConfig& before_internal(
        const SystemSetLabel& label) noexcept;
    EPIX_API SystemSetConfig& in_set_internal(
        const SystemSetLabel& label) noexcept;
    template <typename Func>
    SystemSetConfig& run_if_internal(Func&& func) noexcept {
        if (label) {
            std::unique_ptr<BasicSystem<bool>> cond =
                IntoSystem::into_unique(std::forward<Func>(func));
            conditions.emplace_back(std::move(cond));
        }
        for (auto&& sub_config : sub_configs) {
            sub_config.run_if_internal(func);
        }
        return *this;
    };
    EPIX_API SystemSetConfig& after_config(SystemSetConfig&) noexcept;
};
template <typename Other>
    requires std::same_as<SystemSetConfig, std::decay_t<Other>>
SystemSetConfig into_single(Other&& other) {
    return std::move(other);
}
template <typename Func>
SystemSetConfig into_single(Func&& func) {
    SystemSetConfig config;
    config.system = IntoSystem::into_system(std::forward<Func>(func));
    if constexpr (std::is_function_v<
                      std::remove_pointer_t<std::decay_t<Func>>>) {
        config.label = SystemSetLabel(func);
    } else {
        config.label.emplace();
        config.label->set_index((size_t)config.system.get());
    }
    config.name = std::format("{:#016x}", config.label->get_index());
    return std::move(config);
}
template <typename... Args>
SystemSetConfig into(Args&&... args) {
    if constexpr (sizeof...(Args) == 1) {
        return into_single(std::forward<Args>(args)...);
    }
    SystemSetConfig config;
    (config.sub_configs.emplace_back(into_single(std::forward<Args>(args))),
     ...);
    return std::move(config);
}
template <typename Other>
    requires std::same_as<SystemSetConfig, std::decay_t<Other>>
SystemSetConfig sets_single(Other&& other) {
    return std::move(other);
}
template <typename T>
SystemSetConfig sets_single(T&& t) {
    SystemSetConfig config;
    config.label = SystemSetLabel(t);
    return std::move(config);
}
template <typename... Args>
SystemSetConfig sets(Args... args) {
    if constexpr (sizeof...(Args) == 1) {
        return sets_single(std::forward<Args>(args)...);
    }
    SystemSetConfig config;
    (config.sub_configs.emplace_back(sets_single(std::forward<Args>(args))),
     ...);
    return std::move(config);
}
struct ScheduleLabel : public Label {
    template <typename T>
    ScheduleLabel(T t) : Label(t) {}
    using Label::Label;
    using Label::operator==;
    using Label::operator!=;
};
struct Schedule;
template <typename T>
concept IsScheduleCommand = requires(T t) {
    { t.apply(std::declval<Schedule&>()) };
};
using ScheduleCommandQueue = epix::utils::AtomicCommandQueue<Schedule&>;
struct AddSystemsCommand {
    SystemSetConfig config;
    EPIX_API void apply(Schedule& schedule);
};
struct ConfigureSetsCommand {
    SystemSetConfig config;
    EPIX_API void apply(Schedule& schedule);
};
struct RemoveSystemCommand {
    SystemSetLabel label;
    EPIX_API void apply(Schedule& schedule);
};
struct RemoveSetCommand {
    SystemSetLabel label;
    EPIX_API void apply(Schedule& schedule);
};
struct ScheduleRunner;
struct ScheduleData {
    const ScheduleLabel label;
    async::RwLock<entt::dense_map<SystemSetLabel, SystemSet>> system_sets;
    async::ConQueue<std::pair<SystemSetLabel, bool>> newly_modified_sets;
    ScheduleCommandQueue command_queue;

   public:
    EPIX_API ScheduleData(const ScheduleLabel& label);
};
// Struct for schedule cache. This is used to accelerate schedule runs.
struct ScheduleCache {
    struct SystemSetInfo {
        SystemSet* set;
        std::vector<uint32_t> parents;
        std::vector<uint32_t> succeeds;

        uint32_t depends_count;
        uint32_t children_count;
        uint32_t cached_children_count;
        uint32_t cached_depends_count;

        bool entered;
        bool passed;
        bool finished;
    };

    std::vector<SystemSetInfo> system_set_infos;
    entt::dense_map<SystemSetLabel, uint32_t> set_index_map;
};
struct RunNodeError {
    SystemSetLabel label;
    enum class Type {
        NoExecutorsProvided,
        ExecutorNotFound,
        SystemNotFound,
    } type;
};
struct RunScheduleError {
    ScheduleLabel label;
    enum class Type {
        SetsRemaining,
        WorldsNotSet,
    } type;
    union {
        uint32_t remain_count;
    };
};
struct Schedule {
    struct Config {
        bool run_once     = false;
        bool enable_tracy = false;
    } config;

   private:
    std::unique_ptr<ScheduleData> data;
    std::unique_ptr<async::RwLock<ScheduleCache>> cache;
    World* last_world = nullptr;

    EPIX_API static void update_cache(
        entt::dense_map<SystemSetLabel, SystemSet>& system_sets,
        ScheduleCache& cache) noexcept;
    EPIX_API bool build_sets() noexcept;
    EPIX_API bool flush_cmd() noexcept;

    EPIX_API std::expected<void, RunScheduleError> run_internal(
        RunState& run_state) noexcept;

   public:
    EPIX_API Schedule(const ScheduleLabel& label);
    Schedule(const Schedule&)                      = delete;
    Schedule(Schedule&& other) noexcept            = default;
    Schedule& operator=(const Schedule&)           = delete;
    Schedule& operator=(Schedule&& other) noexcept = default;

    EPIX_API ScheduleLabel label() const noexcept;

    EPIX_API void initialize_systems(World& world);

    EPIX_API void add_systems(SystemSetConfig&& config);
    EPIX_API void add_systems(SystemSetConfig& config);
    EPIX_API void configure_sets(const SystemSetConfig& config);
    EPIX_API void remove_system(const SystemSetLabel& label);
    EPIX_API void remove_set(const SystemSetLabel& label);
    EPIX_API bool contains_system(const SystemSetLabel& label) const noexcept;
    EPIX_API bool contains_set(const SystemSetLabel& label) const noexcept;

    // This `run` call will dispatch the internal run task to another thread.
    // So it needs to be provided a shared pointer to `RunState`.
    EPIX_API std::future<std::expected<void, RunScheduleError>> run(
        RunState run_state) noexcept;

    friend struct ScheduleRunner;
};
};  // namespace epix::app