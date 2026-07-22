module;
#ifndef EPIX_IMPORT_STD
#include <filesystem>
#include <optional>
#include <vector>
#endif

export module epix.render.screenshot;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.render;
import epix.image;
import epix.assets;
import epix.input;
import webgpu;
extern "C++" {
#include <epix/render/screenshot.hpp>
}
