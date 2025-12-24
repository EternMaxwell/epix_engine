#pragma once

#include <unordered_set>

#include "component.hpp"
#include "fwd.hpp"

namespace epix::core::hierarchy {
struct Parent {
    Entity entity;
    static void on_remove(World& world, HookContext ctx);
    static void on_insert(World& world, HookContext ctx);
};
struct Children {
    std::unordered_set<Entity> entities;
    static void on_remove(World& world, HookContext ctx);
    static void on_despawn(World& world, HookContext ctx);
};
}  // namespace epix::core::hierarchy