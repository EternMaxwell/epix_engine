#pragma once


#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/common.hpp>
#endif

namespace epix::sfml::render {
/** @brief Plugin that registers SFML-specific render target (surface)
 * creation for the render pipeline. */
EPIX_EXPORT struct EPIX_API SFMLRenderPlugin {
    void attach(app::App& app);
};
}  // namespace epix::sfml::render

namespace epix::sfml {
EPIX_EXPORT using render::SFMLRenderPlugin;
}
