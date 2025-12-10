/**
 * @file epix.core.entities.cppm
 * @brief C++20 module interface for entity management in the ECS.
 *
 * This module provides the Entity type and Entities manager for creating,
 * destroying, and tracking entities in the ECS world.
 */
module;

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <vector>

export module epix.core.entities;
export import epix.core.api;

export namespace epix::core {

/**
 * @brief A unique identifier for an entity in the ECS world.
 *
 * Entities are represented as a generation counter and an index.
 * The generation ensures that reused indices can be distinguished
 * from their previous occupants.
 */
struct Entity {
    uint32_t generation = 0;  ///< Generation counter for reuse detection
    uint32_t index      = 0;  ///< Index into the entity storage

    auto operator<=>(const Entity&) const = default;

    /**
     * @brief Create an entity from just an index (generation 0).
     */
    static Entity from_index(uint32_t index) { return Entity{0, index}; }

    /**
     * @brief Create an entity from both parts.
     */
    static Entity from_parts(uint32_t index, uint32_t generation) { return Entity{generation, index}; }
};

// ID wrapper types for ECS indices
struct ArchetypeId : public wrapper::int_base<uint32_t> {
    using wrapper::int_base<uint32_t>::int_base;
};

struct TableId : public wrapper::int_base<uint32_t> {
    using wrapper::int_base<uint32_t>::int_base;
};

struct ArchetypeRow : public wrapper::int_base<uint32_t> {
    using wrapper::int_base<uint32_t>::int_base;
};

struct TableRow : public wrapper::int_base<uint32_t> {
    using wrapper::int_base<uint32_t>::int_base;
};

struct BundleId : public wrapper::int_base<uint64_t> {
    using wrapper::int_base<uint64_t>::int_base;
};

/**
 * @brief Location of an entity within the ECS storage.
 *
 * Tracks which archetype and table an entity belongs to,
 * and its position within those containers.
 */
struct EntityLocation {
    ArchetypeId archetype_id   = 0;
    ArchetypeRow archetype_idx = 0;
    TableId table_id           = 0;
    TableRow table_idx         = 0;

    bool operator==(const EntityLocation& other) const = default;
    bool operator!=(const EntityLocation& other) const = default;

    /**
     * @brief Create an invalid location sentinel.
     */
    static constexpr EntityLocation invalid() {
        return {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(),
                std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};
    }
};

/**
 * @brief Metadata for a single entity.
 */
struct EntityMeta {
    uint32_t generation     = 0;
    EntityLocation location = EntityLocation::invalid();

    static constexpr EntityMeta empty() { return {0, EntityLocation::invalid()}; }
};

/**
 * @brief Manager for entity allocation, deallocation, and location tracking.
 *
 * Supports concurrent entity reservation with lock-free operations,
 * while mutations require exclusive access.
 */
struct Entities {
   private:
    std::vector<EntityMeta> meta;
    std::vector<uint32_t> pending;
    mutable std::atomic<int64_t> free_cursor = 0;

   public:
    /**
     * @brief Reserve entity IDs concurrently.
     *
     * Storage for entity generation and location is lazily allocated
     * by calling flush().
     *
     * @param count Number of entities to reserve.
     * @return A viewable range of reserved entities.
     */
    auto reserve_entities(uint32_t count) const {
        int64_t range_end   = free_cursor.fetch_sub(count, std::memory_order_relaxed);
        int64_t range_start = range_end - count;
        int64_t base        = meta.size();

        return std::views::iota(range_start, range_end) | std::views::transform([this, base](int64_t idx) {
                   if (idx < 0) {
                       return Entity::from_index(static_cast<uint32_t>(base - idx - 1));
                   } else {
                       return Entity::from_parts(pending[idx], meta[pending[idx]].generation);
                   }
               });
    }

