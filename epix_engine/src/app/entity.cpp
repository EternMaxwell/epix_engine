#include "epix/app/entity.h"

using namespace epix::app;

EPIX_API Entity::Entity(entt::entity id) noexcept : id(id) {}
EPIX_API Entity::Entity() noexcept : id(entt::null) {}
EPIX_API Entity::operator entt::entity() const noexcept { return id; }
EPIX_API Entity::operator bool() const noexcept { return id != entt::null; }
EPIX_API Entity& Entity::operator=(entt::entity id) noexcept {
    this->id = id;
    return *this;
}
EPIX_API bool Entity::operator!() const noexcept { return id == entt::null; }
EPIX_API bool Entity::operator==(const Entity& other) const noexcept {
    return id == other.id;
}
EPIX_API bool Entity::operator!=(const Entity& other) const noexcept {
    return id != other.id;
}
EPIX_API bool Entity::operator==(const entt::entity& other) const noexcept {
    return id == other;
}
EPIX_API bool Entity::operator!=(const entt::entity& other) const noexcept {
    return id != other;
}
EPIX_API size_t Entity::index() const noexcept { return entt::to_integral(id); }
EPIX_API size_t Entity::hash_code() const noexcept {
    return std::hash<entt::entity>{}(id);
}