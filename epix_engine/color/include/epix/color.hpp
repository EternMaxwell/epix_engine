#pragma once

/** @brief The `bevy_color` crate (Bevy `bevy_color/src/lib.rs:96-146`).
 *
 *  Bevy declares one module per colour space and re-exports each of them flat from the crate root;
 *  the port mirrors that with one header per space plus this umbrella, which is what a call site
 *  includes (`#include <epix/color.hpp>`). Bevy's `prelude` module (`lib.rs:122-130`) re-exports
 *  the same set; the port keeps the flat `<epix/color.hpp>` surface instead, the convention the
 *  camera module already uses for its own prelude.
 *
 *  Deferred to a later slice, and tracked in the colour section: `color_gradient` (it needs
 *  `bevy_math::curve`), `color_range` (Bevy implements `ColorRange` for `core::ops::Range<T>`),
 *  and the palettes `css`/`tailwind`. The internal `StandardColor` marker trait (`lib.rs:148-176`)
 *  has no C++ counterpart either, and `color_difference`'s `EuclideanDistance` trait is documented
 *  in `epix/color/color_ops.hpp` and implemented as members of each space, so it has no header of
 *  its own. */

#include <epix/color/color_ops.hpp>
#include <epix/color/color.hpp>
#include <epix/color/hsla.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/hwba.hpp>
#include <epix/color/laba.hpp>
#include <epix/color/lcha.hpp>
#include <epix/color/linear_rgba.hpp>
#include <epix/color/oklaba.hpp>
#include <epix/color/oklcha.hpp>
#include <epix/color/palettes/basic.hpp>
#include <epix/color/srgba.hpp>
#include <epix/color/xyza.hpp>
