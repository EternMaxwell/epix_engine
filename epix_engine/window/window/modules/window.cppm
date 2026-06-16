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
import epix.core;
import epix.input;
import epix.assets;
import epix.image;
extern "C++" {
#include <epix/window.hpp>
}
