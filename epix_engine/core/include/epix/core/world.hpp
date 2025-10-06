#pragma once

#include "component.hpp"
#include "entities.hpp"
#include "fwd.hpp"
#include "storage.hpp"
#include "type_system/type_registry.hpp"

namespace epix::core {
/**
 * @brief A ecs world that holds all entities and components, and also resources.
 */
struct World {
    size_t id;
    std::shared_ptr<type_system::TypeRegistry> type_registry;
    Entities entities;
    Components components;
    Storage storage;
};
}  // namespace epix::core