#pragma once

#include <epix/app.h>

namespace epix {
namespace window {
namespace components {
struct Window;
}
namespace events {
using namespace components;
struct AnyWindowClose {
    Entity window;
};
struct NoWindowExists {};
struct PrimaryWindowClose {};

struct MouseScroll {
    double xoffset;
    double yoffset;
    Entity window;
};
struct CursorMove {
    double x;
    double y;
    Entity window;
};
}  // namespace events
}  // namespace window
}  // namespace epix