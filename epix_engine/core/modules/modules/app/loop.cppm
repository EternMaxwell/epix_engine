module;

export module epix.core:app.loop;

import :app.decl;

namespace core {
/** @brief Signal used to request application exit.
 *  Send this event to stop the main loop. */
export struct AppExit {
    /** @brief Process exit code. 0 indicates success. */
    int code = 0;
};
/** @brief Plugin that installs the main application loop. */
export struct LoopPlugin {
    /** @brief Register the main loop and exit event with the app. */
    void build(App& app);
};
}  // namespace core