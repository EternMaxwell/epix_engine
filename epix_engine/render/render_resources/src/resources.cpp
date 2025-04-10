#include "epix/render/resources.h"

namespace epix::render::resources {
EPIX_API BindGroup RenderDevice::createBindGroup(
    wgpu::BindGroupLayout layout, ArrayProxy<BindGroupEntry> bindings
) {
    std::vector<wgpu::BindGroupEntry> entries;
    std::vector<wgpu::BindGroupEntryExtras> extras;
    entries.reserve(bindings.size());
    extras.reserve(bindings.size());  // reserve for extras
    for (auto&& entry : bindings) {
        entries.emplace_back();
        entries.back().binding = entry.binding;
        auto&& res             = entry.resource.resource;
        if (std::holds_alternative<BufferBinding>(res)) {
            // buffer binding
            auto&& bufferBinding  = std::get<BufferBinding>(res);
            entries.back().buffer = bufferBinding.buffer;
            entries.back().offset = bufferBinding.offset;
            entries.back().size   = bufferBinding.size;
        } else if (std::holds_alternative<std::vector<Buffer>>(res)) {
            // buffer binding array
            auto&& bufferBindings = std::get<std::vector<Buffer>>(res);
            extras.emplace_back();
            extras.back().bufferCount =
                static_cast<uint32_t>(bufferBindings.size());
            extras.back().buffers      = (WGPUBuffer*)bufferBindings.data();
            entries.back().nextInChain = &extras.back().chain;
        } else if (std::holds_alternative<Sampler>(res)) {
            // sampler
            auto&& sampler         = std::get<Sampler>(res);
            entries.back().sampler = sampler;
        } else if (std::holds_alternative<std::vector<Sampler>>(res)) {
            // sampler array
            auto&& samplers = std::get<std::vector<Sampler>>(res);
            extras.emplace_back();
            extras.back().samplerCount = static_cast<uint32_t>(samplers.size());
            extras.back().samplers     = (WGPUSampler*)samplers.data();
        } else if (std::holds_alternative<TextureView>(res)) {
            // texture view
            auto&& textureView         = std::get<TextureView>(res);
            entries.back().textureView = textureView;
        } else if (std::holds_alternative<std::vector<TextureView>>(res)) {
            // texture view array
            auto&& textureViews = std::get<std::vector<TextureView>>(res);
            extras.emplace_back();
            extras.back().textureViewCount =
                static_cast<uint32_t>(textureViews.size());
            extras.back().textureViews = (WGPUTextureView*)textureViews.data();
            entries.back().nextInChain = &extras.back().chain;
        }
    }
    wgpu::BindGroupDescriptor bindGroupDescriptor;
    bindGroupDescriptor.layout     = layout;
    bindGroupDescriptor.entryCount = static_cast<uint32_t>(entries.size());
    bindGroupDescriptor.entries    = entries.data();
    auto bindGroup = wgpu::Device::createBindGroup(bindGroupDescriptor);
    return bindGroup;
}

// test for compiling
void test1() {
    RenderDevice device;
    BindGroup bindGroup = device.createBindGroup(
        wgpu::BindGroupLayout(),
        BindGroupEntries::sequence(
            Buffer(), std::vector<Buffer>(), Sampler(), std::vector<Sampler>(),
            TextureView(), std::vector<TextureView>()
        )
    );
    bindGroup = device.createBindGroup(
        wgpu::BindGroupLayout(),
        BindGroupEntries::with_indices(
            0, Buffer(), 1, std::vector<Buffer>(), 2, Sampler(), 3,
            std::vector<Sampler>(), 4, TextureView(), 5,
            std::vector<TextureView>()
        )
    );
}
}  // namespace epix::render::resources