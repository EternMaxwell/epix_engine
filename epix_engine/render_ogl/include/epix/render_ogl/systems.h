#pragma once

#include <glad/glad.h>
#include <epix/app.h>
#include <epix/window.h>

namespace epix::render::ogl {
namespace systems {
using namespace prelude;

struct ContextCreated {};

EPIX_API void clear_color(Query<Get<window::Window>> query);
EPIX_API void update_viewport(Query<Get<window::Window>> query);
EPIX_API void context_creation(
    Command cmd,
    Query<Get<Entity, window::Window>, Without<ContextCreated>> query
);
EPIX_API void swap_buffers(Query<Get<window::Window>> query);
}  // namespace systems
}  // namespace epix::render::ogl