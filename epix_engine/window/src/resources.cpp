#include "epix/window/resources.h"

using namespace epix;
using namespace epix::window::components;
using namespace epix::window::resources;

EPIX_API void WindowMap::insert(GLFWwindow* window, Entity entity) {
    window_map[window] = entity;
}

EPIX_API const Entity& WindowMap::get(GLFWwindow* window) const {
    return window_map.at(window);
}