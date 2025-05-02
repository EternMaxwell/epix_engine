#include <iostream>

#include "epix/app/schedule.h"

using namespace epix::app;

EPIX_API Executors::Executors() {
    // Default executor pool
    // add_pool(ExecutorLabel(), "default", 4);
    // add_pool(ExecutorLabel(ExecutorType::SingleThread), "single", 1);
};

EPIX_API Executors::executor_t* Executors::get_pool(const ExecutorLabel& label
) noexcept {
    auto it = pools.find(label);
    if (it != pools.end()) {
        return it->second.get();
    }
    return nullptr;
};
EPIX_API void Executors::add_pool(
    const ExecutorLabel& label, size_t count
) noexcept {
    pools.emplace(label, std::make_unique<executor_t>(count, [label]() {
                      BS::this_thread::set_os_thread_name(label.name());
                  }));
};
EPIX_API void Executors::add_pool(
    const ExecutorLabel& label, const std::string& name, size_t count
) noexcept {
    pools.emplace(label, std::make_unique<executor_t>(count, [name]() {
                      BS::this_thread::set_os_thread_name(name);
                  }));
};

EPIX_API void ScheduleCommandQueue::flush(Schedule& schedule) {
    std::unique_lock lock(m_mutex);
    size_t pointer = 0;
    while (pointer < m_commands.size()) {
        auto index         = m_commands[pointer++];
        auto& command_data = m_registry[index];
        auto* pcommand     = reinterpret_cast<void*>(&m_commands[pointer]);
        command_data.apply(schedule, pcommand);
        command_data.destruct(pcommand);
        pointer += command_data.size;
    }
    m_commands.clear();
};

EPIX_API void AddSystemsCommand::apply(Schedule& schedule) {
    schedule.add_systems(config);
};
EPIX_API void ConfigureSetsCommand::apply(Schedule& schedule) {
    schedule.configure_sets(config);
};
EPIX_API void RemoveSetCommand::apply(Schedule& schedule) {
    schedule.remove_set(label);
};
EPIX_API void RemoveSystemCommand::apply(Schedule& schedule) {
    schedule.remove_system(label);
};

EPIX_API Schedule::Schedule(const ScheduleLabel& label) : label(label) {}
EPIX_API Schedule::Schedule(Schedule&& other)
    : label(other.label),
      systems(std::move(other.systems)),
      system_sets(std::move(other.system_sets)),
      newly_added_sets(std::move(other.newly_added_sets)) {
    other.systems.clear();
    other.system_sets.clear();
    other.newly_added_sets.clear();
};

EPIX_API void Schedule::set_logger(const std::shared_ptr<spdlog::logger>& logger
) {
    this->logger = logger->clone(std::format("schedule:{}", label.name()));
    for (auto&& [label, system] : systems) {
        system.logger = this->logger;
    }
};

EPIX_API void Schedule::build() noexcept {
    // This function completes the set dependency
    for (auto&& label : newly_added_sets) {
        auto& set = system_sets.at(label);
        std::vector<SystemSetLabel> not_presents;
        // add succeed on this set to the depend set
        for (auto&& depend : set.depends) {
            auto&& it = system_sets.find(depend);
            if (it != system_sets.end()) {
                it->second.succeeds.emplace(label);
            } else {
                not_presents.emplace_back(depend);
            }
        }
        for (auto&& not_present : not_presents) {
            set.depends.erase(not_present);
        }
        not_presents.clear();
        // add depend on this set to the succeed set
        for (auto&& succeed : set.succeeds) {
            auto&& it = system_sets.find(succeed);
            if (it != system_sets.end()) {
                it->second.depends.emplace(label);
            } else {
                not_presents.emplace_back(succeed);
            }
        }
        for (auto&& not_present : not_presents) {
            set.succeeds.erase(not_present);
        }
        not_presents.clear();
        // remove not present in_sets from the in_sets
        for (auto&& in_set : set.in_sets) {
            auto&& it = system_sets.find(in_set);
            if (it == system_sets.end()) {
                not_presents.emplace_back(in_set);
            }
        }
        for (auto&& not_present : not_presents) {
            set.in_sets.erase(not_present);
        }
        not_presents.clear();
    }
    newly_added_sets.clear();
};

