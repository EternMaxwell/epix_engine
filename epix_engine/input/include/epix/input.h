#pragma once

#include <epix/app.h>
#include <epix/window.h>

#include "input/components.h"
#include "input/events.h"
#include "input/systems.h"

namespace epix {
namespace input {
using namespace systems;
using namespace events;

struct InputPlugin : Plugin {
    bool enable_output_event = false;
    EPIX_API InputPlugin& enable_output();
    EPIX_API InputPlugin& disable_output();
    EPIX_API void build(App& app) override;
};
}  // namespace input
}  // namespace epix
