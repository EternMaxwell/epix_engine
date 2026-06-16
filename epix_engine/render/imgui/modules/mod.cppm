module;

#ifndef EPIX_IMPORT_STD
#include <expected>
#include <format>
#include <stdexcept>
#include <vector>
#endif

export module epix.render.imgui;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;
import epix.render;
import epix.window;
import epix.glfw.core;
import epix.glfw.render;
import epix.input;
import webgpu;
extern "C++" {
#include <epix/render/imgui.hpp>
}
