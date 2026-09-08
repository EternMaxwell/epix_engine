#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#endif

namespace epix::assets {

/** @brief Bit flags controlling which worlds retain an asset (Bevy 0.18
 * `bevy_asset::RenderAssetUsages`). */
EPIX_EXPORT enum RenderAssetUsages : std::uint8_t {
    MAIN_WORLD = 1 << 0,
    RENDER_WORLD = 1 << 1,
};

}  // namespace epix::assets
