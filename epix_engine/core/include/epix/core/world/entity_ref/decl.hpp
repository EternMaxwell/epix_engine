#pragma once

namespace epix::core {
/** @brief Read-only reference to a single entity's components. */
struct EntityRef;
/** @brief Mutable reference to a single entity's components (no structural changes). */
struct EntityRefMut;
/** @brief Mutable reference with full world access for spawning/despawning. */
struct EntityWorldMut;
}  // namespace epix::core