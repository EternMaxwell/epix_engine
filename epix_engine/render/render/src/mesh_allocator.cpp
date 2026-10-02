#include <epix/app.hpp>
#include <epix/mesh/mesh.hpp>
#include <epix/mesh/vertex_buffer_layout.hpp>
#include <epix/render.hpp>
#include <stdexcept>

using namespace epix;
using namespace epix::ecs;

void epix::render::mesh::MeshAllocator::allocate_meshes(
    const MeshAllocatorSettings& settings,
    const render::ExtractedAssets<::epix::render::mesh::RenderMesh>& extracted_meshes,
    ::epix::mesh::MeshVertexBufferLayouts& mesh_vertex_buffer_layouts,
    const wgpu::Device& device,
    const wgpu::Queue& queue) {
    detail::SlabsToReallocate slabs_to_reallocate;

    for (const auto& [id, source_mesh] : extracted_meshes.extracted) {
        const auto vertex_bytes = static_cast<std::uint64_t>(source_mesh.get_vertex_buffer_size());
        if (vertex_bytes == 0) continue;
        // Bevy's `ElementLayout::vertex` unwraps this lookup, so a mesh without a vertex buffer layout is a hard
        // failure rather than a mesh silently left unallocated.
        if (!source_mesh.get_mesh_vertex_buffer_layout(mesh_vertex_buffer_layouts).value) {
            throw std::runtime_error("A mesh in the extracted set has no vertex buffer layout");
        }
        const auto vertex_element_layout = detail::ElementLayout::vertex(mesh_vertex_buffer_layouts, source_mesh);
        if (general_vertex_slabs_supported) {
            allocate(id, vertex_bytes, vertex_element_layout, slabs_to_reallocate, settings);
        } else {
            allocate_large(id, vertex_element_layout);
        }

        if (const auto index_bytes = source_mesh.get_index_buffer_bytes()) {
            if (const auto index_element_layout = detail::ElementLayout::index(source_mesh)) {
                allocate(id, static_cast<std::uint64_t>(index_bytes->size()), *index_element_layout, slabs_to_reallocate,
                         settings);
            }
        }
    }

    for (const auto& [slab_id, reallocate] : slabs_to_reallocate.slabs) {
        reallocate_slab(device, queue, slab_id, reallocate);
    }

    for (const auto& [id, source_mesh] : extracted_meshes.extracted) {
        if (const auto vertex_slab = mesh_id_to_vertex_slab.find(id); vertex_slab != mesh_id_to_vertex_slab.end()) {
            const auto packed = source_mesh.create_packed_vertex_buffer_data();
            if (!packed.empty()) {
                copy_element_data(device, queue, vertex_slab->second, id, packed.data(), packed.size(),
                                  wgpu::BufferUsage::eVertex);
            }
        }
        if (const auto index_slab = mesh_id_to_index_slab.find(id); index_slab != mesh_id_to_index_slab.end()) {
            if (const auto index_bytes = source_mesh.get_index_buffer_bytes()) {
                copy_element_data(device, queue, index_slab->second, id, index_bytes->data(), index_bytes->size(),
                                  wgpu::BufferUsage::eIndex);
            }
        }
    }
}

epix::render::mesh::MeshAllocator epix::render::mesh::MeshAllocator::from_world(epix::ecs::World& world) {
    (void)world;
    // TEMPORARY wgpu-native limitation: the C API does not expose Bevy's
    // DownlevelFlags query. Epix currently forces Vulkan for Slang SPIR-V
    // passthrough, and Vulkan guarantees BASE_VERTEX. Remove this line with
    // that renderer workaround once the native API exposes the capability.
    return MeshAllocator(true);
}

void epix::render::mesh::allocate_and_free_meshes(
    ResMut<MeshAllocator> mesh_allocator,
    Res<MeshAllocatorSettings> mesh_allocator_settings,
    Res<render::ExtractedAssets<::epix::render::mesh::RenderMesh>> extracted_meshes,
    ResMut<::epix::mesh::MeshVertexBufferLayouts> mesh_vertex_buffer_layouts,
    Res<wgpu::Device> device,
    Res<wgpu::Queue> queue) {
    mesh_allocator->free_meshes(*extracted_meshes);
    mesh_allocator->allocate_meshes(*mesh_allocator_settings, *extracted_meshes, mesh_vertex_buffer_layouts.get_mut(),
                                    *device, *queue);
}

