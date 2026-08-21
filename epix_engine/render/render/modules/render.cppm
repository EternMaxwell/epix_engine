module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#endif
#include <spdlog/spdlog.h>

#include <glm/ext.hpp>
#include <glm/glm.hpp>

export module epix.render;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.assets;
import epix.shader;
import epix.image;
import epix.window;
import epix.transform;
import epix.time;
import BS.thread_pool;
import webgpu;
extern "C++" {
#include <epix/render.hpp>
}
