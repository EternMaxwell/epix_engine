#pragma once

#include <epix/core.hpp>

namespace epix::sfml::render {
/** @brief Plugin that registers SFML-specific render target (surface)
 * creation for the render pipeline. */
struct SFMLRenderPlugin {
    void attach(epix::core::App& app);
};
}  // namespace epix::sfml::render

namespace epix::sfml {
using render::SFMLRenderPlugin;
}  // namespace epix::sfml
