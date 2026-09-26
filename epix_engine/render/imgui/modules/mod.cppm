module;

#ifndef EPIX_IMPORT_STD
#include <expected>
#include <format>
#include <memory>
#include <stdexcept>
#include <vector>
#endif

export module epix.render.imgui;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.render;
import epix.window;
import epix.sdl3.core;
import epix.sdl3.render;
import epix.input;
import webgpu;
extern "C++" {
#include <epix/render/imgui.hpp>
}
