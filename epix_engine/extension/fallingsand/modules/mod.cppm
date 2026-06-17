module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
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
import epix.core;
import epix.mesh;
import epix.time;
import epix.transform;
import epix.core_graph;
import epix.render;
import epix.extension.grid;
import epix.tasks;
import glm;
extern "C++" {
#include <epix/extension/fallingsand.hpp>
}
