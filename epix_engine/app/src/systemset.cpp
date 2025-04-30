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