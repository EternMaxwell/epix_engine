module;

#ifndef EPIX_IMPORT_STD
#include <bit>
#include <compare>
#include <cstdint>
#include <string>
#endif

export module epix.pbr;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.assets;
import epix.image;
import epix.mesh;
import epix.camera;
import epix.transform;
import epix.render;
import epix.core_graph;
import webgpu;
extern "C++" {
#include <epix/pbr.hpp>
}
