#include "epix/app.h"

using namespace epix::app;

EPIX_API bool SystemSet::operator==(const SystemSet& other) const {
    return type == other.type && value == other.value;
}
EPIX_API bool SystemSet::operator!=(const SystemSet& other) const {
    return type != other.type || value != other.value;
}

EPIX_API SystemAddInfo::each_t::each_t(
    const std::string& name,
    FuncIndex index,
    std::unique_ptr<BasicSystem<void>>&& system
)
    : name(name), index(index), system(std::move(system)) {}
EPIX_API SystemAddInfo::each_t::each_t(each_t&& other)
    : name(other.name),
      index(other.index),
      system(std::move(other.system)),
      conditions(std::move(other.conditions)),
      m_in_sets(std::move(other.m_in_sets)),
      m_worker(std::move(other.m_worker)),
      m_ptr_prevs(std::move(other.m_ptr_prevs)),
      m_ptr_nexts(std::move(other.m_ptr_nexts)) {}
EPIX_API SystemAddInfo::each_t& SystemAddInfo::each_t::operator=(each_t&& other
) {
    name        = other.name;
    index       = other.index;
    system      = std::move(other.system);
    conditions  = std::move(other.conditions);
    m_in_sets   = std::move(other.m_in_sets);
    m_worker    = std::move(other.m_worker);
    m_ptr_prevs = std::move(other.m_ptr_prevs);
    m_ptr_nexts = std::move(other.m_ptr_nexts);
    return *this;
}

EPIX_API SystemAddInfo& SystemAddInfo::chain() {
    m_chain = true;
    return *this;
}

EPIX_API SystemAddInfo& SystemAddInfo::worker(const std::string& worker) {
    for (auto& each : m_systems) {
        each.m_worker = worker;
    }
    return *this;
}

EPIX_API SystemAddInfo& SystemAddInfo::set_label(const std::string& label) {
    if (m_systems.size() == 1) {
        m_systems[0].name = label;
    } else {
        for (size_t i = 0; i < m_systems.size(); ++i) {
            m_systems[i].name = std::format("{}#{}", label, i);
        }
    }
    return *this;
}

EPIX_API SystemAddInfo& SystemAddInfo::set_label(
    uint32_t index, const std::string& label
) {
    if (index >= m_systems.size()) return *this;
    m_systems[index].name = label;
    return *this;
}

EPIX_API SystemAddInfo& SystemAddInfo::set_labels(
    index::ArrayProxy<std::string> labels
) {
    auto&& in_begin  = labels.begin();
    auto&& in_end    = labels.end();
    auto&& out_begin = m_systems.begin();
    auto&& out_end   = m_systems.end();
    while (in_begin != in_end && out_begin != out_end) {
        out_begin->name = *in_begin;
        ++in_begin;
        ++out_begin;
    }
    return *this;
}

EPIX_API SystemAddInfo& SystemAddInfo::set_labels(
    index::ArrayProxy<const char*> labels
) {
    auto&& in_begin  = labels.begin();
    auto&& in_end    = labels.end();
    auto&& out_begin = m_systems.begin();
    auto&& out_end   = m_systems.end();
    while (in_begin != in_end && out_begin != out_end) {
        out_begin->name = *in_begin;
        ++in_begin;
        ++out_begin;
    }
    return *this;
}

EPIX_API System::System(
    const std::string& label,
    FuncIndex index,
    std::unique_ptr<BasicSystem<void>>&& system
)
    : label(label),
      index(index),
      system(std::move(system)),
      m_prev_count(0),
      m_next_count(0) {}

EPIX_API bool System::run(World* src, World* dst) {
    ZoneScopedN("Try Run System");
    auto name = std::format("System: {}", label);
    ZoneName(name.c_str(), name.size());
    {
        ZoneScopedN("Check Conditions");
        for (auto& each : conditions) {
            if (!each->run(src, dst)) return false;
        }
    }
    {
        ZoneScopedN("Run System");
        system->run(src, dst);
    }
    return true;
}
EPIX_API void System::clear_tmp() {
    m_tmp_prevs.clear();
    m_tmp_nexts.clear();
    m_prev_count = 0;
    m_next_count = 0;
    m_reach_time.reset();
}
EPIX_API double System::reach_time() {
    if (m_reach_time.has_value()) return m_reach_time.value();
    m_reach_time = 0.0;
    for (auto& each : m_prevs) {
        if (auto ptr = each.lock()) {
            m_reach_time = std::max(
                m_reach_time.value(),
                ptr->reach_time() + ptr->system->get_avg_time()
            );
        } else {
            m_prevs.erase(each);
        }
    }
    return m_reach_time.value();
}

template <>
EPIX_API SystemAddInfo epix::app::into(SystemAddInfo&& info) {
    return std::move(info);
}