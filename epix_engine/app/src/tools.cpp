#include "epix/app.h"

using namespace epix::app;
using namespace epix::app_tools;

EPIX_API Entity& Entity::operator=(entt::entity id) {
    this->id = id;
    return *this;
}
EPIX_API Entity::operator entt::entity() { return id; }
EPIX_API Entity::operator bool() { return id != entt::null; }
EPIX_API bool Entity::operator!() { return id == entt::null; }
EPIX_API bool Entity::operator==(const Entity& other) { return id == other.id; }
EPIX_API bool Entity::operator!=(const Entity& other) { return id != other.id; }
EPIX_API bool Entity::operator==(const entt::entity& other) {
    return id == other;
}
EPIX_API bool Entity::operator!=(const entt::entity& other) {
    return id != other;
}

EPIX_API bool FuncIndex::operator==(const FuncIndex& other) const {
    return type == other.type && func == other.func;
}

EPIX_API size_t std::hash<FuncIndex>::operator()(const FuncIndex& func) const {
    size_t type_hash = func.type.hash_code();
    size_t func_hash = std::hash<void*>()(func.func);
    return type_hash ^
           (func_hash + 0x9e3779b9 + (type_hash << 6) + (type_hash >> 2));
}

EPIX_API size_t std::hash<Entity>::operator()(const Entity& entity) const {
    return std::hash<entt::entity>()(entity.id);
}

EPIX_API bool std::equal_to<Entity>::operator()(
    const Entity& a, const Entity& b
) const {
    return a.id == b.id;
}

EPIX_API size_t std::hash<ScheduleId>::operator()(const ScheduleId& id) const {
    return std::hash<std::type_index>()(id.type) ^
           std::hash<size_t>()(id.value);
}