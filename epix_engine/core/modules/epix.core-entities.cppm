/**
 * @file epix.core-entities.cppm
 * @brief Entities partition for entity management
 */

export module epix.core:entities;

import :api;
import :fwd;

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <vector>

export namespace epix::core {
    struct Entity {
        uint32_t generation = 0;
        uint32_t index      = 0;

        auto operator<=>(const Entity&) const = default;
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
        auto reserve_entities(uint32_t count) const;
        Entity reserve_entity() const;
        void flush(std::invocable<Entity, EntityLocation&> auto allocate);
        bool contains(Entity entity) const;
        EntityLocation get(Entity entity) const;
        std::optional<EntityLocation> get_if_exists(Entity entity) const;
        void set(Entity entity, EntityLocation location);
        void clear();
        size_t len() const { return meta.size(); }
        Entity resolve_from_id(uint32_t index) const;
        bool needs_flush() const { return free_cursor.load(std::memory_order_relaxed) != 0; }
        void free(Entity entity);
        size_t total_count() const { return meta.size() - pending.size(); }
    };
}  // namespace epix::core
