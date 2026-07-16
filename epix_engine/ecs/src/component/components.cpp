#include <epix/ecs/component/components.hpp>

namespace epix::ecs {

std::size_t Components::num_queued() const {
    std::shared_lock lock(*m_mutex);
    return m_queued.components.size();
}

std::size_t Components::num_registered() const { return m_infos.size(); }

std::size_t Components::length() const { return num_queued() + num_registered(); }

std::optional<std::reference_wrapper<const internal::ComponentInfo>> Components::get_info(TypeId id) const {
    if (id.get() >= m_infos.size()) return std::nullopt;
    return m_infos[id.get()].transform([](auto&& ref) { return std::cref(ref); });
}

std::optional<std::reference_wrapper<internal::ComponentInfo>> Components::get_info_mut(TypeId id) {
    if (id.get() >= m_infos.size()) return std::nullopt;
    return m_infos[id.get()].transform([](auto&& ref) { return std::ref(ref); });
}

std::optional<meta::type_index> Components::get_index(TypeId id) const {
    return get_info(id)
        .transform([](const internal::ComponentInfo& info) { return info.type_index(); })
        .or_else([this, id] -> std::optional<meta::type_index> {
            std::shared_lock lock(*m_mutex);
            auto it = std::ranges::find_if(m_queued.components, [id](auto&& item) { return item.second.id == id; });
            if (it != m_queued.components.end()) return it->second.type_index;
            return std::nullopt;
        });
}

bool Components::is_valid(TypeId id) const { return get_info(id).has_value(); }

void Components::register_component_inner(TypeId id, meta::type_index type, StorageType storage_type) {
    std::size_t cap = id.get() + 1;
    if (cap > m_infos.size()) m_infos.resize(cap);
    assert(!m_infos[id.get()].has_value());
    m_infos[id.get()].emplace(id, type, storage_type);
}

std::optional<std::reference_wrapper<const RequiredComponents>> Components::get_required_components(TypeId id) const {
    return get_info(id).transform(
        [](const internal::ComponentInfo& info) { return std::cref(info.required_components()); });
}

std::optional<std::reference_wrapper<RequiredComponents>> Components::get_required_components_mut(TypeId id) {
    return get_info_mut(id).transform(
        [](internal::ComponentInfo& info) { return std::ref(info.required_components_mut()); });
}

std::optional<std::reference_wrapper<const std::vector<TypeId>>> Components::get_required_by(TypeId id) const {
    return get_info(id).transform([](const internal::ComponentInfo& info) { return std::cref(info.required_by()); });
}

std::optional<std::reference_wrapper<std::vector<TypeId>>> Components::get_required_by_mut(TypeId id) {
    return get_info_mut(id).transform([](internal::ComponentInfo& info) { return std::ref(info.required_by_mut()); });
}

}  // namespace epix::ecs
