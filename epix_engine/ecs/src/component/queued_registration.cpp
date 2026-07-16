#include <epix/ecs/component/components.hpp>
#include <epix/ecs/component/register.hpp>

namespace epix::ecs {

TypeId ComponentsQueuedRegistrator::register_arbitrary_component(
    meta::type_index type_index,
    StorageType storage_type,
    void (*func)(ComponentsRegistrator&, TypeId, meta::type_index, StorageType)) const {
    std::unique_lock lock(*m_components->m_mutex);
    auto id = m_ids->next();
    m_components->m_queued.components.insert_or_assign(type_index,
                                                       QueuedRegistration(id, type_index, storage_type, func));
    return id;
}

void ComponentsRegistrator::apply_queued_registrations() {
    if (m_components->num_queued() == 0) return;
    while (auto registrator = [this] -> std::optional<QueuedRegistration> {
        std::unique_lock lock(*m_components->m_mutex);
        auto it = m_components->m_queued.components.begin();
        if (it == m_components->m_queued.components.end()) return std::nullopt;
        auto type_index        = it->first;
        QueuedRegistration reg = std::move(it->second);
        m_components->m_queued.components.erase(type_index);
        return reg;
    }()) {
        registrator->register_component(*this);
    }
}

}  // namespace epix::ecs
