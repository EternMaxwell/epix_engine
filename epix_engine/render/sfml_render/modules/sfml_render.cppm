module;

export module epix.sfml.render;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.window;
import webgpu;
extern "C++" {
#include <epix/sfml/render.hpp>
}
