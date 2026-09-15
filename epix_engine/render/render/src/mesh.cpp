

#include <algorithm>
#include <cstring>
#include <epix/mesh/mesh.hpp>
#include <epix/render/mesh/render_asset.hpp>

using namespace epix;
using namespace epix::render::mesh;

void epix::render::mesh::inherit_weights(
    ecs::Query<ecs::Item<const ecs::Children&, const ::epix::mesh::MorphWeights&>,
               ecs::Filter<ecs::Without<::epix::mesh::Mesh3d>, ecs::Modified<::epix::mesh::MorphWeights>>> morph_nodes,
    ecs::Query<ecs::Item<ecs::Mut<::epix::mesh::MeshMorphWeights>>, ecs::With<::epix::mesh::Mesh3d>> morph_primitives) {
    for (auto&& [children, parent_weights] : morph_nodes.iter()) {
        for (const ecs::Entity child : children.entities()) {
            if (auto primitive = morph_primitives.get(child)) {
                auto&& [child_weights] = *primitive;
                auto& weights          = child_weights.get_mut();
                weights.clear_weights();
                weights.extend_weights(parent_weights.weights());
            }
        }
    }
}

void epix::render::mesh::MorphPlugin::attach(app::App& app) {
    app.configure_sets(app::PostUpdate, ecs::sets(::epix::mesh::InheritWeightSystems{}));
    app.add_systems(
        app::PostUpdate,
        ecs::into(inherit_weights).in_set(::epix::mesh::InheritWeightSystems{}).set_name("inherit morph weights"));
}

std::uint32_t epix::render::mesh::vertex_array_stride(const epix::mesh::Mesh& mesh) {
    std::uint32_t stride = 0;
    for (const auto& [attribute, values] : mesh.attributes()) {
        stride += epix::mesh::vertex_format_size(attribute.format);
    }
    return stride;
}

std::vector<std::uint8_t> epix::render::mesh::packed_vertex_bytes(const epix::mesh::Mesh& mesh) {
    const auto count  = mesh.count_vertices();
    const auto stride = vertex_array_stride(mesh);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(stride) * count, 0);
    std::uint32_t attr_offset = 0;
    for (const auto& [attribute, values] : mesh.attributes()) {
        const auto elem_size  = epix::mesh::vertex_format_size(attribute.format);
        const auto bytes      = values.get_bytes();
        const auto copy_count = elem_size == 0 ? 0 : std::min(count, bytes.size() / elem_size);
        for (std::size_t v = 0; v < copy_count; ++v) {
            std::memcpy(out.data() + v * stride + attr_offset, bytes.data() + v * elem_size, elem_size);
        }
        attr_offset += static_cast<std::uint32_t>(elem_size);
    }
    return out;
}
