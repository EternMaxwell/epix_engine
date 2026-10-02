
#include <epix/render/assets.hpp>

namespace epix::render {

std::string_view gpu_asset_creation_error_message(GpuAssetCreationError error) noexcept {
    switch (error) {
        case GpuAssetCreationError::CreateTexture:
            return "failed to create texture";
        case GpuAssetCreationError::CreateTextureView:
            return "failed to create texture view";
        case GpuAssetCreationError::CreateSampler:
            return "failed to create sampler";
        case GpuAssetCreationError::CreateBuffer:
            return "failed to create buffer";
    }
    return "unknown GPU asset creation failure";
}

}  // namespace epix::render
