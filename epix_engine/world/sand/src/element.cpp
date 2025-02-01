#include "epix/world/sand/components.h"

using namespace epix::world::sand::components;

EPIX_API Element::Element(const std::string& name, GravType type)
    : name(name), grav_type(type) {}
EPIX_API Element Element::solid(const std::string& name) {
    return Element(name, GravType::SOLID);
}
EPIX_API Element Element::liquid(const std::string& name) {
    return Element(name, GravType::LIQUID);
}
EPIX_API Element Element::powder(const std::string& name) {
    return Element(name, GravType::POWDER);
}
EPIX_API Element Element::gas(const std::string& name) {
    return Element(name, GravType::GAS);
}
EPIX_API Element& Element::set_grav_type(GravType type) {
    grav_type = type;
    return *this;
}
EPIX_API Element& Element::set_density(float density) {
    this->density = density;
    return *this;
}
EPIX_API Element& Element::set_bouncing(float bouncing) {
    this->bouncing = bouncing;
    return *this;
}
EPIX_API Element& Element::set_friction(float friction) {
    this->friction = friction;
    return *this;
}
EPIX_API Element& Element::set_awake_rate(float rate) {
    this->awake_rate = std::clamp(rate, 0.0f, 1.0f);
    return *this;
}
EPIX_API Element& Element::set_description(const std::string& description) {
    this->description = description;
    return *this;
}
EPIX_API Element& Element::set_color(std::function<glm::vec4()> color_gen) {
    this->color_gen = color_gen;
    return *this;
}
EPIX_API Element& Element::set_color(const glm::vec4& color) {
    this->color_gen = [color]() { return color; };
    return *this;
}
EPIX_API bool Element::is_complete() const {
    return !name.empty() && color_gen && density != 0.0f;
}
EPIX_API glm::vec4 Element::gen_color() const { return color_gen(); }
EPIX_API bool Element::operator==(const Element& other) const {
    return name == other.name;
}
EPIX_API bool Element::operator!=(const Element& other) const {
    return name != other.name;
}
EPIX_API bool Element::is_solid() const { return grav_type == GravType::SOLID; }
EPIX_API bool Element::is_liquid() const {
    return grav_type == GravType::LIQUID;
}
EPIX_API bool Element::is_powder() const {
    return grav_type == GravType::POWDER;
}
EPIX_API bool Element::is_gas() const { return grav_type == GravType::GAS; }