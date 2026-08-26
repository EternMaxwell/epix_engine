#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/utils.hpp>
#include <utility>
#endif

#define EPIX_MAKE_INT_WRAPPER(name, type)       \
    struct name : epix::utils::int_base<type> { \
        using int_base::int_base;               \
    }

namespace epix::ecs {
/** @brief Lightweight entity identifier composed of a generation counter and a slot index.
 *  Two entities with the same index but different generations are distinct. */
EPIX_EXPORT struct Entity {
    union {
        struct {
            /** @brief Generation counter; incremented when the slot is recycled. */
            std::uint32_t generation;
            /** @brief Slot index into the entity storage. */
            std::uint32_t index;
        };
        /** @brief Combined 64-bit unique id (generation | index). */
        std::uint64_t uid = 0;
    };

    bool operator==(const Entity& other) const noexcept { return uid == other.uid; }
    auto operator<=>(const Entity& other) const noexcept { return uid <=> other.uid; }
    /** @brief Create an entity with generation 0 from a slot index. */
    static Entity from_index(std::uint32_t index) noexcept { return Entity{0, index}; }
    /** @brief Create an entity from a slot index and generation counter. */
    static Entity from_parts(std::uint32_t index, std::uint32_t generation) noexcept {
        return Entity{generation, index};
    }

    /** @brief Sentinel entity for render-world mappings that have no backing
     * render entity yet (Bevy
     * `Entity::PLACEHOLDER`). */
    static const Entity PLACEHOLDER;
};

inline const Entity Entity::PLACEHOLDER{UINT32_MAX, UINT32_MAX};
}  // namespace epix::ecs

template <>
struct std::hash<::epix::ecs::Entity> {
    std::size_t operator()(::epix::ecs::Entity e) const noexcept { return std::hash<std::uint64_t>()(e.uid); }
};
