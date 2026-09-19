#include <epix/render/render_resource.hpp>

#include <cstring>
#include <spdlog/spdlog.h>
#include <string>

wgpu::Buffer epix::render::render_resource::create_buffer_with_data(const wgpu::Device& device,
                                                                   std::span<const std::byte> contents,
                                                                   wgpu::BufferUsage usage,
                                                                   std::string_view label) {
    const auto size  = static_cast<std::uint64_t>(contents.size());
    const std::string label_str(label);
    auto buffer      = device.createBuffer(wgpu::BufferDescriptor()
                                               .setLabel(label_str.c_str())
                                               .setSize(size)
                                               .setUsage(usage)
                                               .setMappedAtCreation(true));
    // A failed creation still yields a handle in this wrapper, and then
    // `getMappedRange` returns null: copying into it would be a null write, and
    // the invalid buffer would later poison a command buffer. Report it instead.
    auto* mapped = static_cast<std::byte*>(buffer.getMappedRange(0, size));
    if (mapped == nullptr) {
        spdlog::error(
            "[render] create_buffer_with_data: no mapped range for a {} byte buffer with usage {:#x} "
            "(label '{}'); the buffer or device is invalid",
            size, static_cast<std::uint32_t>(usage), label);
        return buffer;
    }
    std::memcpy(mapped, contents.data(), static_cast<std::size_t>(size));
    buffer.unmap();
    return buffer;
}
