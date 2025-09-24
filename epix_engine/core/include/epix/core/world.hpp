#pragma once

#include "entities.hpp"
#include "fwd.hpp"
#include "type_system/type_registry.hpp"

namespace epix::core {
/**
 * @brief A ecs world that holds all entities and components, and also resources.
 */
struct World {
    size_t id;
    type_system::TypeRegistry type_registry;
    Entities entities;
};
}  // namespace epix::core