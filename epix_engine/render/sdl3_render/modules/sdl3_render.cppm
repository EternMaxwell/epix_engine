module;
#include <SDL3/SDL.h>

export module epix.sdl3.render;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import webgpu;
extern "C++" {
#include <epix/sdl3/render.hpp>
}
