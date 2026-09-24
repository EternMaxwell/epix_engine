#include <epix/pbr/dummy_white_image.hpp>

namespace epix::pbr {

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
