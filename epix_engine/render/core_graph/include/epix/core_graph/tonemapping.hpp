#pragma once

#include <epix/common.hpp>

namespace epix::core_graph {

/**
 * @brief Per-camera tone-mapping method (Bevy `Tonemapping`).
 *
 * `TonyMcMapface` is intentionally the value-initialized C++ default, matching
 * Bevy's `Default` implementation. Core2D explicitly requires `None`.
 */
EPIX_EXPORT enum class Tonemapping {
    TonyMcMapface,
    None,
    Reinhard,
    ReinhardLuminance,
    AcesFitted,
    AgX,
    SomewhatBoringDisplayTransform,
    BlenderFilmic,
};

/** @brief Bevy `Tonemapping::is_enabled`. */
constexpr bool is_enabled(Tonemapping value) noexcept { return value != Tonemapping::None; }

/** @brief Per-camera debanding switch (Bevy `DebandDither`). */
EPIX_EXPORT enum class DebandDither { Disabled, Enabled };

}  // namespace epix::core_graph
