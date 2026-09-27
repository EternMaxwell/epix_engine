#include <epix/pbr/lightmap.hpp>

#include <bit>
#include <cassert>
#include <cstdint>
#include <epix/pbr/mesh_extraction.hpp>
#include <epix/render/assets.hpp>
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

void extract_lightmaps(ecs::ResMut<RenderLightmaps> render_lightmaps,
                       app::Extract<ecs::Query<ChangedLightmapItem, ChangedLightmapFilter>> changed_lightmaps_query,
                       app::Extract<ecs::RemovedComponents<Lightmap>> removed_lightmaps_query,
                       ecs::Res<render::RenderAssets<render::texture::GpuImage>> images,
                       ecs::Res<render::texture::FallbackImage> fallback_images) {
    RenderLightmaps& lightmaps = *render_lightmaps;

    // Every changed, visible lightmap that does not have a record yet is allocated a
    // slab and slot; an invisible one is simply skipped, and an already-recorded one
    // is left alone - which is what makes the system idempotent across frames.
    for (auto&& [entity, view_visibility, lightmap] : changed_lightmaps_query.iter()) {
        const render::sync_world::MainEntity main_entity{entity};
        if (lightmaps.render_lightmaps.contains(main_entity)) continue;
        if (!view_visibility.get()) continue;

        const auto [slab_index, slot_index] = lightmaps.allocate(*fallback_images, lightmap.image.id());
        lightmaps.render_lightmaps.emplace(
            main_entity, RenderLightmap{.uv_rect          = lightmap.uv_rect,
                                        .slab_index       = slab_index,
                                        .slot_index       = slot_index,
                                        .bicubic_sampling = lightmap.bicubic_sampling});
        lightmaps.pending_lightmaps.emplace(slab_index, slot_index);
    }

    // Removals hand their slot back. A component removed and re-added in the same
    // frame is still in the changed query, so it keeps the slot it just took.
    for (const ecs::Entity entity : removed_lightmaps_query.read()) {
        if (changed_lightmaps_query.get(entity).has_value()) continue;
        const render::sync_world::MainEntity main_entity{entity};
        const auto found = lightmaps.render_lightmaps.find(main_entity);
        if (found == lightmaps.render_lightmaps.end()) continue;
        const RenderLightmap removed = found->second;
        lightmaps.render_lightmaps.erase(found);
        lightmaps.remove(*fallback_images, removed.slab_index, removed.slot_index);
        lightmaps.pending_lightmaps.erase({removed.slab_index, removed.slot_index});
    }

    // Slots still waiting for their image are retried as uploads land. A slot whose
    // asset id is gone cannot be resolved at all and is reported rather than left
    // pending for ever; one whose image is merely not uploaded yet stays pending.
    std::erase_if(lightmaps.pending_lightmaps, [&](const auto& pending) {
        const auto& [slab_index, slot_index] = pending;
        LightmapSlab& slab = lightmaps.slabs[slab_index.index];
        const std::optional<assets::AssetId<::epix::image::Image>>& asset_id =
            slab.lightmaps[slot_index.index].asset_id;
        if (!asset_id.has_value()) {
            spdlog::error(
                "[pbr] Allocated lightmap should have been removed from `pending_lightmaps` by now.");
            return true;
        }
        const render::texture::GpuImage* gpu_image = images->get(*asset_id);
        if (gpu_image == nullptr) return false;
        slab.insert(slot_index, *gpu_image);
        return true;
    });
}

void init_render_lightmaps(ecs::Commands commands, ecs::Res<wgpu::Device> render_device,
                           ecs::Res<wgpu::Adapter> render_adapter) {
    (void)render_adapter;
    // Bevy asks `binding_arrays_are_usable(render_device, render_adapter)`, which is a
    // feature check plus a downlevel flag; the port's bindings expose the feature half
    // (`hasFeature`) but not the adapter's downlevel flags, and the binding-array limit
    // is not reachable either (see `mesh_layout_entry`'s notes). The feature check is
    // therefore what this can ask, and the slab falls back to a single binding where it
    // says no - which is the same shape Bevy takes when the answer is false.
    const bool bindless_supported = render_device->hasFeature(wgpu::FeatureName::eTextureBindingArray);

    RenderLightmaps lightmaps;
    lightmaps.bindless_supported = bindless_supported;
    commands.insert_resource(std::move(lightmaps));
}

void LightmapPlugin::attach(app::App& app) {
    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) return;

    // The set must be in the schedule for the ordering below to apply at all: the
    // port warns and *ignores* a hierarchy whose set is absent, so without this the
    // extraction would not actually be ordered after mesh extraction. Bevy registers
    // `MeshExtractionSystems` inside `MeshRenderPlugin`, which is unported, so its
    // first user does it here - the same silent-gap fix as `MaterialPlugin`'s.
    render_app->get().configure_sets(app::ScheduleInfo(render::ExtractSchedule),
                                     ecs::sets(MeshExtractionSystems::Extract));
    render_app->get().add_systems(app::ScheduleInfo(render::RenderStartup),
                                  ecs::into(init_render_lightmaps).set_name("init render lightmaps"));
    render_app->get().add_systems(
        app::ScheduleInfo(render::ExtractSchedule),
        ecs::into(extract_lightmaps).after(MeshExtractionSystems::Extract).set_name("extract lightmaps"));
}

}  // namespace epix::pbr
