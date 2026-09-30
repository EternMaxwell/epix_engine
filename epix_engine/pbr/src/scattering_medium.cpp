#include <epix/pbr/scattering_medium.hpp>

#include <cstring>

namespace epix::pbr {

namespace {

/** @brief Create one LUT texture from vec4 data (Bevy passes the data to
 *  `create_texture_with_data`; this wrapper uploads it with `writeTexture`). */
wgpu::Texture upload_lut(const wgpu::Device& device, const wgpu::Queue& queue, const std::string& label,
                         std::uint32_t width, std::uint32_t height, const std::vector<glm::vec4>& data) {
    wgpu::TextureDescriptor descriptor{};
    descriptor.setLabel(label.c_str()).setSize({width, height, 1}).setFormat(wgpu::TextureFormat::eRGBA32Float);
    // `eCopyDst` is required by the `writeTexture` below; without it the upload is
    // rejected with a WebGPU validation error. Bevy's `create_texture_with_data` sets
    // the copy-destination usage itself, which is why the CPU-side data path here needs
    // it explicitly.
    descriptor.setUsage(wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopyDst);
    descriptor.setDimension(wgpu::TextureDimension::e2D);
    descriptor.setMipLevelCount(1).setSampleCount(1);

    const wgpu::Texture texture = device.createTexture(descriptor);

    wgpu::TexelCopyBufferLayout layout;
    layout.setBytesPerRow(width * 16u).setRowsPerImage(height);
    queue.writeTexture(wgpu::TexelCopyTextureInfo()
                           .setTexture(texture)
                           .setOrigin({0, 0, 0})
                           .setAspect(wgpu::TextureAspect::eAll),
                       data.data(), data.size() * sizeof(glm::vec4), layout, descriptor.size);
    return texture;
}

}  // namespace

GpuScatteringMedium make_gpu_scattering_medium(const wgpu::Device& device, const wgpu::Queue& queue,
                                               const ScatteringMedium& medium) {
    const std::string prefix = medium.label.empty() ? std::string("scattering_medium") : medium.label;

    GpuScatteringMedium gpu;
    gpu.terms              = medium.terms;
    gpu.falloff_resolution = medium.falloff_resolution;
    gpu.phase_resolution   = medium.phase_resolution;

    gpu.density_lut = upload_lut(device, queue, prefix + "_density_lut", medium.falloff_resolution, 2,
                                 medium.density_lut());
    gpu.density_lut_view = gpu.density_lut.createView({});

    gpu.scattering_lut = upload_lut(device, queue, prefix + "_scattering_lut", medium.falloff_resolution,
                                    medium.phase_resolution, medium.scattering_lut());
    gpu.scattering_lut_view = gpu.scattering_lut.createView({});
    return gpu;
}

ScatteringMediumSampler make_scattering_medium_sampler(const wgpu::Device& device) {
    wgpu::SamplerDescriptor descriptor{};
    descriptor.setLabel("scattering_medium_sampler")
        .setMagFilter(wgpu::FilterMode::eLinear)
        .setMinFilter(wgpu::FilterMode::eLinear);
    return ScatteringMediumSampler{.sampler = device.createSampler(descriptor)};
}

}  // namespace epix::pbr