#include <spdlog/spdlog.h>

#include <cstring>
#include <epix/render.hpp>
#include <string_view>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::render;

namespace epix::render::readback {

wgpu::Buffer GpuReadbackBufferPool::get(const wgpu::Device& device, std::uint64_t size) {
    auto& buffers = this->buffers[size];
    // find an untaken buffer for this size
    for (auto& buf : buffers) {
        if (!buf.taken) {
            buf.taken         = true;
            buf.frames_unused = 0;
            return buf.buffer;
        }
    }
    wgpu::BufferDescriptor desc;
    desc.setLabel("Readback Buffer").setUsage(wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eMapRead).setSize(size);
    wgpu::Buffer buffer = device.createBuffer(desc);
    buffers.push_back(GpuReadbackBuffer{buffer, true, 0});
    return buffer;
}

void GpuReadbackBufferPool::return_buffer(const wgpu::Buffer& buffer) {
    const std::uint64_t size = buffer.getSize();
    auto it                  = this->buffers.find(size);
    if (it == this->buffers.end()) {
        spdlog::warn("[render.gpu_readback] Returned buffer of untracked size {}.", size);
        return;
    }
    bool found = false;
    for (auto& buf : it->second) {
        if (buf.buffer == buffer) {
            buf.taken = false;
            found     = true;
            break;
        }
    }
    if (!found) {
        spdlog::warn("[render.gpu_readback] Returned buffer that was not allocated.");
    }
}

void GpuReadbackBufferPool::update(std::size_t max_unused_frames) {
    for (auto& [size, buffers] : this->buffers) {
        (void)size;
        // tick all the buffers
        for (auto& buf : buffers) {
            if (!buf.taken) {
                buf.frames_unused += 1;
            }
        }
        // remove buffers that haven't been used for max_unused_frames
        std::erase_if(buffers, [&](const GpuReadbackBuffer& buf) { return buf.frames_unused >= max_unused_frames; });
    }
    // remove empty buffer sizes
    std::erase_if(this->buffers, [](const auto& entry) { return entry.second.empty(); });
}

std::uint32_t align_byte_size(std::uint32_t value) {
    const std::uint32_t rem = value % COPY_BYTES_PER_ROW_ALIGNMENT;
    return rem == 0 ? value : value + (COPY_BYTES_PER_ROW_ALIGNMENT - rem);
}

std::uint32_t get_aligned_size(const wgpu::Extent3D& extent, std::uint32_t pixel_size) {
    return extent.height * align_byte_size(extent.width * pixel_size) * extent.depthOrArrayLayers;
}

std::uint32_t texture_format_pixel_size(wgpu::TextureFormat format) {
    switch (format) {
        case wgpu::TextureFormat::eR8Unorm:
        case wgpu::TextureFormat::eR8Snorm:
        case wgpu::TextureFormat::eR8Uint:
        case wgpu::TextureFormat::eR8Sint:
            return 1;
        case wgpu::TextureFormat::eR16Uint:
        case wgpu::TextureFormat::eR16Sint:
        case wgpu::TextureFormat::eR16Float:
        case wgpu::TextureFormat::eRG8Unorm:
        case wgpu::TextureFormat::eRG8Snorm:
        case wgpu::TextureFormat::eRG8Uint:
        case wgpu::TextureFormat::eRG8Sint:
            return 2;
        case wgpu::TextureFormat::eR32Uint:
        case wgpu::TextureFormat::eR32Sint:
        case wgpu::TextureFormat::eR32Float:
        case wgpu::TextureFormat::eRG16Uint:
        case wgpu::TextureFormat::eRG16Sint:
        case wgpu::TextureFormat::eRG16Float:
        case wgpu::TextureFormat::eRGBA8Unorm:
        case wgpu::TextureFormat::eRGBA8UnormSrgb:
        case wgpu::TextureFormat::eRGBA8Snorm:
        case wgpu::TextureFormat::eRGBA8Uint:
        case wgpu::TextureFormat::eRGBA8Sint:
        case wgpu::TextureFormat::eBGRA8Unorm:
        case wgpu::TextureFormat::eBGRA8UnormSrgb:
        case wgpu::TextureFormat::eRGB10A2Uint:
        case wgpu::TextureFormat::eRGB10A2Unorm:
        case wgpu::TextureFormat::eRG11B10Ufloat:
            return 4;
        case wgpu::TextureFormat::eRG32Uint:
        case wgpu::TextureFormat::eRG32Sint:
        case wgpu::TextureFormat::eRG32Float:
        case wgpu::TextureFormat::eRGBA16Uint:
        case wgpu::TextureFormat::eRGBA16Sint:
        case wgpu::TextureFormat::eRGBA16Float:
            return 8;
        case wgpu::TextureFormat::eRGBA32Uint:
        case wgpu::TextureFormat::eRGBA32Sint:
        case wgpu::TextureFormat::eRGBA32Float:
            return 16;
        default:
            return 0;  // compressed or unknown formats
    }
}

wgpu::TexelCopyBufferLayout layout_data(const wgpu::Extent3D& extent, wgpu::TextureFormat format) {
    // Bevy (gpu_readback.rs:387-414): bytes_per_row is only required when the
    // copy is not tightly packed (height > 1 or multiple layers); rows_per_image
    // is only set for array/3D copies (block dimension 1 for non-compressed).
    wgpu::TexelCopyBufferLayout layout;
    if (extent.height > 1 || extent.depthOrArrayLayers > 1) {
        layout.setBytesPerRow(align_byte_size(extent.width * texture_format_pixel_size(format)));
    }
    if (extent.depthOrArrayLayers > 1) {
        layout.setRowsPerImage(extent.height);
    }
    return layout;
}

}  // namespace epix::render::readback

