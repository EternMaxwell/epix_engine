#include <iostream>

#include "epix/app/schedule.h"

using namespace epix::app;

EPIX_API Executors::Executors() {
    // Default executor pool
    pools.emplace(ExecutorLabel(), std::make_unique<executor_t>(4));
    pools.emplace(
        ExecutorLabel(ExecutorType::SingleThread),
        std::make_unique<executor_t>(1)
    );
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
    pools.emplace(label, std::make_unique<executor_t>(count));
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
                it->second.conditions = std::move(config.conditions);
                it->second.executor   = config.executor;
                it->second.logger     = logger;
            }
            // Adding the system set that owns the system
            {
                auto&& it =
                    system_sets.emplace(config.label, SystemSet{}).first;
                std::swap(it->second.in_sets, config.in_sets);
                std::swap(it->second.depends, config.depends);
                std::swap(it->second.succeeds, config.succeeds);
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
    const SystemLabel& label
) {
    // to be implemented

    auto& system = schedule.systems.at(label);

    systems_running.emplace(label);
    if (executors) {
        if (auto executor = executors->get_pool(system.executor)) {
            executor->detach_task([this, &system]() {
                system.run(*src, *dst);
                just_finished_sets.emplace(system.label);
            });
            return {};
        }
        system.run(*src, *dst);
        just_finished_sets.emplace(label);
        return std::unexpected<RunSystemError>(
            std::in_place, label, RunSystemError::Type::ExecutorNotFound
        );
    }
    system.run(*src, *dst);
    just_finished_sets.emplace(label);
    return std::unexpected<RunSystemError>(
        std::in_place, label, RunSystemError::Type::NoExecutorsProvided
    );
};

EPIX_API void ScheduleRunner::enter_waiting_queue() {
    bool new_entered = true;
    while (new_entered) {
        new_entered = false;
        size_t size = wait_to_enter_queue.size();
        for (size_t i = 0; i < size; i++) {
            SystemSetLabel label = wait_to_enter_queue.front();
            wait_to_enter_queue.pop_front();

            // check if this set has all parents entered
            bool parent_all_entered = true;
            for (auto&& parent : schedule.system_sets[label].in_sets) {
                if (!entered_sets.contains(parent)) {
                    parent_all_entered = false;
                    break;
                }
            }
            if (parent_all_entered) {
                // all parents entered, enter the set
                entered_sets.emplace(label);
                new_entered = true;
                if (auto&& sys_it = schedule.systems.find(label);
                    sys_it != schedule.systems.end()) {
                    // this set also owns a system, push it to waiting queue
                    waiting_systems.emplace_back(label);
                }
            } else {
                // still not all parents entered, wait to enter
                wait_to_enter_queue.emplace_back(label);
            }
        }
    }
}

EPIX_API void ScheduleRunner::try_run_waiting_systems() {
    size_t size = waiting_systems.size();
    for (size_t i = 0; i < size; i++) {
        auto&& label = waiting_systems.front();
        waiting_systems.pop_front();
        auto&& sys                 = schedule.systems.at(label);
        bool conflict_with_running = false;
        for (auto&& running : systems_running) {
            auto&& other_sys = schedule.systems.at(running);
            if (sys.conflict_with(other_sys)) {
                conflict_with_running = true;
                break;
            }
        }
        if (!conflict_with_running) {
            run_system(label);
        } else {
            waiting_systems.emplace_back(label);
        }
    }
}

EPIX_API std::expected<void, RunScheduleError> ScheduleRunner::run() {
    // Locking systems and system sets to avoid adding or removing systems or
    // system sets while running
    schedule.logger->trace("Flushing command queue.");
    schedule.flush();
    schedule.logger->trace("Command queue flushed.");

    std::unique_lock lock(schedule.system_sets_mutex);

    if (!src || !dst) {
        return std::unexpected(RunScheduleError{
            schedule.label, RunScheduleError::Type::WorldsNotSet
        });
    }

    schedule.logger->trace("Building schedule.");
    schedule.build();
    schedule.logger->trace("Schedule built.");

    schedule.logger->debug("Running schedule.");

    for (auto&& [label, set] : schedule.system_sets) {
        set_depends_count.emplace(label, set.depends.size());
        if (set.depends.empty()) {
            wait_to_enter_queue.emplace_back(label);
        }
        for (auto&& parent : set.in_sets) {
            auto&& it = set_children_count.find(parent);
            if (it == set_children_count.end()) {
                set_children_count.emplace(parent, 0);
            }
            set_children_count.at(parent)++;
        }
    }
    for (auto&& [label, system] : schedule.systems) {
        auto&& it = set_children_count.find(label);
        if (it == set_children_count.end()) {
            set_children_count.emplace(label, 0);
        }
        set_children_count.at(label)++;
    }

    enter_waiting_queue();
    try_run_waiting_systems();

    // we check if the just finished sets is not empty or if there are still
    // systems running, if so, we need to check if there are any finished sets.
    // Cause only these 2 cases can ensure that just_finished_sets.pop() can
    // return or return later.
    while (!just_finished_sets.empty() || systems_running.size() > 0) {
        auto finished_item = just_finished_sets.pop();
        {
            // finish the set owns the system if children count is 0
            auto& child_count = set_children_count.at(finished_item);
            if (schedule.systems.contains(finished_item)) {
                child_count--;
            }
            if (child_count == 0) {
                // all children are
                entered_sets.erase(finished_item);
                finished_sets.emplace(finished_item);
            }
        }
        systems_running.erase(finished_item);
        {
            auto&& set = schedule.system_sets.at(finished_item);
            // checking all succeeds of the finished system
            for (auto&& succeed : set.succeeds) {
                auto& depend_count = set_depends_count.at(succeed);
                depend_count--;
                if (depend_count == 0) {
                    // all dependencies are finished, enter the set
                    wait_to_enter_queue.emplace_back(succeed);
                }
            }

            // checking all parents of the finished system
            // note that parents are always entered before the child, so
            // they just need to be finished if needed
            for (auto&& parent : set.in_sets) {
                auto& child_count = set_children_count.at(parent);
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

    schedule.logger->debug("Schedule finished.");

    uint32_t remaining_sets =
        schedule.system_sets.size() - finished_sets.size();
    if (remaining_sets > 0) {
        return std::unexpected(RunScheduleError{
            schedule.label, RunScheduleError::Type::SetsRemaining,
            remaining_sets
        });
    }
    return {};
}

EPIX_API void ScheduleRunner::reset() noexcept {
    wait_to_enter_queue.clear();
    entered_sets.clear();
    finished_sets.clear();
    set_children_count.clear();
    set_depends_count.clear();
    systems_running.clear();
    waiting_systems.clear();
    while (auto&& it = just_finished_sets.try_pop()) {}
}