EPIX_API void Schedule::add_systems(SystemConfig&& config) {
    // Try lock, if failed, that means the schedule is currently being run
    if (system_sets_mutex.try_lock()) {
        // Not running, add directly.
        if (config.system && !systems.contains(config.label) &&
            !system_sets.contains(config.label)) {
            // Adding the system
            {
                auto&& it = systems
                                .emplace(
                                    config.label, System(
                                                      config.label, config.name,
                                                      std::move(config.system)
                                                  )
                                )
                                .first;
                it->second.executor = config.executor;
                it->second.logger   = logger;
            }
            // Adding the system set that owns the system
            {
                auto&& it =
                    system_sets.emplace(config.label, SystemSet{}).first;
                std::swap(it->second.in_sets, config.in_sets);
                std::swap(it->second.depends, config.depends);
                std::swap(it->second.succeeds, config.succeeds);
                std::swap(it->second.conditions, config.conditions);
            }
            newly_added_sets.emplace(config.label);
        }
        system_sets_mutex.unlock();
        for (auto&& sub_config : config.sub_configs) {
            add_systems(std::move(sub_config));
        }
    } else {
        // Is running, add to the queue
        command_queue.enqueue<AddSystemsCommand>(std::move(config));
    }
}
EPIX_API void Schedule::add_systems(SystemConfig& config) {
    add_systems(std::move(config));
}
EPIX_API void Schedule::configure_sets(const SystemSetConfig& config) {
    if (system_sets_mutex.try_lock()) {
        if (config.label && !system_sets.contains(*config.label)) {
            auto&& it = system_sets.emplace(*config.label, SystemSet{}).first;
            it->second.in_sets  = config.in_sets;
            it->second.depends  = config.depends;
            it->second.succeeds = config.succeeds;
            newly_added_sets.emplace(*config.label);
        }
        system_sets_mutex.unlock();
        for (auto&& sub_config : config.sub_configs) {
            configure_sets(std::move(sub_config));
        }
    } else {
        command_queue.enqueue<ConfigureSetsCommand>(std::move(config));
    }
};
EPIX_API void Schedule::remove_system(const SystemLabel& label) {
    if (system_sets_mutex.try_lock()) {
        // remove the system with the label
        systems.erase(label);
        // remove the system set with the label
        system_sets.erase(label);
        // remove the label from depends, succeeds and in_sets of all
        // other system sets
        for (auto&& [other_label, other_set] : system_sets) {
            other_set.erase(label);
        }
        newly_added_sets.erase(label);
        system_sets_mutex.unlock();
    } else {
        command_queue.enqueue<RemoveSystemCommand>(label);
    }
};
EPIX_API void Schedule::remove_set(const SystemSetLabel& label) {
    if (system_sets_mutex.try_lock()) {
        // remove the system with the label, since if a system is not owned by a
        // system set, it will never be run.
        systems.erase(label);
        // remove the system set with the label
        system_sets.erase(label);
        // remove the label from depends, succeeds and in_sets of all
        // other system sets
        for (auto&& [other_label, other_set] : system_sets) {
            other_set.erase(label);
        }
        newly_added_sets.erase(label);
        system_sets_mutex.unlock();
    } else {
        command_queue.enqueue<RemoveSetCommand>(label);
    }
};
EPIX_API void Schedule::flush() noexcept { command_queue.flush(*this); };

EPIX_API ScheduleRunner::ScheduleRunner(Schedule& schedule, bool run_once)
    : schedule(schedule), run_once(run_once) {};

EPIX_API void ScheduleRunner::set_executors(
    const std::shared_ptr<Executors>& executors
) noexcept {
    std::unique_lock lock(schedule.system_sets_mutex);
    this->executors = executors;
};
EPIX_API void ScheduleRunner::set_worlds(World& src, World& dst) noexcept {
    std::unique_lock lock(schedule.system_sets_mutex);
    this->src = &src;
    this->dst = &dst;
};

EPIX_API std::expected<void, RunSystemError> ScheduleRunner::run_system(
    uint32_t index
) {
    // to be implemented

    auto& system = *system_set_infos[index].system;
    auto& label  = system.label;

    systems_running.emplace(index);
    if (executors) {
        if (auto executor = executors->get_pool(system.executor)) {
            executor->detach_task([this, &system, index]() {
                if (tracy_settings.enabled) {
                    ZoneScopedN("Run system");
                    auto name = std::format("Run System:{}", system.name);
                    ZoneName(name.c_str(), name.size());
                    system.run(*src, *dst);
                    just_finished_sets.emplace(index);
                } else {
                    system.run(*src, *dst);
                    just_finished_sets.emplace(index);
                }
            });
            return {};
        }
        system.run(*src, *dst);
        just_finished_sets.emplace(index);
        return std::unexpected<RunSystemError>(
            std::in_place, label, RunSystemError::Type::ExecutorNotFound
        );
    }
    system.run(*src, *dst);
    just_finished_sets.emplace(index);
    return std::unexpected<RunSystemError>(
        std::in_place, label, RunSystemError::Type::NoExecutorsProvided
    );
};