void epix::render::sync_readbacks(app::Extract<ecs::ResMut<ecs::Events<ReadbackComplete>>> events,
                                  ecs::ResMut<readback::GpuReadbackBufferPool> buffer_pool,
                                  ecs::ResMut<readback::GpuReadbacks> readbacks,
                                  ecs::Res<readback::GpuReadbackMaxUnusedFrames> max_unused_frames) {
    // deliver completed readbacks to the main world
    auto& mapped = readbacks.get_mut().mapped;
    std::erase_if(mapped, [&](readback::GpuReadback& readback) {
        if (auto result = readback.channel->try_recv()) {
            auto [entity, buffer, data] = std::move(*result);
            if (data) {
                events.get_mut().push(ReadbackComplete{entity, std::move(*data)});
            } else {
                // map failed: return the staging buffer so it is not leaked
                spdlog::warn("[render.gpu_readback] Failed to map readback buffer; returning it to the pool.");
            }
            buffer_pool.get_mut().return_buffer(buffer);
            return true;
        }
        return false;
    });
    buffer_pool.get_mut().update(max_unused_frames.get().value);
}

void epix::render::prepare_buffers(
    ecs::Res<wgpu::Device> device,
    ecs::ResMut<readback::GpuReadbacks> readbacks,
    ecs::ResMut<readback::GpuReadbackBufferPool> buffer_pool,
    ecs::Res<RenderAssets<epix::image::Image>> gpu_images,
    ecs::Res<RenderAssets<ShaderStorageBuffer>> ssbos,
    ecs::Query<ecs::Item<ecs::Entity, const sync_world::MainEntity&, const Readback&>> handles) {
    for (auto&& [render_entity, main_entity, readback] : handles.iter()) {
        (void)render_entity;
        const auto entity = main_entity.entity;  // Bevy main_entity.id()
        if (const auto* texture = std::get_if<Readback::Texture>(&readback.value)) {
            if (auto gpu_image = gpu_images.get().try_get(texture->image.id())) {
                const std::uint32_t pixel_size = readback::texture_format_pixel_size(gpu_image->texture_format);
                if (pixel_size == 0) {
                    spdlog::warn("[render.gpu_readback] Unsupported texture format for readback: {}.",
                                 static_cast<int>(gpu_image->texture_format));
                    continue;
                }
                auto layout = readback::layout_data(gpu_image->size, gpu_image->texture_format);
                auto buffer =
                    buffer_pool.get_mut().get(device.get(), readback::get_aligned_size(gpu_image->size, pixel_size));
                readbacks.get_mut().requested.push_back(readback::GpuReadback{
                    entity,
                    readback::ReadbackSource{readback::ReadbackSource::Texture{
                        gpu_image->texture,
                        std::move(layout),
                        gpu_image->size,
                    }},
                    buffer,
                    std::make_shared<readback::ReadbackChannel>(),
                });
            }
        } else if (const auto* buffer_src = std::get_if<Readback::Buffer>(&readback.value)) {
            if (auto ssbo = ssbos.get().try_get(buffer_src->buffer.id())) {
                const std::uint64_t full_size = ssbo->buffer.getSize();
                std::uint64_t size            = full_size;
                if (buffer_src->start_offset_and_size) {
                    const auto [start, s] = *buffer_src->start_offset_and_size;
                    if (start + s > full_size) {
                        throw std::runtime_error(std::format(
                            "Tried to read past the end of the buffer (start: {}, size: {}, buffer size: {}).", start,
                            s, full_size));
                    }
                    size = s;
                }
                auto buffer = buffer_pool.get_mut().get(device.get(), size);
                readbacks.get_mut().requested.push_back(readback::GpuReadback{
                    entity,
                    readback::ReadbackSource{readback::ReadbackSource::Buffer{
                        ssbo->buffer,
                        buffer_src->start_offset_and_size,
                    }},
                    buffer,
                    std::make_shared<readback::ReadbackChannel>(),
                });
            }
        }
    }
}

