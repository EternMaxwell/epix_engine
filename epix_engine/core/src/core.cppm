// C++20 Module interface file for epix::core
// This demonstrates how the core module would be structured

module;

// Global module fragment - for #include directives that need to be visible
#include <atomic>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

export module epix.core;

// Export module partitions (these would be created for different subsystems)
// export import :archetype;
// export import :bundle;
// export import :component;
// export import :entities;
// export import :query;
// export import :schedule;
// export import :storage;
// export import :system;
// export import :world;

// For now, we export the main namespace and types
// This is a simplified example - the actual implementation would export
// all public API types and functions from the headers

export namespace epix::core {
    // Forward declarations and type exports would go here
    // Example:
    // class World;
    // class Entity;
    // struct WorldId;
    // etc.
    
    // In the full implementation, we would:
    // 1. Convert each header's content to module exports
    // 2. Remove #include directives and use 'import' instead
    // 3. Use module partitions for better organization
    // 4. Export all public API surface
}

// Note: This is a template showing module structure.
// Actual migration requires:
// 1. Converting all header declarations to module exports
// 2. Resolving the C++23 standard library compatibility issues first
// 3. Creating module partition files for subsystems
// 4. Updating CMakeLists.txt to compile modules properly
