#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/color/srgba.hpp>
#endif

namespace epix::color::palettes {

/** @brief Named colors from the CSS1 specification, also known as
 *  [basic colors](https://en.wikipedia.org/wiki/Web_colors#Basic_colors) (Bevy `palettes::basic`,
 *  `bevy_color/src/palettes/basic.rs:1-4`).
 *
 *  This is the same set of colors used in the
 *  [VGA graphics standard](https://en.wikipedia.org/wiki/Video_Graphics_Array). Bevy declares each
 *  one as a `const Srgba`; the port keeps that as an `inline constexpr Srgba`, and the doc comment
 *  keeps Bevy's own swatch line so the values read the same in both. */
namespace basic {

/** @brief The `#00FFFF` basic color, spelled `AQUA` (Bevy `palettes::basic::AQUA`,
 *  `bevy_color/src/palettes/basic.rs:8-9`).
 *  <div style="background-color: #00FFFF; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba AQUA = Srgba::rgb(0.0f, 1.0f, 1.0f);

/** @brief The `#000000` basic color (Bevy `palettes::basic::BLACK`,
 *  `bevy_color/src/palettes/basic.rs:10-11`).
 *  <div style="background-color: #000000; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba BLACK = Srgba::rgb(0.0f, 0.0f, 0.0f);

/** @brief The `#0000FF` basic color (Bevy `palettes::basic::BLUE`,
 *  `bevy_color/src/palettes/basic.rs:12-13`).
 *  <div style="background-color: #0000FF; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba BLUE = Srgba::rgb(0.0f, 0.0f, 1.0f);

/** @brief The `#FF00FF` basic color (Bevy `palettes::basic::FUCHSIA`,
 *  `bevy_color/src/palettes/basic.rs:14-15`).
 *  <div style="background-color: #FF00FF; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba FUCHSIA = Srgba::rgb(1.0f, 0.0f, 1.0f);

/** @brief The `#808080` basic color (Bevy `palettes::basic::GRAY`,
 *  `bevy_color/src/palettes/basic.rs:16-17`).
 *  <div style="background-color: #808080; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba GRAY = Srgba::rgb(0.5019608f, 0.5019608f, 0.5019608f);

/** @brief The `#008000` basic color, spelled `GREEN`; it is not the RGB primary (Bevy
 *  `palettes::basic::GREEN`, `bevy_color/src/palettes/basic.rs:18-19`).
 *  <div style="background-color: #008000; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba GREEN = Srgba::rgb(0.0f, 0.5019608f, 0.0f);

/** @brief The `#00FF00` basic color, spelled `LIME`; it is the RGB primary (Bevy
 *  `palettes::basic::LIME`, `bevy_color/src/palettes/basic.rs:20-21`).
 *  <div style="background-color: #00FF00; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba LIME = Srgba::rgb(0.0f, 1.0f, 0.0f);

/** @brief The `#800000` basic color (Bevy `palettes::basic::MAROON`,
 *  `bevy_color/src/palettes/basic.rs:22-23`).
 *  <div style="background-color: #800000; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba MAROON = Srgba::rgb(0.5019608f, 0.0f, 0.0f);

/** @brief The `#000080` basic color (Bevy `palettes::basic::NAVY`,
 *  `bevy_color/src/palettes/basic.rs:24-25`).
 *  <div style="background-color: #000080; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba NAVY = Srgba::rgb(0.0f, 0.0f, 0.5019608f);

/** @brief The `#808000` basic color (Bevy `palettes::basic::OLIVE`,
 *  `bevy_color/src/palettes/basic.rs:26-27`).
 *  <div style="background-color: #808000; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba OLIVE = Srgba::rgb(0.5019608f, 0.5019608f, 0.0f);

/** @brief The `#800080` basic color (Bevy `palettes::basic::PURPLE`,
 *  `bevy_color/src/palettes/basic.rs:28-29`).
 *  <div style="background-color: #800080; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba PURPLE = Srgba::rgb(0.5019608f, 0.0f, 0.5019608f);

/** @brief The `#FF0000` basic color (Bevy `palettes::basic::RED`,
 *  `bevy_color/src/palettes/basic.rs:30-31`).
 *  <div style="background-color: #FF0000; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba RED = Srgba::rgb(1.0f, 0.0f, 0.0f);

/** @brief The `#C0C0C0` basic color (Bevy `palettes::basic::SILVER`,
 *  `bevy_color/src/palettes/basic.rs:32-33`).
 *  <div style="background-color: #C0C0C0; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba SILVER = Srgba::rgb(0.7529412f, 0.7529412f, 0.7529412f);

/** @brief The `#008080` basic color (Bevy `palettes::basic::TEAL`,
 *  `bevy_color/src/palettes/basic.rs:34-35`).
 *  <div style="background-color: #008080; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba TEAL = Srgba::rgb(0.0f, 0.5019608f, 0.5019608f);

/** @brief The `#FFFFFF` basic color (Bevy `palettes::basic::WHITE`,
 *  `bevy_color/src/palettes/basic.rs:36-37`).
 *  <div style="background-color: #FFFFFF; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba WHITE = Srgba::rgb(1.0f, 1.0f, 1.0f);

/** @brief The `#FFFF00` basic color (Bevy `palettes::basic::YELLOW`,
 *  `bevy_color/src/palettes/basic.rs:38-39`).
 *  <div style="background-color: #FFFF00; width: 10px; padding: 10px; border: 1px solid;"></div> */
inline constexpr Srgba YELLOW = Srgba::rgb(1.0f, 1.0f, 0.0f);

}  // namespace basic

}  // namespace epix::color::palettes
