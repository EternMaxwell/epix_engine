#include <epix/ecs/component/components.hpp>
#include <epix/ecs/component/register.hpp>
#include <epix/ecs/component/required_component.hpp>

namespace {
using namespace epix::ecs;
void enforce_no_required_components_recursion(Components& components, std::span<const TypeId> stack, TypeId required) {
    if (auto direct_recursion = [&] -> std::optional<bool> {
            auto it = std::ranges::find(stack, required);
            if (it == stack.end()) return std::nullopt;
            return stack.end() == std::next(it);
        }()) {
        throw std::runtime_error(std::format(
            "Recursive required components detected: {}\nhelp: {}",
            stack | std::views::transform([&](TypeId id) { return components.get_index(id).value().short_name(); }) |
                std::views::join_with(std::string_view(" -> ")) | std::ranges::to<std::string>(),
            [&, direct = *direct_recursion] {
                if (!direct) return std::string("If intentional, consider merging the components.");
                return std::format("Remove require({}).", components.get_index(required).value().short_name());
            }()));
    }
}
}  // namespace

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

TypeId ComponentsRegistrator::register_resource_checked(meta::type_index type_index, StorageType storage_type) {
    if (auto id = m_components->get_id(type_index)) {
        return *id;
    }

    // if this is already queued, we respect the queue.
    if (auto registrator = [&] -> std::optional<QueuedRegistration> {
            std::unique_lock lock(*m_components->m_mutex);  // do we actually need to lock here?
            auto it = m_components->m_queued.components.find(type_index);
            if (it == m_components->m_queued.components.end()) return std::nullopt;
            QueuedRegistration res = std::move(it->second);
            m_components->m_queued.components.erase(type_index);
            return res;
        }()) {
        return registrator->register_component(*this);
    }

    TypeId id = m_ids->next();
    register_resource_unchecked(type_index, id, storage_type);
    return id;
}
void ComponentsRegistrator::register_resource_unchecked(meta::type_index type_index,
                                                        TypeId id,
                                                        StorageType storage_type) {
    m_components->register_component_inner(id, type_index, storage_type);
    bool inserted = m_components->m_type_ids.insert_or_assign(type_index, id).second;
    assert(inserted);
}
TypeId ComponentsRegistrator::register_component_checked(
    meta::type_index type_index,
    StorageType storage_type,
    void (*register_required_components)(TypeId, RequiredComponentsRegistrator&),
    ComponentHooks& (*update_from_component)(ComponentHooks&)) {
    if (auto id = m_components->get_id(type_index)) {
        enforce_no_required_components_recursion(*m_components, m_recurse_stack, *id);
        return *id;
    }

    // if this is already queued, we respect the queue.
    if (auto registrator = [&] -> std::optional<QueuedRegistration> {
            std::unique_lock lock(*m_components->m_mutex);  // do we actually need to lock here?
            auto it = m_components->m_queued.components.find(type_index);
            if (it == m_components->m_queued.components.end()) return std::nullopt;
            QueuedRegistration res = std::move(it->second);
            m_components->m_queued.components.erase(type_index);
            return res;
        }()) {
        return registrator->register_component(*this);
    }

    TypeId id = m_ids->next();
    register_component_unchecked(type_index, id, storage_type, register_required_components, update_from_component);
    return id;
}
void ComponentsRegistrator::register_component_unchecked(
    meta::type_index type_index,
    TypeId id,
    StorageType storage_type,
    void (*register_required_components)(TypeId, RequiredComponentsRegistrator&),
    ComponentHooks& (*update_from_component)(ComponentHooks&)) {
    m_components->register_component_inner(id, type_index, storage_type);
    bool inserted = m_components->m_type_ids.insert_or_assign(type_index, id).second;
    assert(inserted);

    // recursive register for required components
    m_recurse_stack.push_back(id);
    internal::RequiredComponents required_components{};
    RequiredComponentsRegistrator required_comoponents_registrator(*this, required_components);
    register_required_components(id, required_comoponents_registrator);
    m_components->register_required_by(id, required_components);
    m_recurse_stack.pop_back();

    auto& info = m_components->m_infos[id.get()].value();
    update_from_component(info._hooks);

    info._required_components = std::move(required_components);
}
}  // namespace epix::ecs