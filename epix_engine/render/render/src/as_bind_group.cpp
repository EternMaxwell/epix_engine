#include <epix/render.hpp>

namespace epix::render::render_resource {

std::expected<std::vector<wgpu::BindGroupEntry>, AsBindGroupError> BindingResources::entries() const {
    std::vector<wgpu::BindGroupEntry> result;
    result.reserve(m_resources.size());
    for (const auto& [binding, resource] : m_resources) {
        if (const auto* buffer = std::get_if<wgpu::Buffer>(&resource)) {
            result.emplace_back(wgpu::BindGroupEntry().setBinding(binding).setBuffer(*buffer).setOffset(0).setSize(
                std::numeric_limits<std::uint64_t>::max()));
        } else if (const auto* texture =
                       std::get_if<std::pair<wgpu::TextureViewDimension, wgpu::TextureView>>(&resource)) {
            result.emplace_back(wgpu::BindGroupEntry().setBinding(binding).setTextureView(texture->second));
        } else if (const auto* sampler = std::get_if<std::pair<wgpu::SamplerBindingType, wgpu::Sampler>>(&resource)) {
            result.emplace_back(wgpu::BindGroupEntry().setBinding(binding).setSampler(sampler->second));
        } else {
            return std::unexpected(CreateBindGroupDirectly{});
        }
    }
    return result;
}

}  // namespace epix::render::render_resource
