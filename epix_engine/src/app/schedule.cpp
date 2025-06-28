#include "epix/app/schedule.h"

#include "epix/app/profiler.h"

using namespace epix::app;

EPIX_API void AddSystemsCommand::apply(Schedule& schedule) {
    schedule.add_systems(config);
}
EPIX_API void ConfigureSetsCommand::apply(Schedule& schedule) {
    schedule.configure_sets(config);
}
EPIX_API void RemoveSetCommand::apply(Schedule& schedule) {
    schedule.remove_set(label);
}
EPIX_API void RemoveSystemCommand::apply(Schedule& schedule) {
    schedule.remove_system(label);
}

EPIX_API Schedule::Schedule(const ScheduleLabel& label) {
    data  = std::make_unique<ScheduleData>(label);
    cache = std::make_unique<async::RwLock<ScheduleCache>>();
}

EPIX_API ScheduleData::ScheduleData(const ScheduleLabel& label)
    : label(label) {}

EPIX_API ScheduleLabel Schedule::label() const noexcept { return data->label; }

EPIX_API void Schedule::initialize_systems(World& world) {
    // Initialize the systems in the world
    auto write = data->system_sets.write();
    if (last_world != &world) {
        auto& system_sets = *write;
        for (auto&& [label, set] : system_sets) {
            if (set.system) {
                set.system->initialize(world);
            }
        }
    } else {
        // only initialize the systems that are not initialized yet
        for (auto&& [label, set] : *write) {
            if (set.system && !set.system->initialized()) {
                set.system->initialize(world);
            }
        }
    }
    last_world = &world;
}

EPIX_API bool Schedule::build_sets() noexcept {
    // This function completes the set dependency
    auto psystem_sets       = data->system_sets.write();
    auto& system_sets_write = *psystem_sets;
    bool any                = false;
    while (auto newly_added = data->newly_added_sets.try_pop()) {
        auto it = system_sets_write.find(*newly_added);
        if (it == system_sets_write.end()) continue;

        any         = true;
        auto& set   = it->second;
        auto& label = *newly_added;
        set.built_in_sets.clear();
        set.built_depends.clear();
        set.built_succeeds.clear();
        // add succeed on this set to the depend set
        for (auto&& depend : set.depends) {
            auto&& it = system_sets_write.find(depend);
            if (it != system_sets_write.end()) {
                it->second.built_succeeds.emplace(label);
                set.built_depends.emplace(depend);
            }
        }
        // add depend on this set to the succeed set
        for (auto&& succeed : set.succeeds) {
            auto&& it = system_sets_write.find(succeed);
            if (it != system_sets_write.end()) {
                it->second.built_depends.emplace(label);
                set.built_succeeds.emplace(succeed);
            }
        }
        // remove not present in_sets from the in_sets
        for (auto&& in_set : set.in_sets) {
            auto&& it = system_sets_write.find(in_set);
            if (it != system_sets_write.end()) {
                set.built_in_sets.emplace(in_set);
            }
        }
        for (auto&& [other_label, other_set] : system_sets_write) {
            // If the other set has the label in its in_sets, depends or
            // succeeds, these are dependencies that are required before this
            // set is added.
            if (other_set.in_sets.contains(label)) {
                other_set.built_in_sets.emplace(label);
            }
            if (other_set.depends.contains(label)) {
                other_set.built_depends.emplace(label);
            }
            if (other_set.succeeds.contains(label)) {
                other_set.built_succeeds.emplace(label);
            }
        }
    }
    return any;
};

