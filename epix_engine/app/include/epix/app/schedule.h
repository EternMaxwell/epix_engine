#pragma once

#include <spdlog/spdlog.h>

#define BS_THREAD_POOL_NATIVE_EXTENSIONS
#include <BS_thread_pool.hpp>
#include <expected>
#include <queue>
#include <random>

#include "system.h"
#include "tracy.h"

namespace epix::app {
struct SystemLabel : public Label {
    template <typename... Args>
    SystemLabel(void (*func)(Args...)) : Label(typeid(func), (size_t)func) {}
    SystemLabel() noexcept : Label(typeid(void), 0) {}
    using Label::operator==;
    using Label::operator!=;
};
struct SystemSetLabel : public SystemLabel {
    template <typename T>
    SystemSetLabel(T t) {
        set_type(typeid(T));
        if constexpr (std::is_enum_v<T>) {
            set_index(static_cast<size_t>(t));
        }
    }
    SystemSetLabel(const SystemLabel& label) : SystemLabel(label) {}
    using SystemLabel::SystemLabel;
    using SystemLabel::operator==;
    using SystemLabel::operator!=;
};
enum class ExecutorType {
    SingleThread,
    MultiThread,
};
struct ExecutorLabel : public Label {
    template <typename T>
    ExecutorLabel(T t) : Label(t){};
    ExecutorLabel() noexcept : Label(ExecutorType::MultiThread) {}
    using Label::operator==;
    using Label::operator!=;
};

struct Executors {
    using executor_t = BS::thread_pool<BS::tp::priority>;

   private:
    entt::dense_map<ExecutorLabel, std::unique_ptr<executor_t>> pools;

   public:
    EPIX_API Executors();
    EPIX_API executor_t* get_pool(const ExecutorLabel& label) noexcept;
    EPIX_API void add_pool(const ExecutorLabel& label, size_t count) noexcept;
    EPIX_API void add_pool(
        const ExecutorLabel& label, const std::string& name, size_t count
    ) noexcept;
};

struct System {
    SystemLabel label;
    std::string name;
    ExecutorLabel executor;
    std::unique_ptr<BasicSystem<void>> system;

    std::shared_ptr<spdlog::logger> logger;

   private:
    // Cache conflicts for performance
    entt::dense_map<SystemLabel, bool>
        conflicts;  // Store system labels that are created from function
    entt::dense_map<SystemLabel, bool>
        conflicts_dyn;  // Store system labels that are created from
                        // unique_ptr<BasicSystem>
    static constexpr size_t max_conflict_cache = 4096;

    double time_cost = 1.0;  // in milliseconds
    double time_avg  = 1.0;  // in milliseconds
    double factor    = 0.1;  // for time_avg

   public:
    EPIX_API System(
        const SystemLabel& label,
        const std::string& name,
        std::unique_ptr<BasicSystem<void>>&& system
    );
    EPIX_API System(
        const SystemLabel& label, std::unique_ptr<BasicSystem<void>>&& system
    );
    System(const System&)            = delete;
    System(System&&)                 = default;
    System& operator=(const System&) = delete;
    System& operator=(System&&)      = default;
    EPIX_API bool conflict_with(const System& other) noexcept;
    EPIX_API void run(World& src, World& dst) noexcept;
    EPIX_API double get_time_avg() const noexcept { return time_avg; };
    EPIX_API double get_time_cost() const noexcept { return time_cost; };

    friend struct SystemSet;
};
struct SystemSet {
    std::vector<BasicSystem<bool>> conditions;
    entt::dense_set<SystemSetLabel> in_sets;
    entt::dense_set<SystemSetLabel> depends;
    entt::dense_set<SystemSetLabel> succeeds;

    entt::dense_map<SystemLabel, bool>
        conflicts;  // Store system labels that are created from function
    entt::dense_map<SystemLabel, bool>
        conflicts_dyn;  // Store system labels that are created from
                        // unique_ptr<BasicSystem>
    static constexpr size_t max_conflict_cache = 4096;

