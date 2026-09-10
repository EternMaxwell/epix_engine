#include <epix/app.hpp>
#include <epix/mesh/mesh.hpp>
#include <epix/mesh/vertex_buffer_layout.hpp>
#include <epix/render.hpp>

using namespace epix;
using namespace epix::ecs;

void epix::render::mesh::MeshAllocator::allocate_meshes(
    const MeshAllocatorSettings& settings,
    const render::ExtractedAssets<::epix::mesh::Mesh>& extracted_meshes,
    ::epix::mesh::MeshVertexBufferLayouts& mesh_vertex_buffer_layouts,
    const wgpu::Device& device, const wgpu::Queue& queue) {
    detail::SlabsToReallocate slabs_to_reallocate;

    for (const auto& [id, source_mesh] : extracted_meshes.extracted) {
        const auto vertex_layout = source_mesh.get_mesh_vertex_buffer_layout(mesh_vertex_buffer_layouts);
        if (!vertex_layout.value || vertex_layout.value->layout().array_stride == 0) continue;
        const auto vertex_stride = vertex_layout.value->layout().array_stride;
        const auto vertex_bytes  = static_cast<std::uint64_t>(source_mesh.count_vertices()) * vertex_stride;
        if (vertex_bytes == 0) continue;

        const auto element_layout = detail::ElementLayout::make(detail::ElementClass::Vertex, vertex_stride);
        if (general_vertex_slabs_supported) {
            allocate(id, vertex_bytes, element_layout, slabs_to_reallocate, settings);
        } else {
            allocate_large(id, element_layout);
        }

        if (const auto indices = source_mesh.indices()) {
            const auto& index                = indices->get();
            const std::uint32_t element_size = static_cast<wgpu::IndexFormat>(index) == wgpu::IndexFormat::eUint16
                                                   ? sizeof(std::uint16_t)
                                                   : sizeof(std::uint32_t);
            allocate(id, static_cast<std::uint64_t>(index.len()) * element_size,
                     detail::ElementLayout::make(detail::ElementClass::Index, element_size),
                     slabs_to_reallocate, settings);
        }
    }

    for (const auto& [slab_id, reallocate] : slabs_to_reallocate.slabs) {
        reallocate_slab(device, queue, slab_id, reallocate);
    }

    for (const auto& [id, source_mesh] : extracted_meshes.extracted) {
        if (const auto vertex_slab = mesh_id_to_vertex_slab.find(id);
            vertex_slab != mesh_id_to_vertex_slab.end()) {
            const auto packed = packed_vertex_bytes(source_mesh);
            if (!packed.empty()) {
                copy_element_data(device, queue, vertex_slab->second, id, packed.data(), packed.size(),
                                  wgpu::BufferUsage::eVertex);
            }
        }
        if (const auto index_slab = mesh_id_to_index_slab.find(id);
            index_slab != mesh_id_to_index_slab.end()) {
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
    ResMut<MeshAllocator> mesh_allocator, Res<MeshAllocatorSettings> mesh_allocator_settings,
    Res<render::ExtractedAssets<::epix::mesh::Mesh>> extracted_meshes,
    ResMut<::epix::mesh::MeshVertexBufferLayouts> mesh_vertex_buffer_layouts, Res<wgpu::Device> device,
    Res<wgpu::Queue> queue) {
    mesh_allocator->free_meshes(*extracted_meshes);
    mesh_allocator->allocate_meshes(*mesh_allocator_settings, *extracted_meshes,
                                    mesh_vertex_buffer_layouts.get_mut(), *device, *queue);
}

void epix::render::mesh::MeshAllocatorPlugin::attach(app::App& app) {
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<MeshAllocatorSettings>();
        render_app->get().add_systems(render::Render, into(allocate_and_free_meshes)
                                                          .before(render::prepare_assets<::epix::mesh::Mesh>)
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
    app.add_plugins(render::RenderAssetPlugin<::epix::mesh::Mesh, image::Image>{});
    app.add_plugins(MeshAllocatorPlugin{});

    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<::epix::mesh::MeshVertexBufferLayouts>();
    }
}
