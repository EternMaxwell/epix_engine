#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#endif

namespace epix::app {
EPIX_EXPORT struct App;
/** @brief Signal used to request application exit.
 *  Send this event to stop the main loop. */
EPIX_EXPORT struct AppExit {
    /** @brief Process exit code. 0 indicates success. */
    int code = 0;
};
/** @brief Plugin that installs the main application loop. */
EPIX_EXPORT struct LoopPlugin {
    /** @brief Register the main loop and exit event with the app. */
    void attach(App& app);
};
}  // namespace epix::app