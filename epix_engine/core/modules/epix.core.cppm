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

// Core ECS partitions (fully implemented)
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
export import :label;         // Labels
export import :change_detection; // Change detection

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
        
        // From :change_detection
        using core::Ref;
        using core::Mut;
        using core::Res;
        using core::ResMut;
        
        // From :type_system
        using type_system::TypeId;
        using type_system::TypeInfo;
        using type_system::TypeRegistry;
        
        // From :entities
        using core::Entity;
        using core::Entities;
        using core::EntityRef;
        using core::EntityRefMut;
        using core::EntityWorldMut;
        using core::ArchetypeId;
        using core::TableId;
        using core::ArchetypeRow;
        using core::TableRow;
        using core::BundleId;
        
        // From :component
        using core::ComponentInfo;
        using core::ComponentHooks;
        using core::HookContext;
        
        // From :world
        using core::World;
        using core::DeferredWorld;
        using core::WorldId;
        
        // From :query
        using query::Query;
        using query::QueryState;
        using query::Filter;
        using query::With;
        using query::Without;
        using query::Or;
        using query::Added;
        using query::Modified;
        using query::Has;
        using query::Opt;
        using query::Single;
        using query::Item;
        
        // From :system
        using system::System;
        using system::SystemParam;
        using system::Commands;
        using system::EntityCommands;
        using system::Local;
        using system::ParamSet;
        using system::RunSystemError;
        using system::ValidateParamError;
        using system::SystemException;
        using system::SystemMeta;
        
        // From :event
        using event::Events;
        using event::EventReader;
        using event::EventWriter;
        
        // From :schedule
        using schedule::Schedule;
        using schedule::SystemSetLabel;
        using schedule::ExecuteConfig;
        using schedule::SetConfig;
        
        // From :app
        using core::App;
        using app::AppExit;
        using app::LoopPlugin;
        using app::Plugin;
        using app::AppRunner;
        using app::Extract;
        using app::AppLabel;
        using app::First;
        using app::Last;
        using app::PreStartup;
        using app::Startup;
        using app::PostStartup;
        using app::PreUpdate;
        using app::Update;
        using app::PostUpdate;
        using app::PreExit;
        using app::Exit;
        using app::PostExit;
        using app::State;
        using app::NextState;
        using app::OnEnter;
        using app::OnExit;
        using app::OnChange;
        using app::StateTransition;
        
        // From :hierarchy
        using hierarchy::Parent;
        using hierarchy::Children;
        
        // From :label
        using core::Label;
        
        // From :storage
        using core::Storage;
        
        // From :archetype
        using core::Archetype;
        using core::Archetypes;
        
        // From :bundle
        using core::Bundles;
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