EPIX_API void Schedule::add_systems(SystemSetConfig&& config) {
    // Try lock, if failed, that means the schedule is currently being run
    if (auto pwrite = data->system_sets.try_write()) {
        auto& system_sets = **pwrite;
        // Not running, add directly.
        if (config.system && config.label &&
            !(system_sets.contains(*config.label) &&
              system_sets[*config.label].system)) {
            // Adding the system
            auto it = system_sets.find(*config.label);
            if (it == system_sets.end()) {
                it = system_sets.emplace(*config.label, SystemSet{}).first;
            }
            auto& set      = it->second;
            set.label      = config.label;
            set.executor   = config.executor;
            set.name       = std::move(config.name);
            set.system     = std::move(config.system);
            set.conditions = std::move(config.conditions);
            set.in_sets    = std::move(config.in_sets);
            set.depends    = std::move(config.depends);
            set.succeeds   = std::move(config.succeeds);
            data->newly_added_sets.emplace(*config.label);
        }
    } else {
        // Is running, add to the queue
        data->command_queue.enqueue<AddSystemsCommand>(std::move(config));
        return;
    }
    for (auto&& sub_config : config.sub_configs) {
        add_systems(std::move(sub_config));
    }
}
EPIX_API void Schedule::add_systems(SystemSetConfig& config) {
    add_systems(std::move(config));
}
EPIX_API void Schedule::configure_sets(const SystemSetConfig& config) {
    if (auto pwrite = data->system_sets.try_write()) {
        auto& system_sets = **pwrite;
        if (config.label && !system_sets.contains(*config.label)) {
            auto&& it = system_sets.emplace(*config.label, SystemSet{}).first;
            it->second.in_sets  = config.in_sets;
            it->second.depends  = config.depends;
            it->second.succeeds = config.succeeds;
            for (auto&& cond : config.conditions) {
                it->second.conditions.emplace_back(cond->clone_unique());
            }
            data->newly_added_sets.emplace(*config.label);
        }
    } else {
        data->command_queue.enqueue<ConfigureSetsCommand>(std::move(config));
        return;
    }
    for (auto&& sub_config : config.sub_configs) {
        configure_sets(std::move(sub_config));
    }
};
EPIX_API void Schedule::remove_system(const SystemSetLabel& label) {
    // removing system wont remove the set the label also points to,
    // cause other sets or systems may have dependencies on it.
    // The next time the system added, the previous depencies will be
    // removed and new dependencies will also be accepted.
    if (auto pwrite = data->system_sets.try_write()) {
        auto& system_sets = *pwrite;
        // remove the system with the label
        if (auto it = system_sets->find(label); it != system_sets->end()) {
            auto& set = it->second;
            if (set.system) {
                set.system.reset();
            }
        }
        // remove the label from depends, succeeds and in_sets of all
        // other system sets
        for (auto&& [other_label, other_set] : *system_sets) {
            other_set.detach(label);
        }
    } else {
        data->command_queue.enqueue<RemoveSystemCommand>(label);
    }
};
EPIX_API void Schedule::remove_set(const SystemSetLabel& label) {
    if (auto pwrite = data->system_sets.try_write()) {
        auto& system_sets = *pwrite;
        // remove the system set with the label
        system_sets->erase(label);
        // remove the label from depends, succeeds and in_sets of all
        // other system sets
        for (auto&& [other_label, other_set] : *system_sets) {
            other_set.detach(label);
        }
    } else {
        data->command_queue.enqueue<RemoveSetCommand>(label);
    }
};
EPIX_API bool Schedule::contains_system(const SystemSetLabel& label
) const noexcept {
    auto read = data->system_sets.read();
    if (auto it = read->find(label); it != read->end()) {
        return it->second.system != nullptr;
    }
    return false;
};
EPIX_API bool Schedule::contains_set(const SystemSetLabel& label
) const noexcept {
    auto read = data->system_sets.read();
    return read->contains(label);
};
EPIX_API bool Schedule::flush_cmd() noexcept {
    return data->command_queue.apply(*this);
};

EPIX_API void Schedule::update_cache(
    entt::dense_map<SystemSetLabel, SystemSet>& system_sets,
    ScheduleCache& cache
) noexcept {
    // This function updates the cache of the schedule
    cache.set_index_map.clear();
    cache.system_set_infos.clear();
    cache.system_set_infos.reserve(system_sets.size());
    cache.set_index_map.reserve(system_sets.size());
    for (auto&& [label, set] : system_sets) {
        uint32_t index = (uint32_t)cache.system_set_infos.size();
        cache.set_index_map.emplace(label, index);
        auto& info = cache.system_set_infos.emplace_back();
        info.label = label;
        info.set   = &set;
        info.parents.reserve(set.built_in_sets.size());
        info.succeeds.reserve(set.built_succeeds.size());
        info.cached_children_count = set.system ? 1 : 0;
        info.cached_depends_count  = set.built_depends.size();
    }
    for (auto&& [label, set] : system_sets) {
        auto& info = cache.system_set_infos[cache.set_index_map[label]];
        for (auto&& parent : set.built_in_sets) {
            auto index = cache.set_index_map.at(parent);
            info.parents.emplace_back(index);
            cache.system_set_infos[index].cached_children_count++;
        }
        for (auto&& succeed : set.built_succeeds) {
            info.succeeds.emplace_back(cache.set_index_map.at(succeed));
        }
    }
};

