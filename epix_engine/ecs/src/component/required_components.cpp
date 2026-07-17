#include <algorithm>
#include <epix/ecs/component/components.hpp>
#include <epix/ecs/component/components_impl.hpp>
#include <format>
#include <stdexcept>

namespace epix::ecs {
namespace {

auto find_required(RequiredComponents::Container& components, TypeId id) {
    return std::ranges::find(components, id, &RequiredComponents::Entry::first);
}

auto find_required(const RequiredComponents::Container& components, TypeId id) {
    return std::ranges::find(components, id, &RequiredComponents::Entry::first);
}

bool contains(const std::vector<TypeId>& ids, TypeId id) { return std::ranges::find(ids, id) != ids.end(); }

void insert_unique(std::vector<TypeId>& ids, TypeId id) {
    if (!contains(ids, id)) ids.push_back(id);
}

std::string component_name(const Components& components, TypeId id) {
    return components.get_index(id)
        .transform([](const meta::type_index& index) { return std::string(index.short_name()); })
        .value_or(std::format("component#{}", id.get()));
}

}  // namespace

bool RequiredComponents::directly_requires(TypeId id) const noexcept {
    return find_required(direct, id) != direct.end();
}

void RequiredComponentConstructor::initialize(
    Table& table, SparseSets& sparse_sets, Tick tick, TableRow row, Entity entity) const {
    (*function_)(table, sparse_sets, tick, row, entity);
}

bool RequiredComponents::contains(TypeId id) const noexcept { return find_required(all, id) != all.end(); }

const RequiredComponent* RequiredComponents::get(TypeId id) const noexcept {
    if (auto it = find_required(all, id); it != all.end()) return &it->second;
    return nullptr;
}

void RequiredComponents::register_dynamic(TypeId component_id,
                                          const Components& components,
                                          RequiredComponentConstructor constructor) {
    if (directly_requires(component_id)) {
        throw std::logic_error(std::format("component {} is already directly required", component_id.get()));
    }

    RequiredComponent required_component{.constructor = std::move(constructor)};
    direct.emplace_back(component_id, required_component);
    register_inherited_required_components(all, component_id, std::move(required_component), components);
}

void RequiredComponents::rebuild_inherited_required_components(const Components& components) {
    all.clear();
    for (const auto& [required_id, required_component] : direct) {
        register_inherited_required_components(all, required_id, required_component, components);
    }
}

void RequiredComponents::register_inherited_required_components(Container& all,
                                                                TypeId required_id,
                                                                RequiredComponent required_component,
                                                                const Components& components) {
    const auto required_info = components.get_required_components(required_id);
    if (!required_info) throw std::logic_error("required component has not been registered");

    if (find_required(all, required_id) == all.end()) {
        for (const auto& [inherited_id, inherited_required] : required_info->get().all) {
            if (find_required(all, inherited_id) == all.end()) all.emplace_back(inherited_id, inherited_required);
        }
    }

    if (auto existing = find_required(all, required_id); existing != all.end()) {
        existing->second = std::move(required_component);
    } else {
        all.emplace_back(required_id, std::move(required_component));
    }
}

std::string RequiredComponentsError::message(const Components& components) const {
    switch (kind) {
        case RequiredComponentsErrorKind::DuplicateRegistration:
            return std::format("{} already directly requires {}", component_name(components, requiree),
                               component_name(components, required));
        case RequiredComponentsErrorKind::CyclicRequirement:
            return std::format("{} cannot require {} because it would create a cycle",
                               component_name(components, requiree), component_name(components, required));
        case RequiredComponentsErrorKind::ArchetypeExists:
            return std::format("{} already exists in an archetype", component_name(components, requiree));
    }
    std::unreachable();
}

void RequiredComponentsRegistrator::register_required_dynamic(TypeId component_id,
                                                              RequiredComponentConstructor constructor) {
    if (!static_cast<const Components&>(*components_).is_valid(component_id)) {
        throw std::logic_error("required component has not been registered");
    }
    required_components_->register_dynamic(component_id, static_cast<const Components&>(*components_),
                                           std::move(constructor));
}

void Components::register_required_by(TypeId requiree, const RequiredComponents& required_components) {
    for (TypeId required : required_components.iter_ids()) {
        auto required_by = get_required_by_mut(required);
        if (!required_by) throw std::logic_error("required component has not been registered");
        insert_unique(required_by->get(), requiree);
    }
}

std::expected<void, RequiredComponentsError> Components::register_required_components(
    TypeId requiree, TypeId required, RequiredComponentConstructor constructor) {
    if (!is_valid(requiree) || !is_valid(required)) {
        throw std::logic_error("both components must be registered before adding a required-component relationship");
    }

    const auto required_required_components = get_required_components(required)->get();
    if (required_required_components.contains(requiree)) {
        return std::unexpected(
            RequiredComponentsError{RequiredComponentsErrorKind::CyclicRequirement, requiree, required});
    }

    auto& requiree_required_components = get_required_components_mut(requiree)->get();
    if (requiree_required_components.directly_requires(required)) {
        return std::unexpected(
            RequiredComponentsError{RequiredComponentsErrorKind::DuplicateRegistration, requiree, required});
    }

    const std::size_t old_required_count = requiree_required_components.all.size();
    requiree_required_components.register_dynamic(required, *this, std::move(constructor));

    std::vector<TypeId> newly_required;
    for (const auto& [id, _] : std::views::drop(requiree_required_components.all, old_required_count)) {
        newly_required.push_back(id);
    }

    std::vector<TypeId> affected_requirees{requiree};
    for (TypeId id : get_required_by(requiree)->get()) insert_unique(affected_requirees, id);

    for (TypeId indirect_requiree : std::views::drop(affected_requirees, 1)) {
        get_required_components_mut(indirect_requiree)->get().rebuild_inherited_required_components(*this);
    }

    for (TypeId indirect_required : newly_required) {
        auto& required_by = get_required_by_mut(indirect_required)->get();
        std::erase_if(required_by, [&](TypeId id) { return contains(affected_requirees, id); });
        for (TypeId id : affected_requirees) required_by.push_back(id);
    }

    return {};
}

}  // namespace epix::ecs