    /**
     * @brief Reserve a single entity ID.
     * @return The reserved entity.
     */
    Entity reserve_entity() const {
        int64_t n = free_cursor.fetch_sub(1, std::memory_order_relaxed);
        if (n > 0) {
            uint32_t idx = pending[n - 1];
            return Entity::from_parts(idx, meta[idx].generation);
        } else {
            uint32_t idx = static_cast<uint32_t>(meta.size() - n);
            return Entity::from_index(idx);
        }
    }

    /**
     * @brief Verify that no pending work requires flush().
     */
    void verify_flush() {
        assert(!needs_flush() && "Entities need to be flushed before accessing meta or pending!");
    }

    /**
     * @brief Allocate an entity ID directly.
     * @return The allocated entity.
     */
    Entity alloc();

    /**
     * @brief Destroy an entity, allowing it to be reused.
     * @param entity The entity to free.
     * @return The entity's location if it existed.
     */
    std::optional<EntityLocation> free(Entity entity);

    /**
     * @brief Ensure capacity for at least `count` allocations.
     */
    void reserve(uint32_t count);

    /**
     * @brief Check if the entity exists and is valid.
     */
    bool contains(Entity entity) const;

    /**
     * @brief Clear all entities.
     */
    void clear();

    /**
     * @brief Get the location of an entity.
     * @return The location if the entity is valid, nullopt otherwise.
     */
    std::optional<EntityLocation> get(Entity entity) const;

    /**
     * @brief Update the location of an entity.
     */
    void set(uint32_t index, EntityLocation location);

    /**
     * @brief Increment the generation of a freed entity.
     * @return True if successful.
     */
    bool reserve_generations(uint32_t index, uint32_t generations);

    /**
     * @brief Get the entity with the given index.
     * @return The entity if the index is valid, nullopt otherwise.
     */
    std::optional<Entity> resolve_index(uint32_t index) const;

    /**
     * @brief Check if flush() is needed.
     */
    bool needs_flush() const;

    /**
     * @brief Allocate space for reserved entities and initialize them.
     * @tparam Fn Initializer function taking (Entity, EntityLocation&).
     */
    template <std::invocable<Entity, EntityLocation&> Fn>
    void flush(Fn&& fn) {
        int64_t n = free_cursor.load(std::memory_order_relaxed);
        if (n < 0) {
            auto old_meta_len = meta.size();
            auto new_meta_len = old_meta_len + static_cast<size_t>(-n);
            meta.resize(new_meta_len);
            for (auto&& [index, m] : std::views::enumerate(meta) | std::views::drop(old_meta_len)) {
                fn(Entity::from_parts(index, m.generation), m.location);
            }
            free_cursor.store(0, std::memory_order_relaxed);
            n = 0;
        }

        for (auto&& index : pending | std::views::drop(n)) {
            auto& m = this->meta[index];
            fn(Entity::from_parts(index, m.generation), m.location);
        }
        pending.resize(n);
    }

    /**
     * @brief Flush all reserved entities to invalid state.
     */
    void flush_as_invalid();

    /**
     * @brief Get total count of ever-allocated entities.
     */
    size_t total_count() const { return meta.size(); }

    /**
     * @brief Count of entities in use (allocated + reserved).
     */
    size_t used_count() const {
        int64_t size = meta.size();
        return size - free_cursor.load(std::memory_order_relaxed);
    }

    /**
     * @brief Total prospective count after flush.
     */
    size_t total_prospective_count() const {
        return meta.size() + static_cast<size_t>(-std::min(free_cursor.load(std::memory_order_relaxed), int64_t{0}));
    }

    /**
     * @brief Count of currently allocated (non-freed) entities.
     */
    size_t size() const { return meta.size() - pending.size(); }

    /**
     * @brief Check if no entities are active.
     */
    bool empty() const { return size() == 0; }
};

}  // namespace epix::core

// Hash specialization for Entity
export template <>
struct std::hash<epix::core::Entity> {
    size_t operator()(epix::core::Entity e) const {
        return std::hash<uint64_t>()(static_cast<uint64_t>(e.generation) << 32 | e.index);
    }
};
