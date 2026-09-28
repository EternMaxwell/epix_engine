#include <epix/pbr/shadow_samplers.hpp>

namespace epix::pbr {

wgpu::SamplerDescriptor shadow_sampler_base_descriptor() {
    wgpu::SamplerDescriptor descriptor{};
    descriptor.setAddressModeU(wgpu::AddressMode::eClampToEdge)
        .setAddressModeV(wgpu::AddressMode::eClampToEdge)
        .setAddressModeW(wgpu::AddressMode::eClampToEdge)
        .setMagFilter(wgpu::FilterMode::eLinear)
        .setMinFilter(wgpu::FilterMode::eLinear)
        .setMipmapFilter(wgpu::MipmapFilterMode::eNearest);
    return descriptor;
}

wgpu::SamplerDescriptor shadow_sampler_comparison_descriptor(const char* label) {
    wgpu::SamplerDescriptor descriptor = shadow_sampler_base_descriptor();
    descriptor.setLabel(label).setCompare(wgpu::CompareFunction::eGreaterEqual);
    return descriptor;
}

namespace {

wgpu::Sampler comparison_sampler(const wgpu::Device& device, const char* label) {
    return device.createSampler(shadow_sampler_comparison_descriptor(label));
}

}  // namespace

ShadowSamplers make_shadow_samplers(const wgpu::Device& device) {
    return ShadowSamplers{
        .point_light_comparison_sampler = comparison_sampler(device, "point_light_comparison_sampler"),
        .directional_light_comparison_sampler =
            comparison_sampler(device, "directional_light_comparison_sampler")};
}

void init_shadow_samplers(epix::ecs::Res<wgpu::Device> device, epix::ecs::Commands commands) {
    commands.insert_resource(make_shadow_samplers(device.get()));
}

}  // namespace epix::pbr