#pragma once

#include "epix/app.h"
#include "window/components.h"
#include "window/events.h"
#include "window/systems.h"

namespace epix {
namespace window {
using namespace components;

enum class WindowStartUpSets {
    glfw_initialization,
    window_creation,
    after_window_creation,
};

enum class WindowPreUpdateSets {
    poll_events,
    update_window_data,
};

enum class WindowPreRenderSets {
    before_create,
    window_creation,
    after_create,
};

enum class WindowPostRenderSets {
    before_swap_buffers,
    swap_buffers,
    window_close,
    after_close_window,
};

class WindowPlugin : public app::Plugin {
   public:
    WindowDescription primary_window_description;

    EPIX_API WindowDescription& primary_desc();
    EPIX_API void build(App& app) override;
};
}  // namespace window
}  // namespace epix