module;
#ifndef EPIX_IMPORT_STD
#include <utility>
#endif

export module epix.transform;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export import glm;
import epix.core;
extern "C++" {
#include <epix/transform.hpp>
}
