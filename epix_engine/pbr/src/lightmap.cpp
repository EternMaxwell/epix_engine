#include <epix/pbr/lightmap.hpp>

#include <bit>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace epix::pbr {

glm::uvec2 pack_lightmap_uv_rect(const std::optional<::epix::image::Rect>& maybe_rect) {
    if (!maybe_rect) return glm::uvec2{0u, 0u};
    const glm::uvec4 rect = glm::uvec4(
        glm::round(glm::vec4(maybe_rect->min.x, maybe_rect->min.y, maybe_rect->max.x, maybe_rect->max.y) * 65535.0f));
    return glm::uvec2(rect.x | (rect.y << 16), rect.z | (rect.w << 16));
}

LightmapSlab LightmapSlab::make(const render::texture::FallbackImage& fallback_images, bool bindless_supported) {
    const std::size_t count = bindless_supported ? LIGHTMAPS_PER_SLAB : 1;
    LightmapSlab slab;
    slab.lightmaps.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        slab.lightmaps.push_back(AllocatedLightmap{.gpu_image = fallback_images.d2, .asset_id = std::nullopt});
    }
    slab.free_slots_bitmask = (1u << count) - 1u;
    return slab;
}

LightmapSlotIndex LightmapSlab::allocate(assets::AssetId<::epix::image::Image> image_id) {
    assert(!is_full() && "Attempting to allocate on a full lightmap slab");
    const std::uint32_t slot = std::countr_zero(free_slots_bitmask);
    free_slots_bitmask &= ~(1u << slot);
    lightmaps[slot].asset_id = image_id;
    return LightmapSlotIndex{.index = static_cast<std::uint16_t>(slot)};
}

void LightmapSlab::insert(LightmapSlotIndex index, render::texture::GpuImage gpu_image) {
    lightmaps[index.index] = AllocatedLightmap{.gpu_image = std::move(gpu_image), .asset_id = std::nullopt};
}

void LightmapSlab::remove(const render::texture::FallbackImage& fallback_images, LightmapSlotIndex index) {
    lightmaps[index.index] = AllocatedLightmap{.gpu_image = fallback_images.d2, .asset_id = std::nullopt};
    free_slots_bitmask |= 1u << index.index;
}

std::pair<std::vector<const wgpu::TextureView*>, std::vector<const wgpu::Sampler*>>
LightmapSlab::build_binding_arrays() const {
    std::vector<const wgpu::TextureView*> texture_views;
    std::vector<const wgpu::Sampler*> samplers;
    texture_views.reserve(lightmaps.size());
    samplers.reserve(lightmaps.size());
    for (const AllocatedLightmap& allocated : lightmaps) {
        texture_views.push_back(&allocated.gpu_image.texture_view);
        samplers.push_back(&allocated.gpu_image.sampler);
    }
    return {std::move(texture_views), std::move(samplers)};
}

std::pair<const wgpu::TextureView*, const wgpu::Sampler*> LightmapSlab::bindings_for_first_lightmap() const {
    return {&lightmaps[0].gpu_image.texture_view, &lightmaps[0].gpu_image.sampler};
}

LightmapSlabIndex RenderLightmaps::create_slab(const render::texture::FallbackImage& fallback_images) {
    const auto slab_index = LightmapSlabIndex{.index = static_cast<std::uint32_t>(slabs.size())};
    free_slabs.insert(slab_index.index);
    slabs.push_back(LightmapSlab::make(fallback_images, bindless_supported));
    return slab_index;
}

std::pair<LightmapSlabIndex, LightmapSlotIndex> RenderLightmaps::allocate(
    const render::texture::FallbackImage& fallback_images, assets::AssetId<::epix::image::Image> image_id) {
    const LightmapSlabIndex slab_index = free_slabs.empty()
                                             ? create_slab(fallback_images)
                                             : LightmapSlabIndex{.index = static_cast<std::uint32_t>(*free_slabs.begin())};
    LightmapSlab& slab                 = slabs[slab_index.index];
    const LightmapSlotIndex slot_index = slab.allocate(image_id);
    if (slab.is_full()) free_slabs.erase(slab_index.index);
    return {slab_index, slot_index};
}

void RenderLightmaps::remove(const render::texture::FallbackImage& fallback_images, LightmapSlabIndex slab_index,
                             LightmapSlotIndex slot_index) {
    LightmapSlab& slab = slabs[slab_index.index];
    slab.remove(fallback_images, slot_index);
    if (!slab.is_full()) free_slabs.insert(slab_index.index);
}

}  // namespace epix::pbr