void epix::render::map_buffers(ecs::ResMut<readback::GpuReadbacks> readbacks) {
    auto& requested = readbacks.get_mut().requested;
    for (auto& readback : requested) {
        const std::uint64_t size = readback.buffer.getSize();
        const auto entity        = readback.entity;
        const auto buffer        = readback.buffer;
        const auto channel       = readback.channel;
        auto callback =
            wgpu::BufferMapCallback([entity, buffer, channel](wgpu::MapAsyncStatus status, wgpu::StringView message) {
                if (status != wgpu::MapAsyncStatus::eSuccess) {
                    spdlog::warn("[render.gpu_readback] Failed to map readback buffer: {}.", std::string_view(message));
                    // nullopt data tells sync_readbacks to return the buffer to the pool
                    if (!channel->try_send(entity, buffer, std::nullopt)) {
                        spdlog::warn("[render.gpu_readback] Readback result slot full; dropping result.");
                    }
                    return;
                }
                const void* mapped = buffer.getConstMappedRange(0, buffer.getSize());
                std::vector<std::uint8_t> data(static_cast<const std::uint8_t*>(mapped),
                                               static_cast<const std::uint8_t*>(mapped) + buffer.getSize());
                buffer.unmap();
                if (!channel->try_send(entity, buffer, std::move(data))) {
                    spdlog::warn("[render.gpu_readback] Readback result slot full; dropping result.");
                }
            });
        readback.buffer.mapAsync(wgpu::MapMode::eRead, 0, size,
                                 wgpu::BufferMapCallbackInfo()
                                     .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                                     .setCallback(std::move(callback)));
        readbacks.get_mut().mapped.push_back(std::move(readback));
    }
    requested.clear();
}

void epix::render::submit_readback_commands(World& world, const wgpu::CommandEncoder& encoder) {
    if (auto readbacks = world.get_resource<readback::GpuReadbacks>()) {
        for (const auto& readback : readbacks->get().requested) {
            if (const auto* texture = std::get_if<readback::ReadbackSource::Texture>(&readback.src.value)) {
                wgpu::TexelCopyTextureInfo src_info;
                src_info.setTexture(texture->texture)
                    .setMipLevel(0)
                    .setOrigin(wgpu::Origin3D{0, 0, 0})
                    .setAspect(wgpu::TextureAspect::eAll);
                wgpu::TexelCopyBufferInfo dst_info;
                dst_info.setBuffer(readback.buffer).setLayout(texture->layout);
                encoder.copyTextureToBuffer(src_info, dst_info, texture->size);
            } else if (const auto* buffer_src = std::get_if<readback::ReadbackSource::Buffer>(&readback.src.value)) {
                const auto [src_start, size] = buffer_src->start_offset_and_size.value_or(
                    std::pair<std::uint64_t, std::uint64_t>{0, buffer_src->buffer.getSize()});
                encoder.copyBufferToBuffer(buffer_src->buffer, src_start, readback.buffer, 0, size);
            }
        }
    }
}

void epix::render::GpuReadbackPlugin::attach(App& app) {
    spdlog::debug("[render.gpu_readback] Attaching GpuReadbackPlugin (max_unused_frames={}).", max_unused_frames);
    app.add_event<ReadbackComplete>();
    app.add_plugins(ExtractComponentPlugin<Readback>{});
    if (auto render_app = app.get_sub_app_mut(Render)) {
        render_app->get().world_mut().init_resource<readback::GpuReadbackBufferPool>();
        render_app->get().world_mut().init_resource<readback::GpuReadbacks>();
        render_app->get().world_mut().insert_resource(readback::GpuReadbackMaxUnusedFrames{max_unused_frames});
        render_app->get().add_systems(ExtractSchedule, into(sync_readbacks));
        render_app->get().add_systems(Render, into(prepare_buffers).in_set(RenderSystems::PrepareResources));
        render_app->get().add_systems(Render, into(map_buffers).after(render_system).in_set(RenderSystems::Render));
    }
}
