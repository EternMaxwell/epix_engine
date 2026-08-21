#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <tuple>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/render/assets.hpp>

namespace epix::render {
/** @brief Resource holding the default sampler used for image textures. */
EPIX_EXPORT struct DefaultImageSampler {
    /** @brief The default GPU sampler. */
    wgpu::Sampler sampler;
};
}  // namespace epix::render