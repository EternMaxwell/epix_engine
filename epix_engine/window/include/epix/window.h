#pragma once

#include "epix/app.h"
#include "window/events.h"

namespace epix {
namespace window {
struct WindowPlugin : public app::Plugin {
    EPIX_API void build(App& app) override;
};
}  // namespace window
namespace glfw {
struct GLFWPlugin : public app::Plugin {
    EPIX_API void build(App& app) override;
};
}  // namespace glfw
}  // namespace epix