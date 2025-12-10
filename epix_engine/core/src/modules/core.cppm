// Primary module interface for epix.core
// This exports all core ECS functionality

module;

// Global module fragment - third-party libraries
#include <BS/thread_pool.hpp>
#include <spdlog/spdlog.h>

// Standard library headers
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

export module epix.core;

// Re-export all partitions
export import :fwd;
export import :tick;
export import :meta;
export import :type_system;

// Note: Remaining partitions would be added here as they are converted:
// export import :entities;
// export import :component;
// export import :storage;
// export import :archetype;
// export import :bundle;
// export import :world;
// export import :query;
// export import :system;
// export import :schedule;
// export import :event;
// export import :app;
// export import :hierarchy;
// export import :change_detection;

// Re-export prelude namespace for convenience
export namespace epix::core::prelude {
    // From :tick
    using core::Tick;
    using core::ComponentTicks;
    using core::TickRefs;
    using core::CHECK_TICK_THRESHOLD;
    using core::MAX_CHANGE_AGE;
    
    // From :meta
    using meta::type_id;
    using meta::type_index;
    using meta::type_name;
    using meta::short_name;
    
    // From :type_system
    using type_system::TypeId;
    using type_system::TypeInfo;
    using type_system::TypeRegistry;
    using core::StorageType;
    
    // Wrapper utilities
    using wrapper::int_base;
    
    // As more partitions are added, their types would be exposed here
    // using query::Query;
    // using query::QueryIter;
    // using system::Commands;
    // using system::System;
    // using core::World;
    // using core::Entity;
    // etc.
}

// Convenience namespaces
export namespace epix::prelude {
    using namespace core::prelude;
}

export namespace epix {
    using namespace core::prelude;
}
