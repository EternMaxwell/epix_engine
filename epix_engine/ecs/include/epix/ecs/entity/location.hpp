#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/utils.hpp>
#endif

namespace epix::ecs {
namespace internal {
struct BundleId : epix::utils::int_base<std::uint64_t> {};
}  // namespace internal

/** @brief Strongly-typed archetype identifier. */
EPIX_EXPORT struct ArchetypeId : epix::utils::int_base<std::uint32_t> {};
/** @brief Strongly-typed table identifier. */
EPIX_EXPORT struct TableId : epix::utils::int_base<std::uint32_t> {};
/** @brief Strongly-typed row index within an archetype's entity list. */
EPIX_EXPORT struct ArchetypeRow : epix::utils::int_base<std::uint32_t> {};
/** @brief Strongly-typed row index within a table. */
EPIX_EXPORT struct TableRow : epix::utils::int_base<std::uint32_t> {};

/** @brief Location of an entity within the ECS storage.
 *  Tracks which archetype and table an entity belongs to. */
EPIX_EXPORT struct EntityLocation {
    /** @brief Archetype this entity belongs to. */
    ArchetypeId archetype_id{0};
    /** @brief Row within the archetype's entity list. */
    ArchetypeRow archetype_idx{0};
    /** @brief Table storing this entity's components. */
    TableId table_id{0};
    /** @brief Row within the table. */
    TableRow table_idx{0};

    bool operator==(const EntityLocation& other) const = default;
    bool operator!=(const EntityLocation& other) const = default;

    /** @brief Return a sentinel location with all fields set to max value. */
    static constexpr EntityLocation invalid() noexcept {
        return {std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max(),
                std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max()};
    }
};
}  // namespace epix::ecs