module;

export module epix.core:world.entity_ref.decl;

namespace epix::core {
/** @brief Read-only reference to a single entity's components. */
export struct EntityRef;
/** @brief Mutable reference to a single entity's components (no structural changes). */
export struct EntityRefMut;
/** @brief Mutable reference with full world access for spawning/despawning. */
export struct EntityWorldMut;
}  // namespace core