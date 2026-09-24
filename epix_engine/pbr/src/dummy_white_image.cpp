#include <epix/pbr/dummy_white_image.hpp>

#include <variant>

namespace epix::pbr {

render::texture::GpuImage build_dummy_white_gpu_image(const wgpu::Device& render_device,
                                                      const wgpu::Sampler& default_sampler,
                                                      const wgpu::Queue& render_queue) {
    // Bevy `build_dummy_white_gpu_image` (`bevy_pbr/src/render/mesh.rs:1923-1962`):
    // the default image is the 1x1 opaque-white one, its descriptor creates the
    // texture, and its sampler is the resource's unless the image carries a
    // descriptor of its own.
    image::Image image{};
    const wgpu::TextureDescriptor texture_descriptor = image.texture_descriptor;
    const wgpu::Texture texture                      = render_device.createTexture(texture_descriptor);

    wgpu::Sampler sampler = default_sampler;
    if (!image.sampler().is_default()) {
        const auto& alternative = std::get<image::ImageSampler::DescriptorAlternative>(image.sampler().alternatives);
        const wgpu::SamplerDescriptor descriptor = alternative.value.as_wgpu();
        sampler                                  = render_device.createSampler(descriptor);
    }

    // Bevy writes the pixels when the format has a known texel size. It passes
    // `rows_per_image: None`; a single-row image makes an explicit one
    // equivalent, and this wrapper's layout has no undefined value.
    const auto format_size = image::texture_format_pixel_size(texture_descriptor.format);
    if (format_size.has_value() && *format_size > 0) {
        const auto view = image.raw_view();
        wgpu::TexelCopyBufferLayout layout;
        layout.setBytesPerRow(image.width() * static_cast<std::uint32_t>(*format_size)).setRowsPerImage(1);
        render_queue.writeTexture(wgpu::TexelCopyTextureInfo()
                                      .setTexture(texture)
                                      .setOrigin({0, 0, 0})
                                      .setAspect(wgpu::TextureAspect::eAll),
                                  view.data(), view.size_bytes(), layout, texture_descriptor.size);
    }

    const wgpu::TextureViewDescriptor view_descriptor{};
    return render::texture::GpuImage{
        .texture             = texture,
        .texture_view        = texture.createView(view_descriptor),
        .texture_format      = texture_descriptor.format,
        .texture_view_format = image.texture_view_descriptor ? image.texture_view_descriptor->format
                                                            : std::optional<wgpu::TextureFormat>{},
        .sampler             = sampler,
        .size                = texture_descriptor.size,
        .mip_level_count     = texture_descriptor.mipLevelCount,
        .had_data            = true,
    };
}

std::optional<std::pair<const wgpu::TextureView*, const wgpu::Sampler*>> get_image_texture(
    const render::texture::GpuImage& dummy_white_gpu_image,
    const render::RenderAssets<render::texture::GpuImage>& gpu_images,
    const std::optional<assets::Handle<image::Image>>& handle_option) {
    // Bevy `get_image_texture` (`bevy_pbr/src/render/mesh.rs:1964-1978`): a
    // material's optional texture binds the image's own view and sampler when it
    // has been uploaded, and the 1x1 white one when the handle is absent. An
    // absent handle is not an error, a handle whose image is still missing is -
    // the lookup fails, the same as Bevy's `gpu_images.get(handle)?`.
    if (handle_option) {
        const auto* gpu_image = gpu_images.get(handle_option->id());
        if (gpu_image == nullptr) return std::nullopt;
        return std::make_pair(&gpu_image->texture_view, &gpu_image->sampler);
    }
    return std::make_pair(&dummy_white_gpu_image.texture_view, &dummy_white_gpu_image.sampler);
}

}  // namespace epix::pbr
