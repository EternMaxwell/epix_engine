module;

export module epix.core:hierarchy;

import std;

import :entities;
import :world.decl;
import :component;

namespace epix::core {
/** @brief Component that marks an entity as a child of another entity.
 *  When inserted, automatically updates the parent's Children component.
 *  When removed, the child is detached from the parent's children set. */
export struct Parent {
   public:
    /** @brief Construct a Parent linking to the given parent entity. */
    Parent(Entity parent_entity) : _entity(parent_entity) {}

    /** @brief Get the parent entity. */
    Entity entity() const { return _entity; }

    /** @brief Hook called when a Parent component is removed from an entity. */
    static void on_remove(World& world, HookContext ctx);
    /** @brief Hook called when a Parent component is inserted on an entity. */
    static void on_insert(World& world, HookContext ctx);

   private:
    friend struct Children;

    Entity _entity;
};
/** @brief Component that stores the set of child entities.
 *  Automatically maintained by Parent insert/remove hooks.
 *  When this entity is despawned, all children are recursively despawned. */
export struct Children {
   public:
    Children() = default;

    /** @brief Get a const reference to the set of child entities. */
    auto& entities() const { return _entities; }

    /** @brief Hook called when a Children component is removed. */
    static void on_remove(World& world, HookContext ctx);
    /** @brief Hook called when the entity owning Children is despawned.
     *  Recursively despawns all child entities. */
    static void on_despawn(World& world, HookContext ctx);

   private:
    friend struct Parent;

    std::unordered_set<Entity> _entities;
};
}  // namespace core