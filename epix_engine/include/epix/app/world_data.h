#pragma once

#include <entt/container/dense_map.hpp>
#include <entt/container/dense_set.hpp>
#include <entt/entity/registry.hpp>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <typeindex>

#include "entity.h"
#include "epix/common.h"
#include "epix/utils/command_queue.h"
#include "hash_tool.h"

namespace epix::app {
struct World;
struct Parent {
    Entity entity;
};
struct Children {
    entt::dense_set<Entity> entities;
};
}  // namespace epix::app

namespace epix::app {
using CommandQueue = epix::utils::AtomicCommandQueue<World&>;
struct WorldData {
    entt::registry registry;
    async::RwLock<entt::dense_map<std::type_index, std::shared_ptr<void>>>
        resources;
};
}  // namespace epix::app