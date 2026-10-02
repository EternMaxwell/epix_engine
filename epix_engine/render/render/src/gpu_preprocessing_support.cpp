
#include <epix/render/gpu_preprocessing_support.hpp>

namespace epix::render::batching {

GpuPreprocessingSupport GpuPreprocessingSupport::from_device(const wgpu::Device& device,
                                                             std::optional<wgpu::BackendType> backend_type) noexcept {
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
                                  device.hasFeature(wgpu::FeatureName(wgpu::NativeFeature::eImmediates));
    const bool culling_limits =
        limits.maxStorageTexturesPerShaderStage >= 12 && limits.maxComputeWorkgroupStorageSize != 0;
    return {culling_features && culling_limits ? GpuPreprocessingMode::Culling
                                               : GpuPreprocessingMode::PreprocessingOnly};
}

}  // namespace epix::render::batching
