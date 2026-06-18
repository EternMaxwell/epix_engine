#pragma once

#include <epix/common.hpp>

namespace epix::core {
/** @brief Read-only reference to a single entity's components. */
EPIX_EXPORT struct EntityRef;
/** @brief Mutable reference to a single entity's components (no structural changes). */
EPIX_EXPORT struct EntityRefMut;
/** @brief Mutable reference with full world access for spawning/despawning. */
EPIX_EXPORT struct EntityWorldMut;
}  // namespace epix::core