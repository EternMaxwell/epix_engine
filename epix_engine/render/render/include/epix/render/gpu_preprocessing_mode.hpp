#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#endif

namespace epix::render::batching {

/**
 * @brief The GPU preprocessing strategy selected for a render view (Bevy
 * `GpuPreprocessingMode`).
 *
 * The phase owns this value because it determines both how multidrawable
 * items are queued and which batch-set representation is valid for the view.
 */
EPIX_EXPORT enum class GpuPreprocessingMode : std::uint8_t {
    None,
    PreprocessingOnly,
    Culling,
};

}  // namespace epix::render::batching
