// epix_core module interface
// This is the main module interface for the epix::core ECS library

module;

// Global module fragment - traditional headers that can't be modularized yet
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Third-party headers
#include <spdlog/spdlog.h>
#define BS_THREAD_POOL_NATIVE_EXTENSIONS
#include <BS_thread_pool.hpp>

// Compatibility layer
#include "epix/utils/cpp23_compat.hpp"

// API macros
#include "epix/api/macros.hpp"

export module epix_core;

// Export the core namespace and all its contents
// This is a transitional module that re-exports header content
// In a full migration, these would be defined directly in the module

export {
    // Include all public headers to export their contents
    #include "epix/core/fwd.hpp"
    #include "epix/core/tick.hpp"
    #include "epix/core/label.hpp"
    #include "epix/core/entities.hpp"
    #include "epix/core/component.hpp"
    #include "epix/core/archetype.hpp"
    #include "epix/core/bundle.hpp"
    #include "epix/core/bundleimpl.hpp"
    #include "epix/core/storage.hpp"
    #include "epix/core/change_detection.hpp"
    #include "epix/core/hierarchy.hpp"
    
    // Type system
    #include "epix/core/type_system/type_registry.hpp"
    
    // Meta
    #include "epix/core/meta/fwd.hpp"
    #include "epix/core/meta/typeid.hpp"
    #include "epix/core/meta/typeindex.hpp"
    
    // Query
    #include "epix/core/query/fwd.hpp"
    #include "epix/core/query/access.hpp"
    #include "epix/core/query/filter.hpp"
    #include "epix/core/query/fetch.hpp"
    #include "epix/core/query/state.hpp"
    #include "epix/core/query/iter.hpp"
    #include "epix/core/query/query.hpp"
    
    // System
    #include "epix/core/system/fwd.hpp"
    #include "epix/core/system/func_traits.hpp"
    #include "epix/core/system/system.hpp"
    #include "epix/core/system/system_param.hpp"
    
    // Schedule
    #include "epix/core/schedule/schedule.hpp"
    #include "epix/core/schedule/system_dispatcher.hpp"
    
    // World
    #include "epix/core/world.hpp"
    
    // World utilities
    #include "epix/core/world/command_queue.hpp"
    #include "epix/core/world/entity_ref.hpp"
    #include "epix/core/world/from_world.hpp"
    
    // Events
    #include "epix/core/event/events.hpp"
    #include "epix/core/event/reader.hpp"
    #include "epix/core/event/writer.hpp"
    
    // App
    #include "epix/core/app/state.hpp"
    #include "epix/core/app/plugin.hpp"
    #include "epix/core/app/schedules.hpp"
    #include "epix/core/app/extract.hpp"
    #include "epix/core/app/app_sche.hpp"
    #include "epix/core/app.hpp"
}

// Export the main namespace
export namespace epix::core {
    // All symbols from the included headers are now exported
}
