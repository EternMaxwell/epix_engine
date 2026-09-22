#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#endif

/** @brief Constants for operating with light units (Bevy `light_consts`,
 *  `bevy_light/src/lib.rs:70-130`).
 *
 *  The lumen is luminous flux, the total light a source emits per unit time; the
 *  lux is illuminance, flux per unit area, which is why a directional light is
 *  measured in lux - it lights every surface roughly alike. */
namespace epix::pbr::light_consts {

/** @brief Approximations for converting the wattage of lamps to lumens. */
namespace lumens {
EPIX_EXPORT inline constexpr float LUMENS_PER_LED_WATTS = 90.0f;
EPIX_EXPORT inline constexpr float LUMENS_PER_INCANDESCENT_WATTS = 13.8f;
EPIX_EXPORT inline constexpr float LUMENS_PER_HALOGEN_WATTS = 19.8f;
/** @brief A very large cinema light, bright at Bevy's default very-overcast-day
 *  exposure and far too bright for indoor lighting at a lower exposure. */
EPIX_EXPORT inline constexpr float VERY_LARGE_CINEMA_LIGHT = 1'000'000.0f;
}  // namespace lumens

/** @brief Predefined lux values for several locations. */
namespace lux {
/** @brief Moonless, overcast night sky (starlight). */
EPIX_EXPORT inline constexpr float MOONLESS_NIGHT = 0.0001f;
/** @brief A full moon on a clear night. */
EPIX_EXPORT inline constexpr float FULL_MOON_NIGHT = 0.05f;
/** @brief The dark limit of civil twilight under a clear sky. */
EPIX_EXPORT inline constexpr float CIVIL_TWILIGHT = 3.4f;
/** @brief Family living room lights. */
EPIX_EXPORT inline constexpr float LIVING_ROOM = 50.0f;
/** @brief An office building's hallway or toilet lighting. */
EPIX_EXPORT inline constexpr float HALLWAY = 80.0f;
/** @brief A very dark overcast day. */
EPIX_EXPORT inline constexpr float DARK_OVERCAST_DAY = 100.0f;
/** @brief An office. */
EPIX_EXPORT inline constexpr float OFFICE = 320.0f;
/** @brief Sunrise or sunset on a clear day. */
EPIX_EXPORT inline constexpr float CLEAR_SUNRISE = 400.0f;
/** @brief An overcast day, typical of TV studio lighting. */
EPIX_EXPORT inline constexpr float OVERCAST_DAY = 1'000.0f;
/** @brief Ambient daylight, not direct sunlight; Bevy's directional light
 *  default. */
EPIX_EXPORT inline constexpr float AMBIENT_DAYLIGHT = 10'000.0f;
/** @brief Full daylight, not direct sun. */
EPIX_EXPORT inline constexpr float FULL_DAYLIGHT = 20'000.0f;
/** @brief Direct sunlight. */
EPIX_EXPORT inline constexpr float DIRECT_SUNLIGHT = 100'000.0f;
/** @brief Raw sunlight, before the atmosphere filters it. */
EPIX_EXPORT inline constexpr float RAW_SUNLIGHT = 130'000.0f;
}  // namespace lux

}  // namespace epix::pbr::light_consts
