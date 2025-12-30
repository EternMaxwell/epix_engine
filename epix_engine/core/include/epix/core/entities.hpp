#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <ranges>
#include <vector>

#include "../api/macros.hpp"
#include "fwd.hpp"

namespace epix::core {
struct Entity {
    union {
        struct {
            uint32_t generation;
            uint32_t index;
        };
        uint64_t uid = 0;
    };

    bool operator==(const Entity& other) const { return uid == other.uid; }
    auto operator<=>(const Entity& other) const { return uid <=> other.uid; }
    static Entity from_index(uint32_t index) { return Entity{0, index}; }
    static Entity from_parts(uint32_t index, uint32_t generation) { return Entity{generation, index}; }
};
EPIX_MAKE_U32_WRAPPER(ArchetypeId)
EPIX_MAKE_U32_WRAPPER(TableId)
EPIX_MAKE_U32_WRAPPER(ArchetypeRow)
EPIX_MAKE_U32_WRAPPER(TableRow)
EPIX_MAKE_U64_WRAPPER(BundleId)
struct EntityLocation {
    ArchetypeId archetype_id   = 0;
    ArchetypeRow archetype_idx = 0;
    TableId table_id           = 0;
    TableRow table_idx         = 0;

    bool operator==(const EntityLocation& other) const = default;
    bool operator!=(const EntityLocation& other) const = default;

    static constexpr EntityLocation invalid() {
        return {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(),
                std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};
    }
};
struct EntityMeta {
    uint32_t generation     = 0;
    EntityLocation location = EntityLocation::invalid();

    static constexpr EntityMeta empty() { return {0, EntityLocation::invalid()}; }
};
struct Entities {
   private:
    std::vector<EntityMeta> meta;
    std::vector<uint32_t> pending;
    mutable std::atomic<int64_t> free_cursor = 0;

   public:
    /**
     * @brief Reserve entity IDs concurrently.
     *
     * Storage for entity generation and location is lazily allocated by calling [`flush`](Entities::flush).
     *
     * @param count Number of entities to reserve.
     * @return A viewable range of reserved entities.
     */
    auto reserve_entities(uint32_t count) const {
        int64_t range_end   = free_cursor.fetch_sub(count, std::memory_order_relaxed);
        int64_t range_start = range_end - count;

        int64_t base = meta.size();

        // The following code needs cpp26, for views::concat

        // auto free_range = std::views::iota(std::max(range_start, int64_t{0}), std::max(range_end, int64_t{0})) |
        //                   std::views::transform([this](int64_t idx) {
        //                       return Entity::from_parts(pending[idx], meta[pending[idx]].generation);
        //                   });
        // auto new_end   = base - std::min(range_start, int64_t{0});
        // auto new_start = base - std::min(range_end, int64_t{0});
        // auto new_range = std::views::iota(new_start, new_end) | std::views::transform([this](int64_t idx) {
        //                      return Entity::from_index(static_cast<uint32_t>(idx));
        //                  });

        // return std::views::concat(free_range, new_range);

        return std::views::iota(range_start, range_end) | std::views::transform([this, base](int64_t idx) {
                   if (idx < 0) {
                       // This entity is newly allocated
                       return Entity::from_index(static_cast<uint32_t>(base - idx - 1));
                   } else {
                       return Entity::from_parts(pending[idx], meta[pending[idx]].generation);
                   }
               });
    }
    /**
     * @brief Reserve a single entity ID.
     *
     * This is same as calling `*reserve_entities(1).begin()`. But more efficient.
     *
     * @return The reserved entity.
     */
    Entity reserve_entity() const {
        int64_t n = free_cursor.fetch_sub(1, std::memory_order_relaxed);
        if (n > 0) {
            // from free list
            uint32_t idx = pending[n - 1];
            return Entity::from_parts(idx, meta[idx].generation);
        } else {
            uint32_t idx = static_cast<uint32_t>(meta.size() - n);
            return Entity::from_index(idx);
        }
    }

    /**
     * @brief Check that we do not have pending work requiring `flush()`.
     *
     */
    void verify_flush();

