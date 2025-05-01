#include "epix/app/schedule.h"

using namespace epix::app;

EPIX_API System::System(
    const SystemLabel& label,
    const std::string& name,
    std::unique_ptr<BasicSystem<void>>&& system
)
    : label(label), name(name), system(std::move(system)) {}
EPIX_API System::System(
    const SystemLabel& label, std::unique_ptr<BasicSystem<void>>&& system
)
    : System(label, label.name(), std::move(system)) {}

EPIX_API bool System::conflict_with(const System& other) noexcept {
    if (other.label.get_type() == typeid(void)) {
        auto&& it = conflicts_dyn.find(other.label);
        if (it != conflicts_dyn.end()) {
            return it->second;
        }
    } else {
        auto&& it = conflicts.find(other.label);
        if (it != conflicts.end()) {
            return it->second;
        }
    }
    bool result = false;
    if (system->conflict_with(*other.system)) {
        result = true;
    }
    // for (const auto& condition : conditions) {
    //     if (result) break;
    //     for (const auto& other_condition : other.conditions) {
    //         if (condition->conflict_with(*other_condition)) {
    //             result = true;
    //             break;
    //         }
    //     }
    // }
    // for (const auto& condition : other.conditions) {
    //     if (result) break;
    //     if (system->conflict_with(*condition)) {
    //         result = true;
    //         break;
    //     }
    // }
    // for (const auto& other_condition : other.conditions) {
    //     if (result) break;
    //     if (other.system->conflict_with(*other_condition)) {
    //         result = true;
    //         break;
    //     }
    // }
    if (other.label.get_type() == typeid(void)) {
        if (conflicts_dyn.size() >= max_conflict_cache) {
            // erase a random one
            auto it = conflicts_dyn.begin();
            static thread_local std::mt19937 rng{};
            static thread_local std::uniform_int_distribution<size_t> dist(
                0, max_conflict_cache - 1
            );
            std::advance(it, dist(rng) % conflicts_dyn.size());
            conflicts_dyn.erase(it);
        }
        conflicts_dyn.emplace(other.label, result);
    } else {
        conflicts.emplace(other.label, result);
    }
    return result;
}

#include <iostream>

EPIX_API void System::run(World& src, World& dst) noexcept {
    auto start = std::chrono::high_resolution_clock::now();
    try {
        system->run(src, dst);
    } catch (const BadParamAccess& e) {
        logger->info("BadParamAccess at {}: {}", name, e.what());
    } catch (const std::exception& e) {
        logger->info("Exception at {}: {}", name, e.what());
    } catch (...) {
        logger->info("Unknown exception at {}", name);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto delta =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
            end - start
        )
            .count();
    time_cost = delta;
    time_avg  = factor * delta + (1.0 - factor) * time_avg;
}

EPIX_API SystemConfig& SystemConfig::after_internal(const SystemSetLabel& label
) noexcept {
    if (!in_sets.contains(label) && !succeeds.contains(label)) {
        depends.emplace(label);
    }
    for (auto&& sub_config : sub_configs) {
        sub_config.after_internal(label);
    }
    return *this;
}
EPIX_API SystemConfig& SystemConfig::before_internal(const SystemSetLabel& label
) noexcept {
    if (!in_sets.contains(label) && !depends.contains(label)) {
        succeeds.emplace(label);
    }
    for (auto&& sub_config : sub_configs) {
        sub_config.before_internal(label);
    }
    return *this;
}
EPIX_API SystemConfig& SystemConfig::in_set_internal(const SystemSetLabel& label
) noexcept {
    if (!depends.contains(label) && !succeeds.contains(label)) {
        in_sets.emplace(label);
    }
    for (auto&& sub_config : sub_configs) {
        sub_config.in_set_internal(label);
    }
    return *this;
}
EPIX_API SystemConfig& SystemConfig::after_config(SystemConfig& config
) noexcept {
    if (system) {
        config.before_internal(label);
    }
    for (auto&& sub_config : sub_configs) {
        sub_config.after_config(config);
    }
    return *this;
}
EPIX_API SystemConfig& SystemConfig::chain() noexcept {
    for (size_t i = 0; i < sub_configs.size() - 1; i++) {
        sub_configs[i + 1].after_config(sub_configs[i]);
    }
    return *this;
}
EPIX_API SystemConfig& SystemConfig::set_executor(const ExecutorLabel& executor
) noexcept {
    this->executor = executor;
    for (size_t i = 0; i < sub_configs.size(); i++) {
        sub_configs[i].set_executor(executor);
    }
    return *this;
}
EPIX_API SystemConfig& SystemConfig::set_name(const std::string& name
) noexcept {
    this->name = name;
    for (size_t i = 0; i < sub_configs.size(); i++) {
        sub_configs[i].set_name(std::format("{}#{}", name, i));
    }
    return *this;
}
EPIX_API SystemConfig& SystemConfig::set_name(
    size_t index, const std::string& name
) noexcept {
    sub_configs[index].set_name(name);
    return *this;
}
EPIX_API SystemConfig& SystemConfig::set_names(
    epix::util::ArrayProxy<std::string> names
) noexcept {
    for (size_t i = 0; i < sub_configs.size() && i < names.size(); i++) {
        sub_configs[i].set_name(*(names.begin() + i));
    }
    return *this;
}