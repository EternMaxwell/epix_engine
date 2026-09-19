module;

#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#endif

#include <asio/awaitable.hpp>

export module epix.text;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.assets;
import epix.image;
import epix.transform;
import epix.camera;
import epix.sprite;
import epix.sprite_render;
import epix.core_graph;
import webgpu;
extern "C++" {
#include <epix/text.hpp>
}
