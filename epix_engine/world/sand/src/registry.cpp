#include <spdlog/spdlog.h>

#include "epix/world/sand.h"

using namespace epix::world::sand;

static std::shared_ptr<spdlog::logger> elem_registry_logger =
    spdlog::default_logger()->clone("elem_registry");

EPIX_API Registry_T::Registry_T() { register_elem(Element::place_holder()); }
EPIX_API Registry_T::Registry_T(const Registry_T& other)
    : elemId_map(other.elemId_map), elements(other.elements) {}
EPIX_API Registry_T::Registry_T(Registry_T&& other)
    : elemId_map(std::move(other.elemId_map)),
      elements(std::move(other.elements)) {}
EPIX_API Registry_T& Registry_T::operator=(const Registry_T& other) {
    elemId_map = other.elemId_map;
    elements   = other.elements;
    return *this;
}
EPIX_API Registry_T& Registry_T::operator=(Registry_T&& other) {
    elemId_map = std::move(other.elemId_map);
    elements   = std::move(other.elements);
    return *this;
}

EPIX_API Registry_T* Registry_T::create() { return new Registry_T(); }
EPIX_API void Registry_T::destroy(Registry_T* registry) { delete registry; }
EPIX_API std::unique_ptr<Registry_T> Registry_T::create_unique() {
    return std::make_unique<Registry_T>();
}
EPIX_API std::shared_ptr<Registry_T> Registry_T::create_shared() {
    return std::make_shared<Registry_T>();
}

EPIX_API int Registry_T::register_elem(
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
EPIX_API int Registry_T::register_elem(const Element& elem) {
    return register_elem(elem.name, elem);
}
EPIX_API int Registry_T::id_of(const std::string& name) const {
    // std::shared_lock<std::shared_mutex> lock(mutex);
    if (elemId_map.find(name) == elemId_map.end()) {
        return -1;
    }
    return elemId_map.at(name);
}
EPIX_API const std::string& Registry_T::name_of(int id) const {
    // std::shared_lock<std::shared_mutex> lock(mutex);
    if (id < 0 || id >= elements.size()) {
        return "";
    }
    return elements[id].name;
}
EPIX_API int Registry_T::count() const {
    // std::shared_lock<std::shared_mutex> lock(mutex);
    return elements.size();
}
EPIX_API const Element& Registry_T::get_elem(const std::string& name) const {
    // std::shared_lock<std::shared_mutex> lock(mutex);
    return elements.at(elemId_map.at(name));
}
EPIX_API const Element& Registry_T::get_elem(int id) const {
    return elements[id];
}
EPIX_API const Element& Registry_T::operator[](int id) const {
    return get_elem(id);
}
EPIX_API const Element& Registry_T::operator[](const std::string& name) const {
    return get_elem(name);
}
EPIX_API void Registry_T::add_equiv(
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

EPIX_API Particle Registry_T::create_particle(const PartDef& def) const {
    Particle p;
    if (std::holds_alternative<std::string>(def.id)) {
        p.elem_id = id_of(std::get<std::string>(def.id));
    } else if (std::holds_alternative<int>(def.id)) {
        p.elem_id = std::get<int>(def.id);
    } else {
        throw std::runtime_error("Invalid PartDef");
    }
    auto& elem = get_elem(p.elem_id);
    p.color    = elem.gen_color();
    if (elem.is_liquid() || elem.is_gas() || elem.is_powder()) {
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        static thread_local std::uniform_real_distribution<float> dis(
            -0.5f, 0.5f
        );
        p.set_freefall(true);
        p.velocity = {dis(gen), dis(gen)};
        p.inpos    = {dis(gen), dis(gen)};
        p.inpos /= 2.0f;
    }
    return p;
}