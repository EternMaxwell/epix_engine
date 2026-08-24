#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#endif

namespace epix::render {

/** @brief Debugging flags that can optionally be set when constructing the
 * renderer or a render phase (Bevy `RenderDebugFlags`). */
EPIX_EXPORT struct RenderDebugFlags {
    /** @brief Raw flag storage (bitflags over u8). */
    std::uint8_t bits = 0;

    constexpr static std::uint8_t ALLOW_COPIES_FROM_INDIRECT_PARAMETERS = 1;

    /** @brief Whether indirect draw parameters get the COPY_SRC flag for CPU
     * readback (Bevy ALLOW_COPIES_FROM_INDIRECT_PARAMETERS). */
    bool allow_copies_from_indirect_parameters() const noexcept {
        return (bits & ALLOW_COPIES_FROM_INDIRECT_PARAMETERS) != 0;
    }
    void set_allow_copies_from_indirect_parameters(bool value = true) noexcept {
        bits = value ? (bits | ALLOW_COPIES_FROM_INDIRECT_PARAMETERS) : (bits & ~ALLOW_COPIES_FROM_INDIRECT_PARAMETERS);
    }
};

}  // namespace epix::render
