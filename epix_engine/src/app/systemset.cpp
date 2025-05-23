#include "epix/app/schedule.h"

using namespace epix::app;

EPIX_API SystemSetConfig& SystemSetConfig::after_internal(
    const SystemSetLabel& label
) noexcept {
    if (!in_sets.contains(label) && !succeeds.contains(label)) {
        depends.emplace(label);
    }
    return *this;
}
EPIX_API SystemSetConfig& SystemSetConfig::before_internal(
    const SystemSetLabel& label
) noexcept {
    if (!in_sets.contains(label) && !depends.contains(label)) {
        succeeds.emplace(label);
    }
    return *this;
}
EPIX_API SystemSetConfig& SystemSetConfig::in_set_internal(
    const SystemSetLabel& label
) noexcept {
    if (!depends.contains(label) && !succeeds.contains(label)) {
        in_sets.emplace(label);
    }
    return *this;
}
EPIX_API SystemSetConfig& SystemSetConfig::after_config(SystemSetConfig& config
) noexcept {
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
EPIX_API bool SystemSet::conflict_with(const System& system) noexcept {
    auto& system_label = system.label;
    if (auto&& it = conflicts.find(system_label); it != conflicts.end()) {
        return it->second;
    }
    if (auto&& it = conflicts_dyn.find(system_label);
        it != conflicts_dyn.end()) {
        return it->second;
    }
    bool result = false;
    for (auto&& condition : conditions) {
        if (condition.conflict_with(*system.system)) {
            result = true;
            break;
        }
    }
    if (system_label.get_type() == typeid(void)) {
        if (conflicts_dyn.size() >= max_conflict_cache) {
            // erase a random one
            auto it = conflicts_dyn.begin();
            static thread_local std::mt19937 rng{};
            static thread_local std::uniform_int_distribution<size_t> dist(
                0, max_conflict_cache - 1
            );
            std::advance(it, dist(rng));
            conflicts_dyn.erase(it);
        }
        conflicts_dyn.emplace(system_label, result);
    } else {
        conflicts.emplace(system_label, result);
    }
    return false;
}