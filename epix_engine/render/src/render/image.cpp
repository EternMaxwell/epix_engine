#include "epix/render/image.hpp"

using namespace epix::render::assets;
using namespace epix::image;

using Param          = RenderAsset<Image>::Param;
using ProcessedAsset = RenderAsset<Image>::ProcessedAsset;

ProcessedAsset RenderAsset<Image>::process(Image&& asset, Param& param) {
    auto&& device                = param.get();
    nvrhi::TextureHandle texture = device->createTexture(asset.get_desc());
    auto commandlist = device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
    commandlist->open();
    size_t rowPitch = asset.get_data().size() / asset.get_desc().height;
    commandlist->writeTexture(texture, 0, 0, asset.get_data().data(), rowPitch);
    commandlist->close();
    device->executeCommandList(commandlist);
    return texture;
}
RenderAssetUsage RenderAsset<Image>::usage(const Image& asset) {
    RenderAssetUsage usage = 0;
    if (asset.is_main_world()) {
        usage |= RenderAssetUsageBits::MAIN_WORLD;
    }
    if (asset.is_render_world()) {
        usage |= RenderAssetUsageBits::RENDER_WORLD;
    }
    return usage;
}