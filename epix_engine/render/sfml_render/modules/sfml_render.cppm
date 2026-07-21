module;

export module epix.sfml.render;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;
import epix.window;
import webgpu;
extern "C++" {
#include <epix/sfml/render.hpp>
}
