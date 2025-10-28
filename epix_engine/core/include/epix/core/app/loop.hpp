#pragma once

#include "../fwd.hpp"

namespace epix::core::app {
struct AppExit {
    int code = 0;
};
struct LoopPlugin {
    void build(App& app);
};
}  // namespace epix::core::app