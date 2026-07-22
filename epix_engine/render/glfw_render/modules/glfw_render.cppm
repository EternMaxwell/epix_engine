module;
#include <GLFW/glfw3.h>

export module epix.glfw.render;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import webgpu;
extern "C++" {
#include <epix/glfw/render.hpp>
}