void epix::render::mesh::MeshAllocatorPlugin::attach(app::App& app) {
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<MeshAllocatorSettings>();
        render_app->get().add_systems(render::Render,
                                      into(allocate_and_free_meshes)
                                          .before(render::prepare_assets<::epix::render::mesh::RenderMesh>)
                                          .in_set(render::RenderSystems::PrepareAssets)
                                          .set_name("allocate and free meshes"));
    }
}

void epix::render::mesh::MeshAllocatorPlugin::ready(app::App& app) {
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<MeshAllocator>();
    }
}

void epix::render::mesh::MeshRenderAssetPlugin::attach(app::App& app) {
    app.add_plugins(render::RenderAssetPlugin<::epix::render::mesh::RenderMesh, epix::render::texture::GpuImage>{});
    app.add_plugins(MeshAllocatorPlugin{});

    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<::epix::mesh::MeshVertexBufferLayouts>();
    }
}

namespace epix::render::mesh {
namespace detail {

std::pair<std::uint32_t, SlabGrowthResult> compute_grow_capacity(std::uint32_t current_capacity,
                                                                std::uint32_t new_size_in_slots,
                                                                const MeshAllocatorSettings& settings,
                                                                std::uint64_t slot_size) {
    const std::uint32_t max_slot_capacity = static_cast<std::uint32_t>(settings.max_slab_size / slot_size);
    if (current_capacity >= new_size_in_slots) {
        return {current_capacity, SlabGrowthNoGrowthNeeded{}};
    }
    const std::uint32_t initial_capacity = current_capacity;
    std::uint32_t capacity               = current_capacity;
    while (capacity < new_size_in_slots) {
        const std::uint32_t grown1 =
            static_cast<std::uint32_t>(std::ceil(static_cast<double>(capacity) * settings.growth_factor));
        const std::uint32_t grown = grown1 < max_slot_capacity ? grown1 : max_slot_capacity;
        if (grown == capacity) {
            return {capacity, SlabGrowthCantGrow{}};
        }
        capacity = grown;
    }
    return {capacity, SlabGrowthNeeded{SlabToReallocate{.old_slot_capacity = initial_capacity}}};
}

}  // namespace detail

std::optional<MeshBufferSlice> MeshAllocator::mesh_slice(const epix::assets::AssetId<::epix::mesh::Mesh>& id,
                                                         bool is_vertex) const {
    const auto& map = is_vertex ? mesh_id_to_vertex_slab : mesh_id_to_index_slab;
    auto it         = map.find(id);
    if (it == map.end()) return std::nullopt;
    const auto sit = slabs.find(it->second);
    if (sit == slabs.end()) return std::nullopt;
    if (const auto* general = sit->second.general()) {
        const auto alloc_it = general->resident_allocations.find(id);
        if (alloc_it == general->resident_allocations.end() || !general->buffer) return std::nullopt;
        auto range = detail::general_slab_element_range(alloc_it->second.offset(), alloc_it->second.slot_count,
                                                        general->element_layout);
        return MeshBufferSlice{std::addressof(general->buffer), range};
    }
    if (const auto* large = sit->second.large()) {
        if (!large->buffer) return std::nullopt;
        const std::uint32_t element_count =
            static_cast<std::uint32_t>(large->buffer.getSize() / large->element_layout.size);
        return MeshBufferSlice{std::addressof(large->buffer), {0, element_count}};
    }
    return std::nullopt;
}

void MeshAllocator::reallocate_slab(const wgpu::Device& device,
                                    const wgpu::Queue& queue,
                                    SlabId slab_id,
                                    detail::SlabToReallocate slab_to_reallocate) {
    auto it = slabs.find(slab_id);
    if (it == slabs.end()) return;
    auto* general = it->second.general();
    if (!general) return;
    const std::uint64_t needed_size =
        static_cast<std::uint64_t>(general->current_slot_capacity) * general->element_layout.slot_size();
    auto usage = wgpu::BufferUsage::eCopySrc | wgpu::BufferUsage::eCopyDst | extra_buffer_usages;
    usage =
        usage | (general->element_layout.element_class == detail::ElementClass::Vertex ? wgpu::BufferUsage::eVertex
                                                                                       : wgpu::BufferUsage::eIndex);
    auto new_buffer = device.createBuffer(
        wgpu::BufferDescriptor().setSize(needed_size).setLabel("general mesh slab").setUsage(usage));
    const auto old_buffer = std::exchange(general->buffer, new_buffer);
    if (!old_buffer) return;

    const std::uint64_t bytes_to_copy =
        static_cast<std::uint64_t>(slab_to_reallocate.old_slot_capacity) * general->element_layout.slot_size();
    if (bytes_to_copy == 0) return;
    auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("slab resize encoder"));
    encoder.copyBufferToBuffer(old_buffer, 0, new_buffer, 0, bytes_to_copy);
    queue.submit(std::array{encoder.finish()});
}

