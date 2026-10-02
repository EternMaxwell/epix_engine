
#include <epix/render/mesh/render_asset.hpp>

namespace epix::render {

std::expected<mesh::RenderMesh, PrepareAssetError<epix::mesh::Mesh>> RenderAsset<mesh::RenderMesh>::prepare_asset(
    epix::mesh::Mesh&& mesh,
    epix::assets::AssetId<epix::mesh::Mesh> id,
    Param params,
    const mesh::RenderMesh* previous) {
    (void)id;
    (void)previous;
    // Bevy passes `SystemParamItem<Self::Param>` already resolved.
    auto&& [images, layouts] = params;

    std::optional<wgpu::TextureView> morph_targets;
    if (auto target_handle = mesh.morph_targets()) {
        const auto* target_image = images->get(target_handle->get().id());
        if (target_image == nullptr) {
            return std::unexpected(epix::render::PrepareAssetError<epix::mesh::Mesh>{
                epix::render::RetryNextAssetUpdate<epix::mesh::Mesh>{std::move(mesh)}});
        }
        morph_targets = target_image->texture_view;
    }

    epix::render::mesh::RenderMeshBufferInfo buffer_info;
    if (auto indices = mesh.indices(); indices) {
        const auto& index = indices->get();
        buffer_info = epix::render::mesh::RenderMeshBufferInfo::indexed(static_cast<std::uint32_t>(index.len()),
                                                                        static_cast<wgpu::IndexFormat>(index));
    } else {
        buffer_info = epix::render::mesh::RenderMeshBufferInfo::non_indexed();
    }
    auto key_bits = epix::mesh::BaseMeshPipelineKey::from_primitive_topology(mesh.primitive_topology());
    if (morph_targets) key_bits |= epix::mesh::BaseMeshPipelineKey::MORPH_TARGETS;

    return epix::render::mesh::RenderMesh{
        .vertex_count  = static_cast<std::uint32_t>(mesh.count_vertices()),
        .morph_targets = std::move(morph_targets),
        .buffer_info   = std::move(buffer_info),
        .key_bits      = key_bits,
        .layout        = mesh.get_mesh_vertex_buffer_layout(*layouts),
    };
}

std::optional<std::size_t> RenderAsset<mesh::RenderMesh>::byte_len(const epix::mesh::Mesh& mesh) const {
    std::size_t vertex_size = 0;
    for (const auto& [attribute, values] : mesh.attributes()) {
        vertex_size += epix::mesh::vertex_format_size(attribute.format);
    }
    const std::size_t vertex_count = mesh.count_vertices();
    std::size_t index_bytes        = 0;
    if (const auto bytes = mesh.get_index_buffer_bytes()) index_bytes = bytes->size();
    return vertex_size * vertex_count + index_bytes;
}

}  // namespace epix::render
