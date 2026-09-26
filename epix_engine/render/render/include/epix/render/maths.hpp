#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#endif

namespace epix::render {

/** @brief The embedded path of the shared maths shader library (Bevy
 *  `bevy_render/src/maths.wgsl`, which `RenderPlugin::build` loads at
 *  `bevy_render/src/lib.rs:402`). */
EPIX_EXPORT inline constexpr std::string_view MATHS_SHADER_PATH = "epix/shaders/maths.slang";

/** @brief Publish the maths shader library as an embedded asset and load it
 *  (Bevy's `load_shader_library!(app, "maths.wgsl")`).
 *
 * The mesh shaders import it for the affine and packed-normal helpers, the same
 * way Bevy's do. */
EPIX_EXPORT EPIX_API void register_maths_shader(ecs::World& world);

}  // namespace epix::render
