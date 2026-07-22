module;

#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>
#endif

#include <box2d/box2d.h>
#include <box2d/id.h>
#include <box2d/types.h>

export module epix.experimental.pixelbody;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.time;
import epix.assets;
import epix.mesh;
import epix.transform;
import epix.extension.grid;
import epix.extension.fallingsand;
import webgpu;
extern "C++" {
#include <epix/experimental/pixelbody.hpp>
}
