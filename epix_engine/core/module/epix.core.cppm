/**
 * @file epix.core.cppm
 * @brief Primary module interface for the epix core ECS library.
 *
 * This module exports all core ECS functionality including:
 * - Entity management
 * - Component storage
 * - Systems and schedules
 * - World management
 * - Events
 * - Application framework
 *
 * Usage:
 *   import epix.core;
 */
module;

// Global module fragment for non-modular dependencies
#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <future>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// Third-party library includes
#include <spdlog/spdlog.h>

export module epix.core;

// Re-export all submodules
export import epix.core.api;
export import epix.core.meta;
export import epix.core.tick;
export import epix.core.type_system;
export import epix.core.entities;

// Additional exports from the main module
export namespace epix::core {

// Forward declarations
struct World;
struct WorldCell;
struct EntityRef;
struct EntityRefMut;
struct EntityWorldMut;
struct ComponentInfo;
struct App;

}  // namespace epix::core

// Label system
export namespace epix::core {

/**
 * @brief A type-erased label for identifying schedules, systems, and other named entities.
 *
 * Labels can be created from:
 * - Types (using type_index)
 * - Enums
 * - Integers
 * - Pointers
 */
struct Label {
   public:
    static Label from_raw(const epix::core::meta::type_index& type_index, uintptr_t extra = 0) {
        Label label;
        label.type_index_ = type_index;
        label.extra_      = extra;
        return label;
    }

    template <typename T>
    static Label from_type() {
        return from_raw(epix::core::meta::type_id<T>());
    }

    template <typename T>
    static Label from_enum(T t)
        requires(std::is_enum_v<T>)
    {
        return from_raw(epix::core::meta::type_id<T>(), static_cast<uintptr_t>(t));
    }

    template <std::integral T>
    static Label from_integral(T value) {
        return from_raw(epix::core::meta::type_id<T>(), static_cast<uintptr_t>(value));
    }

    template <typename T>
    static Label from_pointer(T* ptr) {
        return from_raw(epix::core::meta::type_id<T>(), (uintptr_t)(ptr));
    }

    template <typename T>
    Label(T&& t)
        requires(!std::is_same_v<std::decay_t<T>, Label> && !std::derived_from<std::decay_t<T>, Label> &&
                 (std::is_enum_v<std::decay_t<T>> || std::is_pointer_v<std::decay_t<T>> ||
                  std::is_integral_v<std::decay_t<T>> || std::is_empty_v<std::decay_t<T>>))
    {
        if constexpr (std::is_enum_v<std::decay_t<T>>) {
            *this = Label::from_enum(t);
        } else if constexpr (std::is_pointer_v<std::decay_t<T>>) {
            *this = Label::from_pointer(t);
        } else if constexpr (std::is_integral_v<std::decay_t<T>>) {
            *this = Label::from_integral(t);
        } else if constexpr (std::is_empty_v<std::decay_t<T>>) {
            *this = Label::from_type<std::decay_t<T>>();
        }
    }

    Label() = default;

    epix::core::meta::type_index type_index() const { return type_index_; }
    uintptr_t extra() const { return extra_; }

    std::string to_string() const { return std::format("{}#{:x}", type_index_.short_name(), extra_); }

    bool operator==(const Label& other) const noexcept = default;
    bool operator!=(const Label& other) const noexcept = default;

   protected:
    epix::core::meta::type_index type_index_;
    uintptr_t extra_ = 0;
};

}  // namespace epix::core

// Hash specialization for Label
export template <>
struct std::hash<epix::core::Label> {
    size_t operator()(const epix::core::Label& label) const noexcept {
        size_t hash = std::hash<size_t>()(label.type_index().hash_code());
        hash ^= std::hash<size_t>()(label.extra()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

export template <std::derived_from<epix::core::Label> T>
struct std::hash<T> {
    size_t operator()(const T& label) const noexcept { return std::hash<epix::core::Label>()(label); }
};

// Macro for creating label types (note: macros aren't exported, users must define or use a header)
// Helper template for label creation
export namespace epix::core {

/**
 * @brief Helper to define a strongly-typed label.
 * @tparam Derived The derived label type.
 */
template <typename Derived>
struct LabelBase : public Label {
   public:
    LabelBase() = default;

    template <typename T>
    LabelBase(T t)
        requires(!std::is_same_v<std::decay_t<T>, Derived> && std::is_object_v<T> &&
                 std::constructible_from<Label, T>)
        : Label(t) {}
};

}  // namespace epix::core

// WorldId wrapper
export namespace epix::core {

struct WorldId : public wrapper::int_base<uint64_t> {
    using wrapper::int_base<uint64_t>::int_base;
};

}  // namespace epix::core

// Prelude namespace for convenient imports
export namespace epix::core::prelude {

using meta::type_id;
using meta::type_index;

using type_system::TypeId;
using type_system::TypeInfo;
using type_system::TypeRegistry;

// Entity types
using core::ArchetypeId;
using core::ArchetypeRow;
using core::BundleId;
using core::Entity;
using core::EntityLocation;
using core::EntityMeta;
using core::TableId;
using core::TableRow;

// Tick types
using core::CHECK_TICK_THRESHOLD;
using core::ComponentTicks;
using core::MAX_CHANGE_AGE;
using core::Tick;
using core::TickRefs;
using core::Ticks;
using core::TicksMut;

// Label
using core::Label;

// ID wrappers
using core::WorldId;

}  // namespace epix::core::prelude

export namespace epix::prelude {
using namespace epix::core::prelude;
}

export namespace epix {
using namespace epix::core::prelude;
}
