module;
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED 1
#endif
#ifndef EPIX_IMPORT_STD
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#endif
#include <SDL3/SDL.h>

export module epix.sdl3.core;
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
#include <epix/sdl3/core.hpp>
}
