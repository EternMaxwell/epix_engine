module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <vector>
#endif
export module epix.mesh:render;

import glm;
import webgpu;
import epix.assets;
import epix.core;
import epix.image;
import epix.render;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :mesh;
import :gpumesh;

namespace epix::mesh {
/** @brief Alpha blending mode for 2D mesh rendering. */
export enum class MeshAlphaMode2d {
    Opaque,
    Blend,
};

/** @brief Component that associates an entity with a mesh asset for 2D rendering. */
export struct Mesh2d {
    assets::Handle<Mesh> handle;
};

/** @brief Flat-color material for 2D mesh rendering. */
export struct MeshMaterial2d {
    /** @brief Base color. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Alpha blending mode. */
    MeshAlphaMode2d alpha_mode = MeshAlphaMode2d::Opaque;
};

/** @brief Textured material for 2D mesh rendering. */
export struct MeshTextureMaterial2d {
    /** @brief Handle to the texture image asset. */
    assets::Handle<image::Image> image;
    /** @brief Color tint multiplied with the texture. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Alpha blending mode. */
    MeshAlphaMode2d alpha_mode = MeshAlphaMode2d::Blend;
};

/** @brief Extracted mesh data ready for the render world. */
export struct ExtractedMesh2d {
    /** @brief Entity in the main world this was extracted from. */
    core::Entity source_entity;
    /** @brief Asset ID of the mesh. */
    assets::AssetId<Mesh> mesh;
    /** @brief Model (world) transform matrix. */
    glm::mat4 model;
    /** @brief Tint color. */
    glm::vec4 color;
    /** @brief Depth value for sorting. */
    float depth;
    /** @brief Alpha blending mode. */
    MeshAlphaMode2d alpha_mode;
    /** @brief Optional texture asset ID. */
    std::optional<assets::AssetId<image::Image>> texture;
    /** @brief Render layers this entity belongs to. Default: layer 0. */
    render::camera::RenderLayer render_layer = render::camera::RenderLayer::layer(0);
};

/** @brief Batching key for 2D mesh draw commands (groups by texture). */
export struct MeshBatch {
    /** @brief Optional texture bind group for this batch. */
    std::optional<wgpu::BindGroup> texture_bind_group;
    /** @brief Starting instance index within the instance buffer. */
    std::uint32_t instance_start = 0;
};

/** @brief Per-instance data for 2D mesh GPU instancing. */
export struct MeshInstanceData {
    /** @brief Model transform matrix for this instance. */
    glm::mat4 model;
    /** @brief Tint color for this instance. */
    glm::vec4 color;
};

/** @brief GPU buffer and bind group for mesh 2D instance data. */
export struct MeshInstanceBuffer {
    /** @brief GPU buffer holding instance data. */
    wgpu::Buffer buffer;
    /** @brief Bind group for the instance buffer. */
    wgpu::BindGroup bind_group;
    /** @brief CPU-side instance data staged for upload. */
    std::vector<MeshInstanceData> instances;
};

/** @brief Render command that binds the mesh instance buffer to a given slot.
 * @tparam Slot Bind group slot index.
 */
export template <std::size_t Slot>
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

/** @brief Render command that binds a mesh 2D texture at a given slot.
 * @tparam Slot Bind group slot index. */
export template <std::size_t Slot>
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

/** @brief Render command that draws a batched 2D mesh.
 * @tparam PhaseItem The phase item type providing entity/batch info. */
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

        if (gpu_mesh->vertex_count() == 0) {
            return {};
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

/** @brief Plugin that sets up 2D mesh extraction, batching, and rendering. */
export struct MeshRenderPlugin {
    void build(core::App& app);
    void finish(core::App& app);
};
}  // namespace epix::mesh