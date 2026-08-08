#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#endif

namespace epix::sfml::render {
/** @brief Plugin that registers SFML-specific render target (surface)
 * creation for the render pipeline. */
EPIX_EXPORT struct SFMLRenderPlugin {
    void attach(app::App& app);
};
}  // namespace epix::sfml::render

namespace epix::sfml {
EPIX_EXPORT using render::SFMLRenderPlugin;
}