EPIX_API void ScheduleRunner::enter_waiting_queue() {
    do {
        new_entered = false;
        size_t size = wait_to_enter_queue.size();
        for (size_t i = 0; i < size; i++) {
            auto index = wait_to_enter_queue.front();
            wait_to_enter_queue.pop_front();
            auto&& info         = system_set_infos[index];
            bool parent_entered = true;
            bool parent_passed  = true;
            for (auto&& parent : info.parents) {
                parent_entered &= system_set_infos[parent].entered;
                if (!parent_entered) break;
                parent_passed &= system_set_infos[parent].passed;
            }
            if (parent_entered) {
                // all parents entered, try enter the set
                waiting_sets.emplace_back(index, parent_passed);
            } else {
                // still not all parents entered, wait to enter
                wait_to_enter_queue.emplace_back(index);
            }
        }
        try_enter_waiting_sets();
    } while (new_entered);
}

EPIX_API void ScheduleRunner::try_enter_waiting_sets() {
    size_t size = waiting_sets.size();
    for (size_t i = 0; i < size; i++) {
        auto index       = waiting_sets.front().first;
        bool parent_pass = waiting_sets.front().second;
        waiting_sets.pop_front();
        auto& info                 = system_set_infos[index];
        bool conflict_with_running = false;
        for (auto&& running : systems_running) {
            auto& system = *system_set_infos[running].system;
            if (info.set->conflict_with(system)) {
                conflict_with_running = true;
                break;
            }
        }
        if (!conflict_with_running) {
            bool pass = parent_pass;
            if (pass) {
                for (auto&& condition : info.set->conditions) {
                    if (!condition->run(*src, *dst)) {
                        pass = false;
                        break;
                    }
                }
            }
            info.entered = true;
            info.passed  = pass;
            new_entered  = true;
            if (info.system) {
                if (pass) {
                    waiting_systems.emplace_back(index);
                } else {
                    just_finished_sets.emplace(index);
                };
            } else if (info.children_count == 0) {
                just_finished_sets.emplace(index);
            }
        } else {
            waiting_sets.emplace_back(index, parent_pass);
        }
    }
}

EPIX_API void ScheduleRunner::try_run_waiting_systems() {
    size_t size = waiting_systems.size();
    for (size_t i = 0; i < size; i++) {
        auto index = waiting_systems.front();
        waiting_systems.pop_front();
        auto& info                 = system_set_infos[index];
        bool conflict_with_running = false;
        for (auto&& running : systems_running) {
            auto& system = *system_set_infos[running].system;
            if (info.system->conflict_with(system)) {
                conflict_with_running = true;
                break;
            }
        }
        if (!conflict_with_running) {
            if (tracy_settings.enabled) {
                ZoneScopedN("Run or detach system");
                auto name =
                    std::format("Run or detach System:{}", info.system->name);
                ZoneName(name.c_str(), name.size());
                run_system(index);
            } else {
                run_system(index);
            }
        } else {
            waiting_systems.emplace_back(index);
        }
    }
}

EPIX_API void ScheduleRunner::prepare_schedule() {
    for (auto&& [label, set] : schedule.system_sets) {
        uint32_t index = (uint32_t)system_set_infos.size();
        set_index_map.emplace(label, index);
        auto& info = system_set_infos.emplace_back();
        info.label = label;
        info.set   = &set;
        if (auto&& it = schedule.systems.find(label);
            it != schedule.systems.end()) {
            info.system = &it->second;
        } else {
            info.system = nullptr;
        }
        info.parents.reserve(set.in_sets.size());
        info.succeeds.reserve(set.succeeds.size());
        info.depends_count = set.depends.size();
        if (info.depends_count == 0) {
            wait_to_enter_queue.emplace_back(index);
        }
        info.children_count = info.system ? 1 : 0;
        info.entered        = false;
        info.passed         = false;
        info.finished       = false;
    }
    for (auto&& [label, set] : schedule.system_sets) {
        auto& info = system_set_infos.at(set_index_map.at(label));
        for (auto&& parent : set.in_sets) {
            auto index = set_index_map.at(parent);
            info.parents.emplace_back(index);
            system_set_infos[index].children_count++;
        }
        for (auto&& succeed : set.succeeds) {
            info.succeeds.emplace_back(set_index_map.at(succeed));
        }
    }
}
EPIX_API void ScheduleRunner::run_loop() {
    // we check if the just finished sets is not empty or if there are still
    // systems running, if so, we need to check if there are any finished sets.
    // Cause only these 2 cases can ensure that just_finished_sets.pop() can
    // return or return later.
    while (!just_finished_sets.empty() || systems_running.size() > 0) {
        auto finished_item = just_finished_sets.pop();
        {
            auto& info = system_set_infos[finished_item];
            if (info.system) {
                info.children_count--;
            }
            if (info.children_count == 0) {
                info.entered  = false;
                info.finished = true;
            }
        }
        systems_running.erase(finished_item);
        {
            auto& info = system_set_infos[finished_item];
            for (auto&& succeed : info.succeeds) {
                system_set_infos[succeed].depends_count--;
                if (system_set_infos[succeed].depends_count == 0) {
                    // all dependencies are finished, enter the set
                    wait_to_enter_queue.emplace_back(succeed);
                }
            }

            for (auto&& parent : info.parents) {
                auto& child_count = system_set_infos[parent].children_count;
                child_count--;
                if (child_count == 0) {
                    // all children are finished, finish the parent
                    just_finished_sets.emplace(parent);
                }
            }
        }
        enter_waiting_queue();
        try_run_waiting_systems();
    }
}

