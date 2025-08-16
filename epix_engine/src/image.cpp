#include <stb_image.h>

#include "epix/image.h"

using namespace epix::image;

EPIX_API std::span<const char* const> StbImageLoader::extensions() noexcept {
    static constexpr auto exts =
        std::array{".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr",
                   ".pic", ".psd", ".gif",  ".ppm", ".pgm", ".pnm"};
    return exts;
}
EPIX_API Image StbImageLoader::load(const std::filesystem::path& path,
                                    assets::LoadContext& context) {
    Image image;
    int width, height, channels;
    auto path_str = path.string();
    bool hdr      = stbi_is_hdr(path_str.c_str());
    bool bit16    = stbi_is_16_bit(path_str.c_str());

    // default infos for stbi loaded images
    image.info.dimension        = nvrhi::TextureDimension::Texture2D;
    image.info.initialState     = nvrhi::ResourceStates::ShaderResource;
    image.info.keepInitialState = true;

    if (hdr) {
        // this is an HDR image, use float for storage
        auto loaded =
            stbi_loadf(path_str.c_str(), &width, &height, &channels, 4);
        if (!loaded) {
            throw std::runtime_error("Fail to load HDR image: " + path_str);
        }
        image.data.resize(width * height * 4 * sizeof(float));
        std::memcpy(image.data.data(), loaded, image.data.size());
        stbi_image_free(loaded);
        image.info.format = nvrhi::Format::RGBA32_FLOAT;
    } else if (bit16) {
        // this is a 16-bit image, use uint16_t for storage
        auto loaded =
            stbi_load_16(path_str.c_str(), &width, &height, &channels, 4);
        if (!loaded) {
            throw std::runtime_error("Fail to load 16-bit image: " + path_str);
        }
        image.data.resize(width * height * 4 * sizeof(uint16_t));
        std::memcpy(image.data.data(), loaded, image.data.size());
        stbi_image_free(loaded);
        image.info.format = nvrhi::Format::RGBA16_UINT;
    } else {
        // this is an 8-bit image, use uint8_t for storage
        auto loaded =
            stbi_load(path_str.c_str(), &width, &height, &channels, 4);
        if (!loaded) {
            throw std::runtime_error("Fail to load 8-bit image: " + path_str);
        }
        image.data.resize(width * height * 4);
        std::memcpy(image.data.data(), loaded, image.data.size());
        stbi_image_free(loaded);
        image.info.format = nvrhi::Format::RGBA8_UINT;
    }

    // size assigned here after all possible branches
    image.info.width     = width;
    image.info.height    = height;
    image.info.depth     = 1;
    image.info.arraySize = 1;
    image.info.mipLevels = 1;

    image.usage = render::assets::RenderAssetUsageBits::RENDER_WORLD;

    return std::move(image);
}

using namespace epix::render::assets;

EPIX_API void ImagePlugin::build(epix::App& app) {
    app.plugin_scope([](epix::assets::AssetPlugin& asset_plugin) {
        asset_plugin.register_asset<Image>().register_loader<StbImageLoader>();
    });
    app.add_plugins(ExtractAssetPlugin<Image>{});
}

using Param          = epix::render::assets::RenderAsset<Image>::Param;
using ProcessedAsset = epix::render::assets::RenderAsset<Image>::ProcessedAsset;

EPIX_API ProcessedAsset RenderAsset<Image>::process(Image&& asset,
                                                    Param& param) {
    auto&& [device]              = param.get();
    nvrhi::TextureHandle texture = device.get()->createTexture(asset.info);
    auto commandlist             = device.get()->createCommandList();
    commandlist->open();
    size_t rowPitch = asset.data.size() / asset.info.height;
    commandlist->writeTexture(texture, 0, 0, asset.data.data(), rowPitch);
    commandlist->close();
    device.get()->executeCommandList(commandlist);
    return texture;
}
EPIX_API RenderAssetUsage RenderAsset<Image>::usage(const Image& asset) {
    return asset.usage;
}