    /**
     * @brief Allocate a Entity id directly.
     *
     * @return The allocated entity.
     */
    Entity alloc();

    /**
     * @brief Destroy an entity, allowing it to be reused.
     *
     * Note: Must not be called when any reserved entities are awaiting `flush()`.
     *
     * @param entity The entity to free.
     * @return std::optional<EntityLocation> The entity location if any.
     */
    std::optional<EntityLocation> free(Entity entity);

    /**
     * @brief Ensure at least `count` allocations can be made without reallocating.
     *
     * @param count
     */
    void reserve(uint32_t count);

    /**
     * @brief Returns true if contains the given entity.
     *
     * Entities that are freed will return false.
     */
    bool contains(Entity entity) const;

    /**
     * @brief Clear all entities, making the manager empty.
     */
    void clear();

    /**
     * @brief Get the location of an entity. Will return std::nullopt for pending entities.
     */
    std::optional<EntityLocation> get(Entity entity) const;

    /**
     * @brief Updates the location of an entity. Must be called when moving the components of the entity around in
     * storage.
     */
    void set(uint32_t index, EntityLocation location);

    /**
     * @brief Increments the `generation` of a freed entity by `generations`.
     *
     * Does nothing if no entity with this `index` has been allocated yet.
     *
     * @return true if successful.
     */
    bool reserve_generations(uint32_t index, uint32_t generations);

    /**
     * @brief Get the entity with the given index, if it exists. Returns std::nullopt if the index is out of range of
     * currently reserved entities.
     *
     * Note that this function will return currently freed entities.
     */
    std::optional<Entity> resolve_index(uint32_t index) const;

    bool needs_flush() const;

    /**
     * @brief Allocate space for entities previously reserved by `reserve_entities` or `reserve_entity`, then initialize
     * them using the provided function.
     *
     * @tparam Fn
     * @param fn
     */
    template <std::invocable<Entity, EntityLocation&> Fn>
    void flush(Fn&& fn) {
        // set cursor to 0 if negative and get new cursor
        int64_t n = free_cursor.load(std::memory_order_relaxed);
        if (n < 0) {
            auto old_meta_len = meta.size();
            auto new_meta_len = old_meta_len + static_cast<size_t>(-n);
            meta.resize(new_meta_len);
            for (auto&& [index, meta] : std::views::enumerate(meta) | std::views::drop(old_meta_len)) {
                fn(Entity::from_parts(index, meta.generation), meta.location);
            }

            free_cursor.store(0, std::memory_order_relaxed);
            n = 0;
        }

        for (auto&& index : pending | std::views::drop(n)) {
            auto& meta = this->meta[index];
            fn(Entity::from_parts(index, meta.generation), meta.location);
        }
        pending.resize(n);
    };

    /**
     * @brief Flushes all reserved entities to invalid state.
     */
    void flush_as_invalid();

    /**
     * @brief Get the total number of entities that have ever been allocated. Including freed ones.
     *
     * Does not include entities that have been reserved but not yet allocated.
     */
    size_t total_count() const { return meta.size(); }
    /**
     * @brief Count of entities that are used.
     *
     * Including ones that are allocated and reserved but not those that are freed.
     */
    size_t used_count() const {
        int64_t size = meta.size();
        return size - free_cursor.load(std::memory_order_relaxed);
    }
    /**
     * @brief The count of all entities that have ever been allocated or reserved, including those that are freed.
     *
     * This is the value that `total_count()` would return if `flush()` were called right now.
     */
    size_t total_prospective_count() const {
        return meta.size() + static_cast<size_t>(-std::min(free_cursor.load(std::memory_order_relaxed), int64_t{0}));
    }
    /**
     * @brief Count of currently allocated entities.
     */
    size_t size() const { return meta.size() - pending.size(); }
    /**
     * @brief Checks if any entity is currently active.
     */
    bool empty() const { return size() == 0; }
};
}  // namespace epix::core

// Add hash for uint wrappers
namespace std {
template <>
struct hash<epix::core::Entity> {
    size_t operator()(epix::core::Entity e) const {
        return std::hash<uint64_t>()(static_cast<uint64_t>(e.generation) << 32 | e.index);
    }
};
}  // namespace std