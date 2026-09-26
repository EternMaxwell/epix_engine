#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief The index of a material bind group (Bevy `MaterialBindGroupIndex`,
 *  `bevy_pbr/src/material_bind_groups.rs:275`).
 *
 *  In bindless mode a bind group holds many materials and slots index into it; in
 *  non-bindless mode each bind group holds one. */
EPIX_EXPORT struct MaterialBindGroupIndex {
    std::uint32_t index                                   = 0;
    auto operator<=>(const MaterialBindGroupIndex&) const = default;
};

/** @brief A slot within a material bind group (Bevy `MaterialBindGroupSlot`,
 *  `material_bind_groups.rs:291`). Non-bindless materials always use slot 0. */
EPIX_EXPORT struct MaterialBindGroupSlot {
    std::uint32_t slot                                   = 0;
    auto operator<=>(const MaterialBindGroupSlot&) const = default;
};

/** @brief Where a material's GPU data lives (Bevy `MaterialBindingId`,
 *  `material_bind_groups.rs:260-267`): the bind group and the slot inside it. */
EPIX_EXPORT struct MaterialBindingId {
    MaterialBindGroupIndex group{};
    MaterialBindGroupSlot slot{};
    bool operator==(const MaterialBindingId&) const = default;
};

}  // namespace epix::pbr
