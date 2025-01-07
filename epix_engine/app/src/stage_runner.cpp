#include <format>

#include "epix/app.h"

using namespace epix::app;

EPIX_API StageRunner::StageRunner(
    std::type_index stage,
    SubApp* src,
    SubApp* dst,
    WorkerPool* pools,
    SetMap* sets
)
    : m_src(src), m_dst(dst), m_pools(pools), m_sets(sets), m_stage(stage) {
    m_logger =
        spdlog::default_logger()->clone("stage: " + std::string(stage.name()));
}

EPIX_API void StageRunner::set_log_level(spdlog::level::level_enum level) {
    m_logger->set_level(level);
    for (auto& [sub_stage, sub_stage_runner] : m_sub_stages) {
        sub_stage_runner->set_log_level(level);
    }
}

EPIX_API bool StageRunner::conflict(const StageRunner* other) const {
    return m_src == other->m_src || m_dst == other->m_dst ||
           m_src == other->m_dst || m_dst == other->m_src;
}

EPIX_API void StageRunner::build() {
    for (auto& [sub_stage, sub_stage_runner] : m_sub_stages) {
        sub_stage_runner->build();
    }
}

EPIX_API void StageRunner::bake() {
    for (auto& [sub_stage, sub_stage_runner] : m_sub_stages) {
        sub_stage_runner->bake();
    }
}

EPIX_API void StageRunner::run() {
    for (auto sub_stage : m_sub_stage_order) {
        auto name = std::format("sub stage: {}-{}", m_stage.name(), sub_stage);
        ZoneTransientN(zone, name.c_str(), true);
        m_sub_stages[sub_stage]->run();
    }
}