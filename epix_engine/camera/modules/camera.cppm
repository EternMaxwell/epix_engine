module;

#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#endif

export module epix.camera;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.mesh;
import epix.transform;
import epix.window;
import epix.utils;
import webgpu;
extern "C++" {
#include <epix/camera.hpp>
}