    EPIX_API bool conflict_with(const System& system) noexcept;
    EPIX_API void erase(const SystemSetLabel& label) noexcept {
        in_sets.erase(label);
        depends.erase(label);
        succeeds.erase(label);
    };
};
struct SystemConfig {
    SystemLabel label;
    std::string name;
    ExecutorLabel executor;
    std::unique_ptr<BasicSystem<void>> system;
    std::vector<BasicSystem<bool>> conditions;

    entt::dense_set<SystemSetLabel> in_sets;
    entt::dense_set<SystemSetLabel> depends;
    entt::dense_set<SystemSetLabel> succeeds;

    std::vector<SystemConfig> sub_configs;

   private:
    EPIX_API SystemConfig& after_internal(const SystemSetLabel& label) noexcept;
    EPIX_API SystemConfig& before_internal(const SystemSetLabel& label
    ) noexcept;
    EPIX_API SystemConfig& in_set_internal(const SystemSetLabel& label
    ) noexcept;
    EPIX_API SystemConfig& after_config(SystemConfig&) noexcept;
    template <typename... Args>
    SystemConfig& run_if_internal(std::function<bool(Args...)> func) noexcept {
        conditions.emplace_back(func);
        for (auto&& sub_config : sub_configs) {
            sub_config.run_if_internal(func);
        }
        return *this;
    };

   public:
    template <typename... Args>
    SystemConfig& after(Args&&... args) noexcept {
        (after_internal(args), ...);
        return *this;
    };
    template <typename... Args>
    SystemConfig& before(Args&&... args) noexcept {
        (before_internal(args), ...);
        return *this;
    };
    template <typename... Args>
    SystemConfig& in_set(Args&&... args) noexcept {
        (in_set_internal(args), ...);
        return *this;
    };
    EPIX_API SystemConfig& chain() noexcept;
    template <typename... Funcs>
    SystemConfig& run_if(Funcs&&... funcs) noexcept {
        (run_if_internal(std::function(funcs)), ...);
        return *this;
    };
    EPIX_API SystemConfig& set_executor(const ExecutorLabel& label) noexcept;
    EPIX_API SystemConfig& set_name(const std::string& name) noexcept;
    EPIX_API SystemConfig& set_name(
        size_t index, const std::string& name
    ) noexcept;
    EPIX_API SystemConfig& set_names(epix::util::ArrayProxy<std::string> names
    ) noexcept;
};
template <typename Other>
    requires std::same_as<SystemConfig, std::decay_t<Other>>
SystemConfig into_single(Other&& other) {
    return std::move(other);
}
template <typename Func>
SystemConfig into_single(Func&& func) {
    SystemConfig config;
    config.system = std::make_unique<BasicSystem<void>>(func);
    if constexpr (std::is_function_v<
                      std::remove_pointer_t<std::decay_t<Func>>>) {
        config.label = SystemLabel(func);
    } else {
        config.label.set_type(typeid(void));
        config.label.set_index((size_t)config.system.get());
    }
    config.name = std::format("System:{:#016x}", config.label.get_index());
    return std::move(config);
}
template <typename... Args>
SystemConfig into(Args&&... args) {
    SystemConfig config;
    (config.sub_configs.emplace_back(into_single(std::forward<Args>(args))),
     ...);
    return std::move(config);
}
struct SystemSetConfig {
    std::optional<SystemSetLabel> label;
    entt::dense_set<SystemSetLabel> in_sets;
    entt::dense_set<SystemSetLabel> depends;
    entt::dense_set<SystemSetLabel> succeeds;

    std::vector<BasicSystem<bool>> conditions;

    std::vector<SystemSetConfig> sub_configs;

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
    EPIX_API SystemSetConfig& chain() noexcept;

