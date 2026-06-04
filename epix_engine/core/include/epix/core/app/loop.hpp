#pragma once

#include <epix/core/app/decl.hpp>

namespace epix::core {
/** @brief Signal used to request application exit.
 *  Send this event to stop the main loop. */
struct AppExit {
    /** @brief Process exit code. 0 indicates success. */
    int code = 0;
};
/** @brief Plugin that installs the main application loop. */
struct LoopPlugin {
    /** @brief Register the main loop and exit event with the app. */
    void attach(App& app);
};
}  // namespace epix::core