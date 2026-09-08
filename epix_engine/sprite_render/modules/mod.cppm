module;

#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <vector>
#endif

export module epix.sprite_render;
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
import epix.sprite;
import webgpu;
extern "C++" {
#include <epix/sprite_render.hpp>
}
