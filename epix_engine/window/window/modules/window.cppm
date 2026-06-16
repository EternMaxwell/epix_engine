module;
#ifndef EPIX_IMPORT_STD
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#endif

export module epix.window;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export import epix.core;
export import epix.input;
export import epix.assets;
export import epix.image;
extern "C++" {
#include <epix/window.hpp>
}
