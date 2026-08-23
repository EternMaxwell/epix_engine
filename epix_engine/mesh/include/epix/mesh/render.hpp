#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <expected>
#include <format>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif
#include <epix/mesh/gpumesh.hpp>
#include <epix/mesh/mesh.hpp>

namespace epix::mesh {
/** @brief Alpha blending mode for 2D mesh rendering. */
EPIX_EXPORT enum class MeshAlphaMode2d {
    Opaque,
    Blend,
};

/** @brief Component that associates an entity with a mesh asset for 2D rendering. */
EPIX_EXPORT struct Mesh2d {
    assets::Handle<Mesh> handle;
};

/** @brief Flat-color material for 2D mesh rendering. */
EPIX_EXPORT struct MeshMaterial2d {
    /** @brief Base color. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Alpha blending mode. */
    MeshAlphaMode2d alpha_mode = MeshAlphaMode2d::Opaque;
};

/** @brief Textured material for 2D mesh rendering. */
EPIX_EXPORT struct MeshTextureMaterial2d {
    /** @brief Handle to the texture image asset. */
    assets::Handle<image::Image> image;
    /** @brief Color tint multiplied with the texture. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Alpha blending mode. */
    MeshAlphaMode2d alpha_mode = MeshAlphaMode2d::Blend;
};

/** @brief Extracted mesh data ready for the render world. */
EPIX_EXPORT struct ExtractedMesh2d {
    /** @brief Entity in the main world this was extracted from. */
    ecs::Entity source_entity;
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
    camera::RenderLayers render_layer = camera::RenderLayers::layer(0);
};

/** @brief Batching key for 2D mesh draw commands (groups by texture). */
EPIX_EXPORT struct MeshBatch {
    /** @brief Optional texture bind group for this batch. */
    std::optional<wgpu::BindGroup> texture_bind_group;
    /** @brief Starting instance index within the instance buffer. */
    std::uint32_t instance_start = 0;
};

/** @brief Per-instance data for 2D mesh GPU instancing. */
EPIX_EXPORT struct MeshInstanceData {
    /** @brief Model transform matrix for this instance. */
    glm::mat4 model;
    /** @brief Tint color for this instance. */
    glm::vec4 color;
};

/** @brief GPU buffer and bind group for mesh 2D instance data. */
EPIX_EXPORT struct MeshInstanceBuffer {
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
EPIX_EXPORT template <std::size_t Slot>
struct BindMesh2dInstances {
    template <render::phase::PhaseItem PhaseItem>
    struct Command {
        void prepare(const ecs::World&) noexcept {}

        std::expected<void, render::phase::RenderCommandError> render(
            const PhaseItem& item,
            ecs::Item<const render::view::ViewBindGroup&>,
            std::optional<ecs::Item<const MeshBatch&, const ExtractedMesh2d&>>,
            ecs::ParamSet<ecs::Res<MeshInstanceBuffer>> params,
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
EPIX_EXPORT template <std::size_t Slot>
struct BindMesh2dTexture {
    template <render::phase::PhaseItem PhaseItem>
    struct Command {
        void prepare(const ecs::World&) noexcept {}

        std::expected<void, render::phase::RenderCommandError> render(
            const PhaseItem& item,
            ecs::Item<const render::view::ViewBindGroup&>,
            std::optional<ecs::Item<const MeshBatch&, const ExtractedMesh2d&>> entity_item,
            ecs::ParamSet<>,
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
EPIX_EXPORT template <render::phase::PhaseItem PhaseItem>
struct DrawMesh2dBatch {
    void prepare(const ecs::World&) noexcept {}

    std::expected<void, render::phase::RenderCommandError> render(
        const PhaseItem& item,
        ecs::Item<const render::view::ViewBindGroup&>,
        std::optional<ecs::Item<const MeshBatch&, const ExtractedMesh2d&>> entity_item,
        ecs::ParamSet<ecs::Res<render::RenderAssets<Mesh>>> params,
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

        auto batch_size = render::phase::batch_range_len(item.batch_range);
        gpu_mesh->bind_to(encoder);
        if (gpu_mesh->is_indexed()) {
            encoder.drawIndexed(static_cast<std::uint32_t>(gpu_mesh->vertex_count()), batch_size, 0, 0,
                                item.batch_range.first);
        } else {
            encoder.draw(static_cast<std::uint32_t>(gpu_mesh->vertex_count()), batch_size, 0, item.batch_range.first);
        }
        return {};
    }
};

/** @brief Plugin that sets up 2D mesh extraction, batching, and rendering. */
EPIX_EXPORT struct MeshRenderPlugin {
    void attach(app::App& app);
    void ready(app::App& app);
};
}  // namespace epix::mesh