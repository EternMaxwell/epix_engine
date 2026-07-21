module;

#ifndef EPIX_IMPORT_STD
#include <utility>
#endif
#include <glm/ext.hpp>
#include <glm/glm.hpp>

export module epix.transform:code;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;
extern "C++" {
#include <epix/transform.hpp>
}
