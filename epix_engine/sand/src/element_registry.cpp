#include <spdlog/spdlog.h>

#include "epix/world/sand/components.h"

using namespace epix::world::sand::components;

static std::shared_ptr<spdlog::logger> elem_registry_logger =
    spdlog::default_logger()->clone("elem_registry");

EPIX_API ElemRegistry::ElemRegistry() {}
EPIX_API ElemRegistry::ElemRegistry(const ElemRegistry& other)
    : elemId_map(other.elemId_map), elements(other.elements) {}
EPIX_API ElemRegistry::ElemRegistry(ElemRegistry&& other)
    : elemId_map(std::move(other.elemId_map)),
      elements(std::move(other.elements)) {}
EPIX_API ElemRegistry& ElemRegistry::operator=(const ElemRegistry& other) {
    elemId_map = other.elemId_map;
    elements   = other.elements;
    return *this;
}
EPIX_API ElemRegistry& ElemRegistry::operator=(ElemRegistry&& other) {
    elemId_map = std::move(other.elemId_map);
    elements   = std::move(other.elements);
    return *this;
}

EPIX_API int ElemRegistry::register_elem(
    const std::string& name, const Element& elem
) {
    // std::unique_lock<std::shared_mutex> lock(mutex);
    if (elemId_map.find(name) != elemId_map.end()) {
        elem_registry_logger->warn(
            "Attempted to register element {} that already exists", name
        );
        return -1;
    }
    if (!elem.is_complete()) {
        elem_registry_logger->warn(
            "Attempted to register incomplete element {}", name
        );
        return -1;
    }
    uint32_t id      = elements.size();
    elemId_map[name] = id;
    elements.emplace_back(elem);
    return id;
}
EPIX_API int ElemRegistry::register_elem(const Element& elem) {
    return register_elem(elem.name, elem);
}
EPIX_API int ElemRegistry::elem_id(const std::string& name) const {
    // std::shared_lock<std::shared_mutex> lock(mutex);
    if (elemId_map.find(name) == elemId_map.end()) {
        return -1;
    }
    return elemId_map.at(name);
}
EPIX_API std::string ElemRegistry::elem_name(int id) const {
    // std::shared_lock<std::shared_mutex> lock(mutex);
    if (id < 0 || id >= elements.size()) {
        return "";
    }
    return elements[id].name;
}
EPIX_API int ElemRegistry::count() const {
    // std::shared_lock<std::shared_mutex> lock(mutex);
    return elements.size();
}
EPIX_API const Element& ElemRegistry::get_elem(const std::string& name) const {
    // std::shared_lock<std::shared_mutex> lock(mutex);
    return elements.at(elemId_map.at(name));
}
EPIX_API const Element& ElemRegistry::get_elem(int id) const {
    return elements[id];
}
EPIX_API const Element& ElemRegistry::operator[](int id) const {
    return get_elem(id);
}
EPIX_API void ElemRegistry::add_equiv(
    const std::string& name, const std::string& equiv
) {
    // std::unique_lock<std::shared_mutex> lock(mutex);
    if (elemId_map.find(name) == elemId_map.end()) {
        elem_registry_logger->warn(
            "Attempted to add equivalent element {} to non-existent "
            "element {}",
            equiv, name
        );
        return;
    }
    elemId_map[equiv] = elemId_map.at(name);
}