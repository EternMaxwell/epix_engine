#pragma once

#include <concepts>

namespace epix::assets {
/** @brief Concept for asset types.
 *  Matches bevy_asset's `Asset` trait bound: `VisitAssetDependencies + TypePath + Send + Sync`.
 *  C++ has no TypePath/Send/Sync equivalents, so only VisitAssetDependencies is required. */
template <typename T>
concept Asset = std::movable<T>;
}  // namespace epix::assets