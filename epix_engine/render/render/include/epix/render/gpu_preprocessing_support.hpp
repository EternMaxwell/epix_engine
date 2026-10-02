#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <optional>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/render/gpu_preprocessing_mode.hpp>

namespace epix::render::batching {

/** @brief Adapter capability summary for GPU preprocessing (Bevy
 * `GpuPreprocessingSupport`, batching/gpu_preprocessing.rs). */
EPIX_EXPORT struct GpuPreprocessingSupport {
    GpuPreprocessingMode max_supported_mode = GpuPreprocessingMode::None;

    bool is_available() const noexcept { return max_supported_mode != GpuPreprocessingMode::None; }
    bool is_culling_supported() const noexcept { return max_supported_mode == GpuPreprocessingMode::Culling; }
    GpuPreprocessingMode min(GpuPreprocessingMode mode) const noexcept {
        if (max_supported_mode == GpuPreprocessingMode::None || mode == GpuPreprocessingMode::None) {
            return GpuPreprocessingMode::None;
        }
        if (max_supported_mode == GpuPreprocessingMode::Culling) return mode;
        return GpuPreprocessingMode::PreprocessingOnly;
    }
    EPIX_API static GpuPreprocessingSupport from_device(const wgpu::Device& device,
                                                        std::optional<wgpu::BackendType> backend_type =
                                                            std::nullopt) noexcept;
};

}  // namespace epix::render::batching
