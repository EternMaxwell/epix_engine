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
import epix.ecs;
import epix.app;
import epix.input;
import epix.window;
import epix.assets;
import epix.image;
extern "C++" {
#include <epix/glfw/core.hpp>
}
