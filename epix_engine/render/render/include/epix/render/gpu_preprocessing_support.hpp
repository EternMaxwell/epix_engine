#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <optional>
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
    static GpuPreprocessingSupport from_device(const wgpu::Device& device,
                                               std::optional<wgpu::BackendType> backend_type = std::nullopt) noexcept {
        // Bevy's gate is based on compute support for preprocessing, then on
        // indirect-first-instance, push constants, and texture/compute limits
        // for the stronger occlusion-culling mode. Multi-draw is useful to
        // render a prepared batch but is not a prerequisite for preprocessing
        // itself.
        wgpu::Limits limits;
        device.getLimits(&limits);
        const bool compute_supported = limits.maxComputeWorkgroupSizeX != 0;
        const bool is_gl_backend     = backend_type && *backend_type == wgpu::BackendType::eOpenGL;
        if (!compute_supported || is_gl_backend) return {};

        const bool culling_features = device.hasFeature(wgpu::FeatureName::eIndirectFirstInstance) &&
                                      device.hasFeature(wgpu::FeatureName(wgpu::NativeFeature::ePushConstants));
        const bool culling_limits =
            limits.maxStorageTexturesPerShaderStage >= 12 && limits.maxComputeWorkgroupStorageSize != 0;
        return {culling_features && culling_limits ? GpuPreprocessingMode::Culling
                                                   : GpuPreprocessingMode::PreprocessingOnly};
    }
};

}  // namespace epix::render::batching
