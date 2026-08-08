#include <spdlog/spdlog.h>

#include <epix/ecs/world.hpp>
#include <vector>

namespace epix::ecs {
namespace {
bool is_entity_resource_component(const World& world, TypeId resource_id) {
    auto marker_id = world.components().get_valid_id<IsResource>();
    return marker_id &&
           world.components()
               .get_required_components(resource_id)
               .transform([&](const RequiredComponents& required) { return required.contains(*marker_id); })
               .value_or(false);
}
}  // namespace

void IsResource::on_insert(World& world, HookContext context) {
    auto marker = world.entity(context.entity).get<IsResource>();
    if (!marker) return;
    TypeId resource_id = marker->get().resource_component_id();
    if (!is_entity_resource_component(world, resource_id)) {
        spdlog::warn("[ecs] IsResource on entity {} references component {}, which is not a movable resource.",
                     context.entity.index, resource_id.get());
        return;
    }
    auto existing = world.resource_entities().get(resource_id);
    if (!existing) {
        world._resource_entities.insert(resource_id, context.entity);
    } else if (*existing != context.entity) {
        spdlog::warn("[ecs] Tried to insert resource component {} on entity {} while canonical entity {} exists.",
                     resource_id.get(), context.entity.index, existing->index);
    }
}

void IsResource::on_remove(World& world, HookContext context) {
    auto marker = world.entity(context.entity).get<IsResource>();
    if (!marker) return;
    TypeId resource_id = marker->get().resource_component_id();
    if (world.resource_entities().get(resource_id) == context.entity) {
        world._resource_entities.remove(resource_id);
    }
}

void IsResource::on_despawn(World&, HookContext context) {
    spdlog::warn("[ecs] Resource entity {} was despawned; resource entities are intended to remain stable.",
                 context.entity.index);
}

void World::reconcile_resource_entity(Entity entity) {
    auto entity_ref = get_entity(entity);
    if (!entity_ref) return;
    auto marker = entity_ref->get<IsResource>();
    if (!marker) return;

    TypeId resource_id = marker->get().resource_component_id();
    if (!is_entity_resource_component(*this, resource_id)) {
        auto marker_id = _components.get_valid_id<IsResource>();
        auto invalid   = get_entity_mut(entity);
        if (marker_id && invalid && invalid->contains_id(*marker_id)) invalid->remove_by_id(*marker_id);
        return;
    }
    auto canonical = _resource_entities.get(resource_id);
    if (!canonical) {
        _resource_entities.insert(resource_id, entity);
        return;
    }
    if (*canonical == entity) return;

    if (!get_entity(*canonical)) {
        _resource_entities.remove(resource_id);
        _resource_entities.insert(resource_id, entity);
        return;
    }

    spdlog::warn("[ecs] Removing duplicate resource component {} and IsResource marker from entity {}.",
                 resource_id.get(), entity.index);
    auto duplicate = get_entity_mut(entity);
    if (!duplicate) return;
    if (duplicate->contains_id(resource_id)) duplicate->remove_by_id(resource_id);
    auto marker_id = _components.get_valid_id<IsResource>();
    if (marker_id && duplicate->contains_id(*marker_id)) duplicate->remove_by_id(*marker_id);
}

bool World::remove_resource_by_id(TypeId type_id) {
    if (auto entity = _resource_entities.get(type_id)) {
        auto entity_mut = get_entity_mut(*entity);
        if (entity_mut && entity_mut->contains_id(type_id)) {
            entity_mut->remove_by_id(type_id);
            return true;
        }
        return false;
    }
    return _storage.resources.get_mut(type_id)
        .transform([](ResourceData& resource) {
            if (!resource.is_present()) return false;
            resource.remove();
            return true;
        })
        .value_or(false);
}

void World::clear_resources() {
    std::vector<std::pair<TypeId, Entity>> movable_resources;
    movable_resources.reserve(_resource_entities.size());
    for (auto&& [id, entity] : _resource_entities.iter()) {
        movable_resources.emplace_back(TypeId(id), entity);
    }
    for (auto [id, entity] : movable_resources) {
        auto entity_mut = get_entity_mut(entity);
        if (entity_mut && entity_mut->contains_id(id)) entity_mut->remove_by_id(id);
    }
    _storage.resources.clear();
}

void World::clear_entities() {
    flush();

    std::vector<Entity> ordinary_entities;
    ordinary_entities.reserve(_entities.used_count());
    for (std::uint32_t index = 0; index < _entities.total_count(); ++index) {
        auto entity = _entities.resolve_index(index);
        if (!entity || !_entities.contains(*entity)) continue;
        auto entity_ref = get_entity(*entity);
        if (entity_ref && !entity_ref->contains<IsResource>()) ordinary_entities.push_back(*entity);
    }

    for (Entity entity : ordinary_entities) {
        if (auto entity_mut = get_entity_mut(entity)) entity_mut->despawn();
    }
}

void World::flush_components() { registrator().apply_queued_registrations(); }

void World::flush_entities() {
    auto& empty_archetype = _archetypes.get_empty_mut();
    auto& empty_table     = _storage.tables.get_mut(empty_archetype.table_id()).value().get();
    _entities.flush([&](Entity entity, EntityLocation& location) {
        location = empty_archetype.allocate(entity, empty_table.allocate(entity));
    });
}

void World::flush_commands() { _command_queue.apply(*this); }

void World::flush() {
    flush_components();
    flush_entities();
    flush_commands();
}

namespace internal {

WorldId world_id(const World& world) noexcept { return world.id(); }
const Components& world_components(const World& world) noexcept { return world.components(); }
Components& world_components_mut(World& world) noexcept { return world.components_mut(); }
ComponentsRegistrator world_registrator(World& world) noexcept { return world.registrator(); }
ComponentsQueuedRegistrator world_queued_registrator(World& world) noexcept { return world.queued_registrator(); }
const Entities& world_entities(const World& world) noexcept { return world.entities(); }
Entities& world_entities_mut(World& world) noexcept { return world.entities_mut(); }
const Storage& world_storage(const World& world) noexcept { return world.storage(); }
Storage& world_storage_mut(World& world) noexcept { return world.storage_mut(); }
std::optional<Entity> world_resource_entity(const World& world, TypeId resource_id) noexcept {
    return world.resource_entity_by_id(resource_id);
}
void world_reconcile_resource_entity(World& world, Entity entity) { world.reconcile_resource_entity(entity); }
const Archetypes& world_archetypes(const World& world) noexcept { return world.archetypes(); }
Archetypes& world_archetypes_mut(World& world) noexcept { return world.archetypes_mut(); }
const Bundles& world_bundles(const World& world) noexcept { return world.bundles(); }
Bundles& world_bundles_mut(World& world) noexcept { return world.bundles_mut(); }
CommandQueue& world_command_queue(World& world) noexcept { return world.command_queue(); }
Tick world_change_tick(const World& world) noexcept { return world.change_tick(); }
Tick world_increment_change_tick(World& world) noexcept { return world.increment_change_tick(); }
Tick world_last_change_tick(const World& world) noexcept { return world.last_change_tick(); }
void world_flush_components(World& world) { world.flush_components(); }
void world_flush_entities(World& world) { world.flush_entities(); }
void world_flush_commands(World& world) { world.flush_commands(); }
void world_flush(World& world) { world.flush(); }

WorldId world_id(const DeferredWorld& world) noexcept { return world.id(); }
const Entities& world_entities(const DeferredWorld& world) noexcept { return world.entities(); }
const Storage& world_storage(const DeferredWorld& world) noexcept { return world.storage(); }
const Components& world_components(const DeferredWorld& world) noexcept { return world.components(); }
ComponentsQueuedRegistrator world_queued_registrator(const DeferredWorld& world) noexcept {
    return world.queued_registrator();
}
const Archetypes& world_archetypes(const DeferredWorld& world) noexcept { return world.archetypes(); }
const Bundles& world_bundles(const DeferredWorld& world) noexcept { return world.bundles(); }
CommandQueue& world_command_queue(DeferredWorld& world) noexcept { return world.command_queue(); }
Tick world_change_tick(const DeferredWorld& world) noexcept { return world.change_tick(); }
Tick world_last_change_tick(const DeferredWorld& world) noexcept { return world.last_change_tick(); }

}  // namespace internal
}  // namespace epix::ecs
