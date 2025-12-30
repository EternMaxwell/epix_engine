module;

#define EPIX_MAKE_INT_WRAPPER(name, type) \
    struct name : core::int_base<type> {  \
        using int_base::int_base;         \
    };

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <ranges>
#include <vector>

export module epix.core:entities;

import :utils;

namespace core {
export struct Entity {
    union {
        struct {
            std::uint32_t generation;
            std::uint32_t index;
        };
        std::uint64_t uid = 0;
    };

    bool operator==(const Entity& other) const { return uid == other.uid; }
    auto operator<=>(const Entity& other) const { return uid <=> other.uid; }
    static Entity from_index(std::uint32_t index) { return Entity{0, index}; }
    static Entity from_parts(std::uint32_t index, std::uint32_t generation) { return Entity{generation, index}; }
};
export EPIX_MAKE_INT_WRAPPER(ArchetypeId, std::uint32_t);
export EPIX_MAKE_INT_WRAPPER(TableId, std::uint32_t);
export EPIX_MAKE_INT_WRAPPER(ArchetypeRow, std::uint32_t);
export EPIX_MAKE_INT_WRAPPER(TableRow, std::uint32_t);
EPIX_MAKE_INT_WRAPPER(BundleId, std::uint64_t);

export struct EntityLocation {
    ArchetypeId archetype_id   = 0;
    ArchetypeRow archetype_idx = 0;
    TableId table_id           = 0;
    TableRow table_idx         = 0;

    bool operator==(const EntityLocation& other) const = default;
    bool operator!=(const EntityLocation& other) const = default;

    static constexpr EntityLocation invalid() {
        return {std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max(),
                std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max()};
    }
};
struct EntityMeta {
    std::uint32_t generation = 0;
    EntityLocation location  = EntityLocation::invalid();

    static constexpr EntityMeta empty() { return {0, EntityLocation::invalid()}; }
};
export struct Entities {
   private:
    std::vector<EntityMeta> meta;
    std::vector<std::uint32_t> pending;
    mutable std::atomic<std::int64_t> free_cursor = 0;

   public:
    /**
     * @brief Reserve entity IDs concurrently.
     *
     * Storage for entity generation and location is lazily allocated by calling [`flush`](Entities::flush).
     *
     * @param count Number of entities to reserve.
     * @return A viewable range of reserved entities.
     */
    auto reserve_entities(std::uint32_t count) const {
        std::int64_t range_end   = free_cursor.fetch_sub(count, std::memory_order_relaxed);
        std::int64_t range_start = range_end - count;

        std::int64_t base = meta.size();

        // The following code needs cpp26, for views::concat

        // auto free_range = std::views::iota(std::max(range_start, std::int64_t{0}), std::max(range_end,
        // std::int64_t{0})) |
        //                   std::views::transform([this](std::int64_t idx) {
        //                       return Entity::from_parts(pending[idx], meta[pending[idx]].generation);
        //                   });
        // auto new_end   = base - std::min(range_start, std::int64_t{0});
        // auto new_start = base - std::min(range_end, std::int64_t{0});
        // auto new_range = std::views::iota(new_start, new_end) | std::views::transform([this](std::int64_t idx) {
        //                      return Entity::from_index(static_cast<std::uint32_t>(idx));
        //                  });

        // return std::views::concat(free_range, new_range);

        return std::views::iota(range_start, range_end) | std::views::transform([this, base](std::int64_t idx) {
                   if (idx < 0) {
                       // This entity is newly allocated
                       return Entity::from_index(static_cast<std::uint32_t>(base - idx - 1));
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
        std::int64_t n = free_cursor.fetch_sub(1, std::memory_order_relaxed);
        if (n > 0) {
            // from free list
            std::uint32_t idx = pending[n - 1];
            return Entity::from_parts(idx, meta[idx].generation);
        } else {
            std::uint32_t idx = static_cast<std::uint32_t>(meta.size() - n);
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
    void reserve(std::uint32_t count);

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
    void set(std::uint32_t index, EntityLocation location);

    /**
     * @brief Increments the `generation` of a freed entity by `generations`.
     *
     * Does nothing if no entity with this `index` has been allocated yet.
     *
     * @return true if successful.
     */
    bool reserve_generations(std::uint32_t index, std::uint32_t generations);

    /**
     * @brief Get the entity with the given index, if it exists. Returns std::nullopt if the index is out of range of
     * currently reserved entities.
     *
     * Note that this function will return currently freed entities.
     */
    std::optional<Entity> resolve_index(std::uint32_t index) const;

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
        std::int64_t n = free_cursor.load(std::memory_order_relaxed);
        if (n < 0) {
            auto old_meta_len = meta.size();
            auto new_meta_len = old_meta_len + static_cast<std::size_t>(-n);
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
    std::size_t total_count() const { return meta.size(); }
    /**
     * @brief Count of entities that are used.
     *
     * Including ones that are allocated and reserved but not those that are freed.
     */
    std::size_t used_count() const {
        std::int64_t size = meta.size();
        return size - free_cursor.load(std::memory_order_relaxed);
    }
    /**
     * @brief The count of all entities that have ever been allocated or reserved, including those that are freed.
     *
     * This is the value that `total_count()` would return if `flush()` were called right now.
     */
    std::size_t total_prospective_count() const {
        return meta.size() +
               static_cast<std::size_t>(-std::min(free_cursor.load(std::memory_order_relaxed), std::int64_t{0}));
    }
    /**
     * @brief Count of currently allocated entities.
     */
    std::size_t size() const { return meta.size() - pending.size(); }
    /**
     * @brief Checks if any entity is currently active.
     */
    bool empty() const { return size() == 0; }
};
}  // namespace core

template <>
struct std::hash<::core::Entity> {
    std::size_t operator()(::core::Entity e) const { return std::hash<std::uint64_t>()(e.uid); }
};