/**
 * @file epix.core.cppm
 * @brief Main module interface for epix.core
 * 
 * This is the primary module interface that re-exports all core ECS functionality.
 * It uses module partitions to organize the codebase into logical units.
 */

export module epix.core;

// Standard library includes (not using import std yet due to incomplete compiler support)
#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <expected>
#include <format>
#include <functional>
#include <future>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <ranges>
#include <ratio>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// Third-party library includes (these don't have module interfaces yet)
#include <BS_thread_pool.hpp>
#include <spdlog/spdlog.h>

// Re-export all module partitions
// These partitions need to be created for the full implementation

// Foundation partitions (currently implemented)
export import :api;           // API macros and basic types
export import :fwd;           // Forward declarations
export import :meta;          // Type metadata system
export import :tick;          // Change detection ticks

// Core ECS partitions (to be implemented)
export import :type_system;   // Type registry
export import :storage;       // Component storage
export import :archetype;     // Archetype management
export import :entities;      // Entity management
export import :component;     // Component definitions
export import :bundle;        // Component bundles
export import :query;         // Entity queries
export import :system;        // System execution
export import :event;         // Event system
export import :schedule;      // System scheduling
export import :world;         // World abstraction
export import :app;           // Application framework
export import :hierarchy;     // Entity hierarchies

/**
 * Main epix::core namespace
 * Contains all core ECS functionality
 */
export namespace epix::core {
    /**
     * Prelude namespace for commonly used types
     * Import this to get the most commonly used core types
     */
    namespace prelude {
        // From :meta
        using meta::type_id;
        using meta::type_index;
        using meta::type_name;
        using meta::short_name;
        
        // From :tick
        using core::Tick;
        using core::ComponentTicks;
        using core::TickRefs;
        
        // From :api  
        using wrapper::int_base;
        
        // The following will be uncommented as partitions are implemented:
        
        // // From :type_system
        // using type_system::TypeId;
        // using type_system::TypeInfo;
        // using type_system::TypeRegistry;
        
        // // From :entities
        // using core::Entity;
        // using core::Entities;
        // using core::EntityRef;
        // using core::EntityRefMut;
        // using core::EntityWorldMut;
        
        // // From :component
        // using core::ComponentInfo;
        
        // // From :world
        // using core::World;
        // using core::DeferredWorld;
        
        // // From :query
        // using query::Query;
        // using query::QueryState;
        // using query::Filter;
        // using query::With;
        // using query::Without;
        
        // // From :system
        // using system::System;
        // using system::SystemParam;
        // using system::Commands;
        
        // // From :event
        // using event::Events;
        // using event::EventReader;
        // using event::EventWriter;
        
        // // From :schedule
        // using schedule::Schedule;
        
        // // From :app
        // using core::App;
        // using app::AppExit;
        // using app::LoopPlugin;
        
        // // From :hierarchy
        // using core::hierarchy::Parent;
        // using core::hierarchy::Children;
    }
    
    // Make prelude available in epix::core
    using namespace prelude;
}

// Make core prelude available in top-level epix namespace
export namespace epix {
    using namespace core::prelude;
}

// Make core prelude available in epix::prelude
export namespace epix::prelude {
    using namespace core::prelude;
}
