#include "epix/app/schedule.h"

using namespace epix::app;

EPIX_API SystemSetConfig& SystemSetConfig::after_internal(const SystemSetLabel& label) noexcept {
    if (!in_sets.contains(label) && !succeeds.contains(label)) {
        depends.emplace(label);
    }
    for (auto&& sub_config : sub_configs) {
        sub_config.after_internal(label);
    }
    return *this;
}
EPIX_API SystemSetConfig& SystemSetConfig::before_internal(const SystemSetLabel& label) noexcept {
    if (!in_sets.contains(label) && !depends.contains(label)) {
        succeeds.emplace(label);
    }
    for (auto&& sub_config : sub_configs) {
        sub_config.before_internal(label);
    }
    return *this;
}
EPIX_API SystemSetConfig& SystemSetConfig::in_set_internal(const SystemSetLabel& label) noexcept {
    if (!depends.contains(label) && !succeeds.contains(label)) {
        in_sets.emplace(label);
    }
    for (auto&& sub_config : sub_configs) {
        sub_config.in_set_internal(label);
    }
    return *this;
}
EPIX_API SystemSetConfig& SystemSetConfig::after_config(SystemSetConfig& config) noexcept {
    if (label) {
        config.before_internal(*label);
    }
    for (auto&& sub_config : sub_configs) {
        sub_config.after_config(config);
    }
    return *this;
}
EPIX_API SystemSetConfig& SystemSetConfig::chain() noexcept {
    for (size_t i = 0; i < sub_configs.size() - 1; i++) {
        sub_configs[i + 1].after_config(sub_configs[i]);
    }
    return *this;
}

EPIX_API SystemSetConfig& SystemSetConfig::set_executor(const ExecutorLabel& executor) noexcept {
    this->executor = executor;
    for (size_t i = 0; i < sub_configs.size(); i++) {
        sub_configs[i].set_executor(executor);
    }
    return *this;
}
EPIX_API SystemSetConfig& SystemSetConfig::set_name(const std::string& name) noexcept {
    this->name = name;
    for (size_t i = 0; i < sub_configs.size(); i++) {
        sub_configs[i].set_name(std::format("{}#{}", name, i));
    }
    return *this;
}
EPIX_API SystemSetConfig& SystemSetConfig::set_name(size_t index, const std::string& name) noexcept {
    sub_configs[index].set_name(name);
    return *this;
}
EPIX_API SystemSetConfig& SystemSetConfig::set_names(epix::util::ArrayProxy<std::string> names) noexcept {
    this->name = names.size() > 0 ? *(names.begin()) : std::string{};
    for (size_t i = 0; i < sub_configs.size() && i < names.size(); i++) {
        sub_configs[i].set_name(*(names.begin() + i));
    }
    return *this;
}

EPIX_API bool SystemSet::conflict_with(const SystemSet& system) noexcept {
    auto& system_label = system.label;
    if (auto&& it = conflicts.find(system_label); it != conflicts.end()) {
        return it->second;
    }
    if (auto&& it = conflicts_dyn.find(system_label); it != conflicts_dyn.end()) {
        return it->second;
    }
    bool result = false;
    for (auto&& condition : conditions) {
        if (SystemMeta::conflict(condition->get_meta(), (*system.system).get_meta())) {
            result = true;
            break;
        }
    }
    if (system_label.get_type() == meta::type_id<void>{}) {
        if (conflicts_dyn.size() >= max_conflict_cache) {
            // erase a random one
            auto it = conflicts_dyn.begin();
            static thread_local std::mt19937 rng{};
            static thread_local std::uniform_int_distribution<size_t> dist(0, max_conflict_cache - 1);
            std::advance(it, dist(rng));
            conflicts_dyn.erase(it->first);
        }
        conflicts_dyn.emplace(system_label, result);
    } else {
        conflicts.emplace(system_label, result);
    }
    return false;
}
EPIX_API void SystemSet::detach(const SystemSetLabel& label) noexcept {
    built_in_sets.erase(label);
    built_depends.erase(label);
    built_succeeds.erase(label);
}
EPIX_API bool SystemSet::empty() const noexcept {
    return system == nullptr && conditions.empty() && in_sets.empty() && depends.empty() && succeeds.empty();
}