   private:
    EPIX_API SystemSetConfig& after_internal(const SystemSetLabel& label
    ) noexcept;
    EPIX_API SystemSetConfig& before_internal(const SystemSetLabel& label
    ) noexcept;
    EPIX_API SystemSetConfig& in_set_internal(const SystemSetLabel& label
    ) noexcept;
    template <typename... Args>
    SystemConfig& run_if_internal(std::function<bool(Args...)> func) noexcept {
        conditions.emplace_back(func);
        for (auto&& sub_config : sub_configs) {
            sub_config.run_if_internal(func);
        }
        return *this;
    };
    EPIX_API SystemSetConfig& after_config(SystemSetConfig&) noexcept;
};
template <typename Other>
    requires std::same_as<SystemSetConfig, std::decay_t<Other>>
SystemSetConfig sets_single(Other&& other) {
    return std::move(other);
}
template <typename T>
SystemSetConfig sets_single(T&& t) {
    SystemSetConfig config;
    config.label.emplace(t);
    return std::move(config);
}
template <typename... Args>
SystemSetConfig sets(Args... args) {
    SystemSetConfig config;
    (config.sub_configs.emplace_back(sets_single(std::forward<Args>(args))),
     ...);
    return std::move(config);
}
struct ScheduleLabel : public Label {
    template <typename T>
    ScheduleLabel(T t) : Label(t) {}
    using Label::operator==;
    using Label::operator!=;
};
struct Schedule;
template <typename T>
concept IsScheduleCommand = requires(T t) {
    { t.apply(std::declval<Schedule&>()) };
};
struct ScheduleCommandQueue {
   private:
    struct Command {
        std::type_index type;
        size_t size;
        void (*apply)(Schedule&, void*);
        void (*destruct)(void*);
    };
    entt::dense_map<std::type_index, std::uint8_t> m_command_map;
    std::vector<Command> m_registry;
    std::vector<std::uint8_t> m_commands;
    std::mutex m_mutex;

    template <IsScheduleCommand T, typename... Args>
    void enqueue_internal(Args&&... args) {
        using type = T;
        std::unique_lock lock(m_mutex);
        auto it       = m_command_map.find(typeid(type));
        uint8_t index = 0;
        if (it == m_command_map.end()) {
            if (m_registry.size() >= 256) {
                throw std::runtime_error(
                    "Command queue can not accept more than 256 command types."
                );
            }
            m_command_map.emplace(
                typeid(type), static_cast<uint8_t>(m_registry.size())
            );
            index = m_registry.size();
            m_registry.emplace_back(
                typeid(type), sizeof(type),
                [](Schedule& schedule, void* command) {
                    auto* cmd = reinterpret_cast<type*>(command);
                    cmd->apply(schedule);
                },
                [](void* command) {
                    auto* cmd = reinterpret_cast<type*>(command);
                    cmd->~type();
                }
            );
        } else {
            index = it->second;
        }
        m_commands.push_back(index);
        m_commands.resize(m_commands.size() + sizeof(type));
        auto* pcommand = reinterpret_cast<type*>(
            m_commands.data() + m_commands.size() - sizeof(type)
        );
        // construct the command in place
        new (pcommand) type(std::forward<Args>(args)...);
    }

   public:
    ScheduleCommandQueue()                                       = default;
    ScheduleCommandQueue(const ScheduleCommandQueue&)            = delete;
    ScheduleCommandQueue(ScheduleCommandQueue&&)                 = delete;
    ScheduleCommandQueue& operator=(const ScheduleCommandQueue&) = delete;
    ScheduleCommandQueue& operator=(ScheduleCommandQueue&&)      = delete;
    ~ScheduleCommandQueue()                                      = default;

