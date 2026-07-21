module;

#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstdint>
#include <optional>
#endif

export module epix.core_graph;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;
import epix.transform;
import epix.render;
import webgpu;
extern "C++" {
#include <epix/core_graph.hpp>
}
