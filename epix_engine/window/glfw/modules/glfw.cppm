module;
#ifndef EPIX_IMPORT_STD
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#endif
#include <GLFW/glfw3.h>

export module epix.glfw.core;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export import epix.core;
export import epix.input;
export import epix.window;
export import epix.assets;
export import epix.image;
extern "C++" {
#include <epix/glfw/core.hpp>
}
