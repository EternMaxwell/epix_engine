#include <spdlog/spdlog.h>

#include <epix/assets.hpp>
#include <epix/render.hpp>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::render;

GpuShaderStorageBuffer RenderAsset<ShaderStorageBuffer>::prepare_asset(ShaderStorageBuffer&& asset, Param param) {
    auto& [device, queue] = param;
    GpuShaderStorageBuffer result;
    result.had_data = asset.data.has_value();

    // Bevy (storage.rs:131-157): with data -> create_buffer_with_data (size =
    // data length, usage | COPY_DST implicitly); without -> create_buffer with
    // the descriptor usage unchanged.
    const std::uint64_t size = asset.data.has_value() ? asset.data->size() : asset.size;
    const auto usage = asset.data.has_value() ? asset.usage | wgpu::BufferUsage::eCopyDst : asset.usage;

    wgpu::BufferDescriptor desc;
    desc.setLabel(asset.label.c_str()).setUsage(usage).setSize(size);
    result.buffer = device.get().createBuffer(desc);
    if (!result.buffer) {
        throw std::runtime_error("Failed to create GPU storage buffer for ShaderStorageBuffer");
    }
    if (asset.data && !asset.data->empty()) {
        queue.get().writeBuffer(result.buffer, 0, asset.data->data(), asset.data->size());
    }
    return result;
}

RenderAssetUsages RenderAsset<ShaderStorageBuffer>::usage(const ShaderStorageBuffer& asset) noexcept {
    return asset.asset_usage;
}

void StoragePlugin::attach(App& app) {
    spdlog::debug("[render.storage] Attaching StoragePlugin.");
    epix::assets::app_register_asset<ShaderStorageBuffer>(app);
    app.add_plugins(RenderAssetPlugin<ShaderStorageBuffer>{});
}
