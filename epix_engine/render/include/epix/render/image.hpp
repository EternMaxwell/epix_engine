#include <epix/image.hpp>

#include "assets.hpp"

template <>
struct epix::render::assets::RenderAsset<epix::image::Image> {
    using Param          = Res<nvrhi::DeviceHandle>;
    using ProcessedAsset = nvrhi::TextureHandle;

    ProcessedAsset process(image::Image&& asset, Param& param);
    RenderAssetUsage usage(const image::Image& asset);
};