void MeshAllocator::copy_element_data(const wgpu::Device& device,
                                      const wgpu::Queue& queue,
                                      SlabId slab_id,
                                      const epix::assets::AssetId<::epix::mesh::Mesh>& id,
                                      const void* data,
                                      std::size_t bytes,
                                      wgpu::BufferUsage usage) {
    auto it = slabs.find(slab_id);
    if (it == slabs.end()) return;
    if (auto* large = it->second.large()) {
        // Bevy copy_element_data for Slab::LargeObject: create the buffer
        // and its data in one go.
        if (!large->buffer) {
            large->buffer =
                device.createBuffer(wgpu::BufferDescriptor()
                                        .setSize(bytes)
                                        .setLabel("large mesh slab")
                                        .setUsage(usage | wgpu::BufferUsage::eCopyDst | extra_buffer_usages)
                                        .setMappedAtCreation(true));
            if (auto* range = static_cast<std::uint8_t*>(large->buffer.getMappedRange(0, bytes))) {
                std::memcpy(range, data, bytes);
            }
            large->buffer.unmap();
        }
        return;
    }
    auto* general = it->second.general();
    if (!general || !general->buffer) return;
    // Bevy GeneralSlab::copy_element_data: the write size is rounded up to
    // the slot size (a multiple of COPY_BUFFER_ALIGNMENT) so wgpu accepts
    // the copy; only the payload bytes are meaningful.
    const auto pending = general->pending_allocations.find(id);
    if (pending == general->pending_allocations.end()) return;
    const auto allocation         = pending->second;
    const std::uint64_t slot_size = general->element_layout.slot_size();
    const std::uint64_t aligned   = (bytes + slot_size - 1) / slot_size * slot_size;
    if (aligned == bytes) {
        queue.writeBuffer(general->buffer, static_cast<std::uint64_t>(allocation.offset()) * slot_size, data, bytes);
    } else {
        std::vector<std::uint8_t> padded(static_cast<std::size_t>(aligned), 0);
        std::memcpy(padded.data(), data, bytes);
        queue.writeBuffer(general->buffer, static_cast<std::uint64_t>(allocation.offset()) * slot_size, padded.data(),
                          padded.size());
    }
    general->resident_allocations[id] = allocation;
    general->pending_allocations.erase(pending);
}

