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
#include "system.h"
#include "tracy.h"

namespace epix::app {
enum class ExecutorType {
    SingleThread,
    MultiThread,
};
struct ExecutorLabel : public Label {
    template <typename T>
    ExecutorLabel(T t) : Label(t){};
    ExecutorLabel() noexcept : Label(ExecutorType::MultiThread) {}
    // using Label::operator==;
    // using Label::operator!=;
};

struct Executors {
    using executor_t = BS::thread_pool<BS::tp::priority>;

   private:
    async::RwLock<entt::dense_map<ExecutorLabel, std::unique_ptr<executor_t>>>
        pools;

   public:
    EPIX_API Executors();
    EPIX_API executor_t* get_pool(const ExecutorLabel& label) noexcept;
    EPIX_API void add_pool(const ExecutorLabel& label, size_t count) noexcept;
    EPIX_API void add_pool(const ExecutorLabel& label,
                           const std::string& name,
                           size_t count) noexcept;
};

struct MissingExecutorError {
    ExecutorLabel label;
};
using EnqueueSystemError = std::variant<MissingExecutorError>;
// A struct that stores the run state of a world.
// Stores all running system infos and all waiting systems.
// This struct should be used per world, and will handle resource conflicts
// between systems.
// This struct should be thread-safe.
struct RunStateData {
    struct RunSystemConfig {
        std::function<void()> on_finish = []() {};
        std::function<void()> on_start  = []() {};
        ExecutorLabel executor          = ExecutorType::MultiThread;
        bool enable_tracy               = false;
        std::optional<std::string> tracy_name;
    };

   private:
    async::RwLock<World>::WriteGuard world;
    Executors* executors;
    std::mutex
        m_system_mutex;  // Mutex for running_systems and waiting_system_callers
    std::condition_variable
        m_system_cv;  // Condition variable for waiting systems
    entt::dense_set<const SystemMeta*> running_systems;
    async::ConQueue<const SystemMeta*> finished_systems;
    std::deque<std::function<bool()>>
        waiting_system_callers;  // Callers that are waiting for a system to run
    std::unique_ptr<BasicSystem<void>> m_apply_commands;

   public:
    EPIX_API RunStateData(async::RwLock<World>::WriteGuard&& world,
                          Executors& executors);
    EPIX_API ~RunStateData();

    EPIX_API void apply_commands();
    EPIX_API bool wait();

    template <typename ret>
    std::expected<std::future<std::expected<ret, RunSystemError>>,
                  EnqueueSystemError>
    run_system(BasicSystem<ret>* system, const RunSystemConfig& config) {
        auto pool = executors->get_pool(config.executor);
        if (pool == nullptr) {
            return std::unexpected(MissingExecutorError{config.executor});
        }
        // checking whether the system conflicts with any running system
        auto prom = std::make_shared<
            std::promise<std::expected<ret, RunSystemError>>>();
        auto ft       = prom->get_future();
        auto try_call = [this, system, prom = std::move(prom), pool,
                         config]() mutable -> bool {
            for (auto&& sys : running_systems) {
                if (SystemMeta::conflict(system->get_meta(), *sys)) {
                    return false;
                }
            }
            running_systems.emplace(&system->get_meta());
            pool->detach_task(
                [this, system, prom = std::move(prom), config]() mutable {
                    std::optional<std::expected<ret, RunSystemError>> result;
                    try {
                        if (config.on_start) config.on_start();
                        if (config.enable_tracy) {
                            ZoneScopedN("RunState run system.");
                            if (config.tracy_name) {
                                auto size = config.tracy_name->size();
                                ZoneName(config.tracy_name->data(), size);
                            }
                            result.emplace(system->run(*world));
                        } else {
                            result.emplace(system->run(*world));
                        }
                    } catch (...) {
                        result.emplace(std::unexpected(
                            SystemExceptionError{std::current_exception()}));
                    }
                    std::unique_lock lock(m_system_mutex);
                    running_systems.erase(&system->get_meta());
                    prom->set_value(std::move(result.value()));
                    try {
                        if (config.on_finish) config.on_finish();
                    } catch (...) {}
                    while (!waiting_system_callers.empty()) {
                        auto& caller = waiting_system_callers.front();
                        if (caller()) {
                            waiting_system_callers.pop_front();
                        } else {
                            break;
                            // break here to reserve the order of systems.
                        }
                    }
                    m_system_cv.notify_all();
                });
            return true;
        };
        std::unique_lock lock(m_system_mutex);
        if (!try_call()) {
            waiting_system_callers.emplace_back(std::move(try_call));
        }
        return ft;
    }
    template <typename T>
    std::optional<std::expected<T, RunSystemError>> try_run_system(
        BasicSystem<T>* system) {
        std::unique_lock lock(m_system_mutex);
        for (auto&& sys : running_systems) {
            if (SystemMeta::conflict(*system, *sys)) {
                return std::nullopt;
            }
        }
        // run in the caller thread, so executor is ignored
        return system->run(*world);
    }
    /**
     * @brief Try run the providing systems in the current thread.
     *
     * The provided systems will only be run if all of them do not conflict
     * with any running system.
     *
     * @tparam Systems A viewable range of BasicSystem pointers.
     * @param systems A range of systems to run.
     * @return std::optional<std::vector<std::expected<T, RunSystemError>>> A
     * vector of results for each system, or std::nullopt if any system
     * conflicts with any running system.
     */
    template <typename Systems>
        requires std::ranges::range<Systems> && requires(Systems systems) {
            epix::util::type_traits::specialization_of<
                std::decay_t<decltype(*systems.begin())>, BasicSystem>;
        }
    auto try_run_multi(Systems&& systems) {
        using system_t =
            std::remove_pointer_t<std::decay_t<decltype(*systems.begin())>>;
        using ret = typename system_t::return_type;
        std::unique_lock lock(m_system_mutex);
        for (auto&& sys : running_systems) {
            for (auto&& system : systems) {
                if (SystemMeta::conflict(system->get_meta(), *sys)) {
                    return std::optional<
                        std::vector<std::expected<ret, RunSystemError>>>();
                }
            }
        }
        std::vector<std::expected<ret, RunSystemError>> results;
        results.reserve(systems.size());
        for (auto&& system : systems) {
            results.emplace_back(system->run(*world));
        }
        return std::make_optional(std::move(results));
    }
};
struct RunState {
   private:
    std::shared_ptr<RunStateData> data;

   public:
    using RunSystemConfig = RunStateData::RunSystemConfig;

    EPIX_API RunState(async::RwLock<World>::WriteGuard&& world,
                      Executors& executors);

    EPIX_API void apply_commands();
    EPIX_API bool wait();

    template <typename ret>
    std::expected<std::future<std::expected<ret, RunSystemError>>,
                  EnqueueSystemError>
    run_system(BasicSystem<ret>* system,
               const RunStateData::RunSystemConfig& config) {
        return data->run_system(system, config);
    }
    template <typename T>
    std::optional<std::expected<T, RunSystemError>> try_run_system(
        BasicSystem<T>* system) {
        return data->try_run_system(system);
    }
    template <typename Systems>
        requires std::ranges::range<Systems> && requires(Systems systems) {
            epix::util::type_traits::specialization_of<
                std::decay_t<decltype(*systems.begin())>, BasicSystem>;
        }
    auto try_run_multi(Systems&& systems) {
        return data->try_run_multi(std::forward<Systems>(systems));
    }
};
}  // namespace epix::app