#include <epix/ecs/archetype/archetype.hpp>
#include <epix/ecs/component/components.hpp>
#include <epix/ecs/component/register.hpp>
#include <epix/ecs/component/required_component.hpp>
#include <epix/ecs/storage/resource.hpp>

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
void ComponentsRegistrator::configure_resource_component(TypeId resource_id) {
    TypeId marker_id = register_component<IsResource>();
    if (resource_id == marker_id) return;

    bool has_marker = m_components->get_required_components(resource_id)
                          .transform([&](const RequiredComponents& required) { return required.contains(marker_id); })
                          .value_or(false);
    if (has_marker) return;

    if (m_archetypes->by_component.contains(resource_id)) {
        throw std::logic_error(
            std::format("Cannot register component {} as a resource after it has already been inserted on an entity.",
                        m_components->get_index(resource_id)
                            .transform([](meta::type_index type) { return type.name(); })
                            .value_or("<unknown>")));
    }

    auto result = m_components->register_required_components<IsResource>(
        resource_id, marker_id, [resource_id] { return IsResource(resource_id); });
    if (!result && result.error().kind != RequiredComponentsErrorKind::DuplicateRegistration) {
        throw std::logic_error(result.error().message(*m_components));
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
    RequiredComponents required_components{};
    RequiredComponentsRegistrator required_components_registrator(*this, required_components);
    register_required_components(id, required_components_registrator);
    m_components->register_required_by(id, required_components);
    m_recurse_stack.pop_back();

    auto& info = m_components->m_infos[id.get()].value();
    update_from_component(info._hooks);

    info._required_components = std::move(required_components);
}
}  // namespace epix::ecs