void MeshAllocator::allocate_general(const epix::assets::AssetId<::epix::mesh::Mesh>& id,
                                     std::uint32_t data_slot_count,
                                     const detail::ElementLayout& layout,
                                     detail::SlabsToReallocate& slabs_to_reallocate,
                                     const MeshAllocatorSettings& settings) {
    // Loop through the slabs that accept the layout, trying the first one
    // that can fit the payload (Bevy slab_layouts candidate search).
    auto& candidate_slabs = slab_layouts[layout];
    std::optional<detail::MeshAllocation> mesh_allocation;
    for (const auto& slab_id : candidate_slabs) {
        auto it = slabs.find(slab_id);
        if (it == slabs.end()) continue;
        auto* general = it->second.general();
        if (!general) continue;
        auto allocation = general->allocator.allocate(data_slot_count);
        if (!allocation) continue;
        const auto growth = general->grow_if_necessary(allocation->offset + data_slot_count, settings);
        if (std::holds_alternative<detail::SlabGrowthCantGrow>(growth)) {
            continue;
        }
        if (const auto* needed_growth = std::get_if<detail::SlabGrowthNeeded>(&growth)) {
            // Preserve the capacity at the beginning of the frame if this
            // slab grows more than once before reallocation.
            slabs_to_reallocate.slabs.try_emplace(slab_id, needed_growth->slab_to_reallocate);
        }
        mesh_allocation = detail::MeshAllocation{slab_id, {*allocation, data_slot_count}};
        break;
    }
    // No existing slab fit: create a new one big enough for the payload.
    if (!mesh_allocation) {
        const SlabId new_id = next_slab_id;
        next_slab_id        = SlabId{
            next_slab_id.get() == std::numeric_limits<std::uint32_t>::max() - 1 ? 0 : next_slab_id.get() + 1};
        auto new_slab   = detail::GeneralSlab::make(layout, data_slot_count, settings);
        auto allocation = new_slab.allocator.allocate(data_slot_count);
        if (!allocation) return;
        mesh_allocation = detail::MeshAllocation{new_id, {*allocation, data_slot_count}};
        slabs.emplace(new_id, detail::Slab::general(std::move(new_slab)));
        candidate_slabs.push_back(new_id);
        slabs_to_reallocate.slabs.try_emplace(new_id, detail::SlabToReallocate{});
    }
    const auto& [slab_id, slab_allocation] = *mesh_allocation;
    // Mark the allocation as pending; do not copy it in yet (Bevy
    // allocate_meshes batches uploads after all allocations).
    if (auto* general = slabs.at(slab_id).general()) {
        general->pending_allocations[id] = slab_allocation;
    }
    record_allocation(id, slab_id, layout.element_class);
}

void MeshAllocator::free_meshes(const ExtractedAssets<::epix::render::mesh::RenderMesh>& extracted_meshes) {
    std::unordered_set<SlabId> empty_slabs;
    const auto free_mesh = [&](const epix::assets::AssetId<::epix::mesh::Mesh>& id) {
        if (auto vertex = mesh_id_to_vertex_slab.find(id); vertex != mesh_id_to_vertex_slab.end()) {
            const auto slab_id = vertex->second;
            mesh_id_to_vertex_slab.erase(vertex);
            free_allocation_in_slab(id, slab_id, empty_slabs);
        }
        if (auto index = mesh_id_to_index_slab.find(id); index != mesh_id_to_index_slab.end()) {
            const auto slab_id = index->second;
            mesh_id_to_index_slab.erase(index);
            free_allocation_in_slab(id, slab_id, empty_slabs);
        }
    };
    for (const auto& id : extracted_meshes.removed) free_mesh(id);
    for (const auto& id : extracted_meshes.modified) free_mesh(id);

    for (const auto slab_id : empty_slabs) {
        for (auto& [layout, slab_ids] : slab_layouts) {
            (void)layout;
            if (const auto position = std::ranges::find(slab_ids, slab_id); position != slab_ids.end()) {
                slab_ids.erase(position);
            }
        }
        slabs.erase(slab_id);
    }
}

void MeshAllocator::free_allocation_in_slab(const epix::assets::AssetId<::epix::mesh::Mesh>& id,
                                            SlabId slab_id,
                                            std::unordered_set<SlabId>& empty_slabs) {
    const auto slab = slabs.find(slab_id);
    if (slab == slabs.end()) return;
    if (slab->second.large()) {
        empty_slabs.insert(slab_id);
        return;
    }
    auto* general = slab->second.general();
    if (!general) return;
    std::optional<detail::SlabAllocation> slab_allocation;
    if (auto resident = general->resident_allocations.find(id); resident != general->resident_allocations.end()) {
        slab_allocation = resident->second;
        general->resident_allocations.erase(resident);
    } else if (auto pending = general->pending_allocations.find(id); pending != general->pending_allocations.end()) {
        slab_allocation = pending->second;
        general->pending_allocations.erase(pending);
    }
    if (!slab_allocation) return;
    general->allocator.free(slab_allocation->allocation);
    if (general->is_empty()) empty_slabs.insert(slab_id);
}

}  // namespace epix::render::mesh
