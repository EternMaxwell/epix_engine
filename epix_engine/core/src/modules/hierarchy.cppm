module;

export module epix.core:hierarchy;

import std;

import :entities;
import :world.decl;
import :component;

namespace core {
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
}  // namespace core