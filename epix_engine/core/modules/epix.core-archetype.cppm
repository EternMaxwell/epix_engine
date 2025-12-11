/**
 * @file epix.core-archetype.cppm
 * @brief Archetype partition for archetype management
 */

export module epix.core:archetype;

import :fwd;
import :entities;
import :type_system;
import :component;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <vector>

export namespace epix::core::archetype {
    struct ArchetypeEntity {
        Entity entity;
        TableRow table_idx;
    };
    
    struct ArchetypeRecord {
        std::optional<size_t> table_dense;
    };
    
    struct ArchetypeComponents {
        std::vector<TypeId> table_components;
        std::vector<TypeId> sparse_components;
    };
    
    struct ArchetypeComponentsHash {
        size_t operator()(const ArchetypeComponents& ac) const {
            size_t hash = 0;
            for (auto type_id : ac.table_components) {
                hash ^= std::hash<uint32_t>()(type_id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            for (auto type_id : ac.sparse_components) {
                hash ^= std::hash<uint32_t>()(type_id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };
    
    struct ArchetypeComponentsEqual {
        bool operator()(const ArchetypeComponents& a, const ArchetypeComponents& b) const {
            return a.table_components == b.table_components && a.sparse_components == b.sparse_components;
        }
    };
    
    using ComponentIndex = std::unordered_map<TypeId, std::unordered_map<ArchetypeId, ArchetypeRecord, std::hash<uint32_t>>>;
    
    enum class ComponentStatus {
        Added,
        Exists,
    };
}  // namespace epix::core::archetype

export namespace epix::core {
    struct Archetype {
        // Archetype implementation
    };
    
    struct Archetypes {
        // Archetypes collection
    };
}  // namespace epix::core
