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

export module epix.render;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;
import epix.assets;
import epix.shader;
import epix.image;
import epix.window;
import epix.transform;
import BS.thread_pool;
import webgpu;
import glm;
extern "C++" {
#include <epix/render.hpp>
}