EPIX_API void ScheduleRunner::finishing() {
    if (run_once) {
        // if run_once, clear all systems in the schedule
        // and all sets that own systems
        for (auto&& [label, set] : schedule.systems) {
            // directly call remove_system to remove the system since the
            // systems are locked, and this operation will be queued
            schedule.remove_system(label);
        }
        // removals will be actually done in the next flush
    }

    src->command_queue().flush(*src);
    dst->command_queue().flush(*dst);
}

EPIX_API std::expected<void, RunScheduleError> ScheduleRunner::run_internal() {
    // Locking systems and system sets to avoid adding or removing systems or
    // system sets while running

    schedule.logger->trace("Flushing command queue.");
    if (tracy_settings.enabled) {
        ZoneScopedN("Flush schedule command queue");
        schedule.flush();
    } else {
        schedule.flush();
    }
    schedule.logger->trace("Command queue flushed.");

    std::unique_lock lock(schedule.system_sets_mutex);

    if (!src || !dst) {
        return std::unexpected(RunScheduleError{
            schedule.label, RunScheduleError::Type::WorldsNotSet
        });
    }

    schedule.logger->trace("Building schedule.");
    if (tracy_settings.enabled) {
        ZoneScopedN("Build schedule if needed");
        schedule.build();
    } else {
        schedule.build();
    }
    schedule.logger->trace("Schedule built.");

    schedule.logger->debug("Running schedule.");

    if (tracy_settings.enabled) {
        ZoneScopedN("Prepare schedule for running");
        prepare_schedule();
    } else {
        prepare_schedule();
    }

    enter_waiting_queue();
    try_run_waiting_systems();

    run_loop();

    if (tracy_settings.enabled) {
        ZoneScopedN("Finishing schedule");
        finishing();
    } else {
        finishing();
    }

    schedule.logger->debug("Schedule finished.");

    uint32_t remaining_sets = 0;
    for (auto&& info : system_set_infos) {
        if (!info.finished) {
            remaining_sets++;
            schedule.logger->warn(
                "    Schedule {} has set {} not finished.",
                schedule.label.name(), info.label.name()
            );
        }
    }
    if (remaining_sets > 0) {
        schedule.logger->warn(
            "Schedule {} has {} sets remaining.", schedule.label.name(),
            remaining_sets
        );
        return std::unexpected(RunScheduleError{
            schedule.label, RunScheduleError::Type::SetsRemaining,
            remaining_sets
        });
    }
    return {};
}

EPIX_API std::expected<void, RunScheduleError> ScheduleRunner::run() {
    if (tracy_settings.enabled) {
        ZoneScopedN("Run schedule");
        auto name = std::format("Schedule:{}", schedule.label.name());
        ZoneName(name.c_str(), name.size());
        return run_internal();
    } else {
        return run_internal();
    }
}

EPIX_API void ScheduleRunner::reset() noexcept {
    wait_to_enter_queue.clear();
    systems_running.clear();
    new_entered = false;
    waiting_systems.clear();
    waiting_sets.clear();
    system_set_infos.clear();
    while (auto&& it = just_finished_sets.try_pop()) {}
}

EPIX_API ScheduleRunner::TracySettings& ScheduleRunner::get_tracy_settings(
) noexcept {
    return tracy_settings;
}