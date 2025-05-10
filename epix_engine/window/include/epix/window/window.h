#pragma once

#include <GLFW/glfw3.h>
#include <epix/assets.h>
#include <epix/utils/core.h>

#include <variant>

namespace epix::window::window {
enum class StandardCursor {
    Arrow,
    IBeam,
    Crosshair,
    Hand,
    ResizeAll,
    ResizeNS,
    ResizeEW,
    ResizeNWSE,
    ResizeNESW,
    NotAllowed,
};
struct CursorIcon {
    std::variant<StandardCursor, epix::assets::UntypedHandle> icon;
};
struct Cursor {};
struct Window {
    Cursor cursor;
};
}  // namespace epix::window::window
