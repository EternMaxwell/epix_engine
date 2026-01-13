module;

export module epix.core:hierarchy;

import std;

import :entities;
import :world.decl;
import :component;

namespace core {
export struct Parent {
   public:
    Parent(Entity parent_entity) : _entity(parent_entity) {}

    Entity entity() const { return _entity; }

    static void on_remove(World& world, HookContext ctx);
    static void on_insert(World& world, HookContext ctx);

   private:
    friend struct Children;

    Entity _entity;
};
export struct Children {
   public:
    Children() = default;

    auto& entities() const { return _entities; }

    static void on_remove(World& world, HookContext ctx);
    static void on_despawn(World& world, HookContext ctx);

   private:
    friend struct Parent;

    std::unordered_set<Entity> _entities;
};
}  // namespace core