EPIX_API std::expected<void, RunScheduleError> Schedule::run_internal(
    RunState& run_state
) noexcept {
    Config run_config        = config;
    bool should_update_cache = flush_cmd();
    should_update_cache |= build_sets();
    auto write = async::scoped_write(data->system_sets, *cache);
    auto&& [system_sets, cache0] = *write;
    if (should_update_cache) {
        update_cache(system_sets, cache0);
    }
    // rebind, fix bug of intellisense
    ScheduleCache& cache = cache0;

    // run needed data
    std::deque<uint32_t>
        queue_wait_to_enter;  // not entered set and was checking
    std::deque<std::pair<uint32_t, bool>>
        waiting_sets;  // check done and should enter
    async::ConQueue<uint32_t> just_finished_sets;  // just finished sets
    std::vector<std::expected<
        std::future<std::expected<void, RunSystemError>>, EnqueueSystemError>>
        futures(cache.system_set_infos.size());
    bool new_entered = false;
    size_t running   = 0;

    // lambdas
    auto run_system = [&](uint32_t index) {
        auto& info  = cache.system_set_infos[index];
        auto& label = info.label;
        running++;
        auto res = run_state.run_system(
            info.set->system.get(),
            RunState::RunSystemConfig{
                .on_finish = [&just_finished_sets,
                              index]() { just_finished_sets.emplace(index); },
                .executor  = info.set->executor,
            }
        );
        if (!res) {
            // detach system failed, mark as finished
            just_finished_sets.emplace(index);
        }
        futures[index] = std::move(res);
    };
    auto enter_waiting = [&]() {
        size_t size = waiting_sets.size();
        for (size_t i = 0; i < size; i++) {
            auto index       = waiting_sets.front().first;
            bool parent_pass = waiting_sets.front().second;
            waiting_sets.pop_front();
            auto& info = cache.system_set_infos[index];
            if (!parent_pass) {
                info.entered = true;
                info.passed  = false;
                new_entered  = true;
                if (info.set->system || info.children_count == 0) {
                    just_finished_sets.emplace(index);
                }
            }
            if (auto res = run_state.try_run_multi(
                    std::views::all(info.set->conditions) |
                    std::views::transform([](auto& cond) { return cond.get(); })
                )) {
                bool pass    = std::ranges::all_of(res.value(), [](auto&& r) {
                    return r.value_or(false);
                });
                info.entered = true;
                info.passed  = pass;
                new_entered  = true;
                if (info.set->system) {
                    if (pass) {
                        run_system(index);
                    } else {
                        just_finished_sets.emplace(index);
                    }
                } else if (info.children_count == 0) {
                    just_finished_sets.emplace(index);
                }
            } else {
                waiting_sets.emplace_back(index, parent_pass);
            }
        }
    };
    auto try_queued = [&]() {
        do {
            new_entered = false;
            size_t size = queue_wait_to_enter.size();
            for (size_t i = 0; i < size; i++) {
                uint32_t index = queue_wait_to_enter.front();
                queue_wait_to_enter.pop_front();
                auto& info          = cache.system_set_infos[index];
                bool parent_entered = true;
                bool parent_passed  = true;
                for (auto&& parent : info.parents) {
                    parent_entered &= cache.system_set_infos[parent].entered;
                    if (!parent_entered) break;
                    parent_passed &= cache.system_set_infos[parent].passed;
                }
                if (parent_entered) {
                    // all parents entered, try enter the set
                    waiting_sets.emplace_back(index, parent_passed);
                } else {
                    // still not all parents entered, wait to enter
                    queue_wait_to_enter.emplace_back(index);
                }
            }
            enter_waiting();
        } while (new_entered);
    };

    // prepare run
    for (uint32_t index = 0; index < cache.system_set_infos.size(); index++) {
        auto& info         = cache.system_set_infos[index];
        info.depends_count = info.set->built_depends.size();
        if (info.depends_count == 0) {
            queue_wait_to_enter.emplace_back(index);
        }
        info.entered        = false;
        info.passed         = false;
        info.finished       = false;
        info.children_count = info.cached_children_count;
    }

    try_queued();
    while (!just_finished_sets.empty() || running > 0) {
        uint32_t finished = just_finished_sets.pop();
        auto& info        = cache.system_set_infos[finished];
        {
            auto& res = futures[finished];
            if (!res) {
                spdlog::error(
                    "Enqueue system failed, no required executor found. "
                    "System:{}, Executor:{}, Schedule:{}",
                    info.label.name(), info.set->executor.name(),
                    data->label.name()
                );
            } else if (auto& value = res.value(); value.valid()) {
                auto sys_ret = value.get();
                if (!sys_ret) {
                    std::visit(
                        epix::util::visitor{
                            [&](const NotInitializedError& e) {
                                spdlog::error(
                                    "System not initialized. System:{}, "
                                    "required arg states:{}",
                                    info.label.name(), e.needed_state.name()
                                );
                            },
                            [&](const UpdateStateFailedError& e) {
                                spdlog::error(
                                    "Update args states failed. System:{}, "
                                    "failed args:{}",
                                    info.label.name(),
                                    std::views::all(e.failed_args) |
                                        std::views::transform([](auto&& type) {
                                            return type.name();
                                        })
                                );
                            },
                            [&](SystemExceptionError& e) {
                                try {
                                    std::rethrow_exception(e.exception);
                                } catch (const std::exception& ex) {
                                    spdlog::error(
                                        "System exception. System:{}, "
                                        "Exception:{}",
                                        info.label.name(), ex.what()
                                    );
                                } catch (...) {
                                    spdlog::error(
                                        "System exception. System:{}, "
                                        "Exception:unknown",
                                        info.label.name()
                                    );
                                }
                            },
                            [](auto&&) {}
                        },
                        sys_ret.error()
                    );
                }
            }
        }
        {
            if (info.set->system) {
                info.children_count--;
                if (info.passed) running--;
            }
            if (info.children_count == 0) {
                info.entered  = false;
                info.finished = true;
            }
        }
        {
            for (auto&& succeed : info.succeeds) {
                auto& succeed_info = cache.system_set_infos[succeed];
                succeed_info.depends_count--;
                if (succeed_info.depends_count == 0) {
                    queue_wait_to_enter.emplace_back(succeed);
                }
            }
            for (auto&& parent : info.parents) {
                auto& parent_info = cache.system_set_infos[parent];
                parent_info.children_count--;
                if (parent_info.children_count == 0) {
                    just_finished_sets.emplace(parent);
                }
            }
        }
        try_queued();
    }

    run_state.apply_commands();

    if (run_config.run_once) {
        for (auto&& [label, set] : system_sets) {
            remove_system(label);
        }
    }

    uint32_t remaining_sets = 0;
    for (auto&& info : cache.system_set_infos) {
        if (!info.finished) {
            remaining_sets++;
            spdlog::warn(
                "\t{}:{} not finished.", info.set->system ? "System" : "Set",
                info.label.name()
            );
        }
    }
    if (remaining_sets > 0) {
        spdlog::warn(
            "{} sets/systems remaining, in schedule:{}(full name:{})",
            remaining_sets, data->label.name(), data->label.name()
        );
        return std::unexpected(RunScheduleError{
            data->label, RunScheduleError::Type::SetsRemaining, remaining_sets
        });
    }
    return {};
}

EPIX_API std::future<std::expected<void, RunScheduleError>> Schedule::run(
    RunState& run_state
) noexcept {
    return std::async(
        std::launch::async,
        [this, &run_state]() -> std::expected<void, RunScheduleError> {
            return run_internal(run_state);
        }
    );
}