    template <IsScheduleCommand T, typename... Args>
    void enqueue(Args&&... args) {
        enqueue_internal<T>(std::forward<Args>(args)...);
    };
    template <typename T>
    void enqueue(T&& command) {
        using type = std::decay_t<T>;
        enqueue_internal<type>(std::forward<T>(command));
    };
    EPIX_API bool flush(Schedule& schedule);
};
struct AddSystemsCommand {
    SystemConfig config;
    EPIX_API void apply(Schedule& schedule);
};
struct ConfigureSetsCommand {
    SystemSetConfig config;
    EPIX_API void apply(Schedule& schedule);
};
struct RemoveSystemCommand {
    SystemLabel label;
    EPIX_API void apply(Schedule& schedule);
};
struct RemoveSetCommand {
    SystemSetLabel label;
    EPIX_API void apply(Schedule& schedule);
};
struct ScheduleRunner;
struct Schedule {
    ScheduleLabel label;
    entt::dense_map<SystemLabel, System> systems;
    entt::dense_map<SystemSetLabel, SystemSet> system_sets;
    entt::dense_set<SystemSetLabel> newly_added_sets;
    std::mutex system_sets_mutex;

    std::shared_ptr<spdlog::logger> logger;

    std::unique_ptr<ScheduleRunner> prunner;

    ScheduleCommandQueue command_queue;

   private:
    // true if rebuilt
    EPIX_API bool build() noexcept;
    // true if any command
    EPIX_API bool flush() noexcept;

   public:
    EPIX_API Schedule(const ScheduleLabel& label);
    EPIX_API Schedule(Schedule&&);

    EPIX_API void set_logger(const std::shared_ptr<spdlog::logger>& logger);

    EPIX_API void add_systems(SystemConfig&& config);
    EPIX_API void add_systems(SystemConfig& config);
    EPIX_API void configure_sets(const SystemSetConfig& config);
    EPIX_API void remove_system(const SystemLabel& label);
    EPIX_API void remove_set(const SystemSetLabel& label);
    EPIX_API ScheduleRunner& runner() noexcept;

    friend struct ScheduleRunner;
};
struct RunSystemError {
    SystemLabel label;
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
struct ScheduleRunner {
    struct TracySettings {
        bool enabled = false;
    };

   private:
    TracySettings tracy_settings;
    Schedule& schedule;
    World* src;
    World* dst;
    bool run_once;
    std::shared_ptr<Executors> executors;

    std::deque<uint32_t> wait_to_enter_queue;
    bool new_entered = false;
    entt::dense_set<uint32_t> systems_running;
    std::deque<uint32_t> waiting_systems;
    std::deque<std::pair<uint32_t, bool>> waiting_sets;

    epix::utils::async::ConQueue<uint32_t> just_finished_sets;

    struct SystemSetInfo {
        SystemSetLabel label;
        SystemSet* set;
        System* system;
        std::vector<uint32_t> parents;
        std::vector<uint32_t> succeeds;

        uint32_t depends_count;
        uint32_t children_count;

        bool entered;
        bool passed;
        bool finished;
    };

    std::vector<SystemSetInfo> system_set_infos;
    entt::dense_map<SystemSetLabel, uint32_t> set_index_map;

    EPIX_API void enter_waiting_queue();
    EPIX_API void try_enter_waiting_sets();
    EPIX_API void try_run_waiting_systems();

    EPIX_API void prepare_runner();
    EPIX_API void sync_schedule();
    EPIX_API void run_loop();
    EPIX_API void finishing();
    EPIX_API std::expected<void, RunScheduleError> run_internal();
    EPIX_API std::expected<void, RunSystemError> run_system(uint32_t index);

    EPIX_API ScheduleRunner(Schedule& schedule, bool run_once = false);

   public:
    EPIX_API TracySettings& get_tracy_settings() noexcept;
    EPIX_API void set_run_once(bool run_once) noexcept;
    EPIX_API void set_worlds(World& src, World& dst) noexcept;
    EPIX_API void set_executors(const std::shared_ptr<Executors>& executors
    ) noexcept;
    EPIX_API std::expected<void, RunScheduleError> run();
    EPIX_API void reset() noexcept;

    friend struct Schedule;
};
};  // namespace epix::app