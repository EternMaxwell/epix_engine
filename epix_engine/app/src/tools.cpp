#include "epix/app.h"

using namespace epix::app;
using namespace epix::app_tools;

EPIX_API Label::Label(std::type_index t, size_t i) noexcept
    : type(t), index(i) {}
EPIX_API Label::Label() noexcept : type(typeid(void)), index(0) {}
EPIX_API bool Label::operator==(const Label& other) const noexcept {
    return type == other.type && index == other.index;
}
EPIX_API bool Label::operator!=(const Label& other) const noexcept {
    return !(*this == other);
}
EPIX_API void Label::set_type(std::type_index t) noexcept { type = t; }
EPIX_API void Label::set_index(size_t i) noexcept { index = i; }
EPIX_API size_t Label::hash_code() const noexcept {
    size_t seed = type.hash_code();
    seed ^= std::hash<size_t>()(index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}
EPIX_API std::string Label::name() const noexcept {
    return std::format("{}#{}", type.name(), index);
}

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
EPIX_API size_t Entity::index() const { return static_cast<size_t>(id); }
EPIX_API size_t Entity::hash_code() const {
    return std::hash<entt::entity>()(id);
}
EPIX_API size_t std::hash<epix::app::Entity>::operator()(const Entity& entity
) const {
    return entity.hash_code();
}