module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#endif

export module epix.extension.fallingsand;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.mesh;
import epix.time;
import epix.transform;
import epix.core_graph;
import epix.render;
import epix.extension.grid;
import epix.task;
import glm;
extern "C++" {
#include <epix/extension/fallingsand.hpp>
}
