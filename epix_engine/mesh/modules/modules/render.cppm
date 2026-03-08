module;

export module epix.mesh:render;

import glm;
import webgpu;
import epix.assets;
import epix.core;
import epix.image;
import epix.render;
import std;

import :mesh;
import :gpumesh;

namespace mesh {
export enum class MeshAlphaMode2d {
    Opaque,
    Blend,
};

export struct Mesh2d {
    assets::Handle<Mesh> handle;
};

export struct MeshMaterial2d {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    MeshAlphaMode2d alpha_mode = MeshAlphaMode2d::Opaque;
};

export struct MeshTextureMaterial2d {
    assets::Handle<image::Image> image;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    MeshAlphaMode2d alpha_mode = MeshAlphaMode2d::Blend;
};

export struct ExtractedMesh2d {
    core::Entity source_entity;
    assets::AssetId<Mesh> mesh;
    glm::mat4 model;
    glm::vec4 color;
    float depth;
    MeshAlphaMode2d alpha_mode;
    std::optional<assets::AssetId<image::Image>> texture;
};

export struct MeshBatch {
    std::optional<wgpu::BindGroup> texture_bind_group;
    std::uint32_t instance_start = 0;
};

export struct MeshInstanceData {
    glm::mat4 model;
    glm::vec4 color;
};

export struct MeshInstanceBuffer {
    wgpu::Buffer buffer;
    wgpu::BindGroup bind_group;
    std::vector<MeshInstanceData> instances;
};

export template <size_t Slot>
struct BindMesh2dInstances {
    template <render::phase::PhaseItem PhaseItem>
    struct Command {
        void prepare(const core::World&) {}

        std::expected<void, render::phase::RenderCommandError> render(
            const PhaseItem& item,
            core::Item<const render::view::ViewBindGroup&>,
            std::optional<core::Item<const MeshBatch&, const ExtractedMesh2d&>>,
            core::ParamSet<core::Res<MeshInstanceBuffer>> params,
            const wgpu::RenderPassEncoder& encoder) {
            auto&& [instances] = params.get();
            if (!instances->bind_group) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type    = render::phase::RenderCommandError::Type::Failure,
                    .message = std::format("[mesh] Mesh instance buffer bind group is not ready for entity {:#x}.",
                                           item.entity().index),
                });
            }

            encoder.setBindGroup(Slot, instances->bind_group, std::span<const std::uint32_t>{});
            return {};
        }
    };
};

export template <size_t Slot>
struct BindMesh2dTexture {
    template <render::phase::PhaseItem PhaseItem>
    struct Command {
        void prepare(const core::World&) {}

        std::expected<void, render::phase::RenderCommandError> render(
            const PhaseItem& item,
            core::Item<const render::view::ViewBindGroup&>,
            std::optional<core::Item<const MeshBatch&, const ExtractedMesh2d&>> entity_item,
            core::ParamSet<>,
            const wgpu::RenderPassEncoder& encoder) {
            if (!entity_item) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Failure,
                    .message =
                        std::format("[mesh] Mesh entity {:#x} is missing MeshBatch or ExtractedMesh2d during draw.",
                                    item.entity().index),
                });
            }

            auto&& [mesh_batch, extracted_mesh] = **entity_item;
            if (!extracted_mesh.texture) {
                return {};
            }
            if (!mesh_batch.texture_bind_group) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Failure,
                    .message =
                        std::format("[mesh] Entity {:#x} requires texture {} but MeshBatch has no texture bind group.",
                                    item.entity().index, extracted_mesh.texture->to_string_short()),
                });
            }

            encoder.setBindGroup(Slot, *mesh_batch.texture_bind_group, std::span<const std::uint32_t>{});
            return {};
        }
    };
};

export template <render::phase::PhaseItem PhaseItem>
struct DrawMesh2dBatch {
    void prepare(const core::World&) {}

    std::expected<void, render::phase::RenderCommandError> render(
        const PhaseItem& item,
        core::Item<const render::view::ViewBindGroup&>,
        std::optional<core::Item<const MeshBatch&, const ExtractedMesh2d&>> entity_item,
        core::ParamSet<core::Res<render::RenderAssets<Mesh>>> params,
        const wgpu::RenderPassEncoder& encoder) {
        if (!entity_item) {
            return std::unexpected(render::phase::RenderCommandError{
                .type    = render::phase::RenderCommandError::Type::Failure,
                .message = std::format("[mesh] Mesh entity {:#x} is missing MeshBatch or ExtractedMesh2d during draw.",
                                       item.entity().index),
            });
        }

        auto&& [mesh_batch, extracted_mesh] = **entity_item;
        auto&& [gpu_meshes]                 = params.get();
        auto* gpu_mesh                      = gpu_meshes->try_get(extracted_mesh.mesh);
        if (!gpu_mesh) {
            return std::unexpected(render::phase::RenderCommandError{
                .type = render::phase::RenderCommandError::Type::Failure,
                .message =
                    std::format("[mesh] GPU mesh {} for entity {:#x} is missing from RenderAssets<Mesh> at draw time.",
                                extracted_mesh.mesh.to_string_short(), item.entity().index),
            });
        }

        auto batch_size = static_cast<std::uint32_t>(item.batch_size());
        gpu_mesh->bind_to(encoder);
        if (gpu_mesh->is_indexed()) {
            encoder.drawIndexed(static_cast<std::uint32_t>(gpu_mesh->vertex_count()), batch_size, 0, 0,
                                mesh_batch.instance_start);
        } else {
            encoder.draw(static_cast<std::uint32_t>(gpu_mesh->vertex_count()), batch_size, 0,
                         mesh_batch.instance_start);
        }
        return {};
    }
};

export struct MeshRenderPlugin {
    void build(core::App& app);
    void finish(core::App& app);
};
}  // namespace mesh