#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
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
/** @brief C++ tagged-union counterpart to Bevy `AlphaMode2d::Opaque`. */
EPIX_EXPORT struct MeshAlphaMode2dOpaque {};
/** @brief C++ tagged-union counterpart to Bevy `AlphaMode2d::Mask(f32)`. */
EPIX_EXPORT struct MeshAlphaMode2dMask {
    float cutoff = 0.5f;
    bool operator==(const MeshAlphaMode2dMask&) const noexcept = default;
};
/** @brief C++ tagged-union counterpart to Bevy `AlphaMode2d::Blend`. */
EPIX_EXPORT struct MeshAlphaMode2dBlend {};
/** @brief Alpha mode for 2D meshes (Bevy `AlphaMode2d`). */
EPIX_EXPORT using MeshAlphaMode2d = std::variant<MeshAlphaMode2dOpaque, MeshAlphaMode2dMask, MeshAlphaMode2dBlend>;

/** @brief Component that associates an entity with a mesh asset for 2D rendering. */
EPIX_EXPORT struct Mesh2d {
    assets::Handle<Mesh> handle;
};

/** @brief Flat-color material for 2D mesh rendering. */
EPIX_EXPORT struct MeshMaterial2d {
    /** @brief Base color. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Alpha blending mode. */
    MeshAlphaMode2d alpha_mode = MeshAlphaMode2dOpaque{};
};

/** @brief Textured material for 2D mesh rendering. */
EPIX_EXPORT struct MeshTextureMaterial2d {
    /** @brief Handle to the texture image asset. */
    assets::Handle<image::Image> image;
    /** @brief Color tint multiplied with the texture. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Alpha blending mode. */
    MeshAlphaMode2d alpha_mode = MeshAlphaMode2dBlend{};
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

/** @brief Render-world data for one main-world 2D mesh entity (Bevy
 * `RenderMesh2dInstance`).  Binned phase items intentionally carry only a
 * `MainEntity`; draw commands recover their mesh and material data here. */
EPIX_EXPORT struct RenderMesh2dInstance {
    ExtractedMesh2d extracted;
    MeshBatch batch;
};

/** @brief Main-entity keyed render instances (Bevy
 * `RenderMesh2dInstances`).  This is deliberately a resource rather than
 * render-entity components: batchable binned items use `Entity::PLACEHOLDER`.
 */
EPIX_EXPORT struct RenderMesh2dInstances {
    render::sync_world::MainEntityHashMap<RenderMesh2dInstance> instances;

    void clear() noexcept { instances.clear(); }
    auto find(render::sync_world::MainEntity entity) { return instances.find(entity); }
    auto find(render::sync_world::MainEntity entity) const { return instances.find(entity); }
    auto end() { return instances.end(); }
    auto end() const { return instances.end(); }
};

/** @brief Per-instance data for 2D mesh GPU instancing. */
EPIX_EXPORT struct MeshInstanceData {
    /** @brief Model transform matrix for this instance. */
    glm::mat4 model;
    /** @brief Tint color for this instance. */
    glm::vec4 color;
    /** @brief Alpha cutoff for `MeshAlphaMode2dMask`; ignored by other modes. */
    float alpha_cutoff = 0.0f;
    /** @brief Explicit storage-buffer tail padding. HLSL lays this element out
     * at a 16-byte stride after the scalar cutoff. */
    std::array<float, 3> _padding{};
};
static_assert(sizeof(MeshInstanceData) == 96);

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
            std::optional<ecs::Item<>>,
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
            std::optional<ecs::Item<>>,
            ecs::ParamSet<ecs::Res<RenderMesh2dInstances>> params,
            const wgpu::RenderPassEncoder& encoder) {
            auto&& [instances] = params.get();
            const auto instance = instances->find(item.main_entity());
            if (instance == instances->end()) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type    = render::phase::RenderCommandError::Type::Skip,
                    .message = std::format("[mesh] Main entity {:#x} has no RenderMesh2dInstance during draw.",
                                           item.main_entity().id().index),
                });
            }

            const auto& [extracted_mesh, mesh_batch] = instance->second;
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
        std::optional<ecs::Item<>>,
        ecs::ParamSet<ecs::Res<RenderMesh2dInstances>, ecs::Res<render::RenderAssets<Mesh>>,
                      ecs::Res<MeshAllocator>> params,
        const wgpu::RenderPassEncoder& encoder) {
        auto&& [instances, render_meshes, mesh_allocator] = params.get();
        const auto instance = instances->find(item.main_entity());
        if (instance == instances->end()) {
            return std::unexpected(render::phase::RenderCommandError{
                .type    = render::phase::RenderCommandError::Type::Skip,
                .message = std::format("[mesh] Main entity {:#x} has no RenderMesh2dInstance during draw.",
                                       item.main_entity().id().index),
            });
        }

        const auto& extracted_mesh = instance->second.extracted;
        auto* render_mesh                    = render_meshes->try_get(extracted_mesh.mesh);
        if (!render_mesh) {
            return std::unexpected(render::phase::RenderCommandError{
                .type = render::phase::RenderCommandError::Type::Failure,
                .message =
                    std::format("[mesh] GPU mesh {} for entity {:#x} is missing from RenderAssets<Mesh> at draw time.",
                                extracted_mesh.mesh.to_string_short(), item.entity().index),
            });
        }

        // Bevy DrawMesh2d: the buffers live in the MeshAllocator; bind the
        // vertex (and optionally index) slice for this mesh.
        const auto vertex_slice = mesh_allocator->mesh_vertex_slice(extracted_mesh.mesh);
        if (!vertex_slice) {
            return std::unexpected(render::phase::RenderCommandError{
                .type = render::phase::RenderCommandError::Type::Failure,
                .message =
                    std::format("[mesh] Mesh {} for entity {:#x} has no MeshAllocator vertex slice at draw time.",
                                extracted_mesh.mesh.to_string_short(), item.entity().index),
            });
        }
        const auto& layout     = render_mesh->layout.value->layout;
        const std::uint64_t stride = layout.array_stride;
        const auto vertex_begin    = static_cast<std::uint64_t>(vertex_slice->begin);
        encoder.setVertexBuffer(0, *vertex_slice->buffer, vertex_begin * stride,
                                static_cast<std::uint64_t>(vertex_slice->end - vertex_slice->begin) * stride);

        const auto& batch_range = item.batch_range();
        auto batch_size = render::phase::batch_range_len(batch_range);
        if (render_mesh->indexed()) {
            const auto index_slice = mesh_allocator->mesh_index_slice(extracted_mesh.mesh);
            if (!index_slice) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Failure,
                    .message = std::format("[mesh] Indexed mesh {} for entity {:#x} has no MeshAllocator index slice.",
                                           extracted_mesh.mesh.to_string_short(), item.entity().index),
                });
            }
            const auto* info = render_mesh->buffer_info.indexed_info();
            const wgpu::IndexFormat format = info ? info->index_format : wgpu::IndexFormat::eUint16;
            const std::uint64_t element_size = format == wgpu::IndexFormat::eUint16 ? 2 : 4;
            encoder.setIndexBuffer(*index_slice->buffer, format,
                                   static_cast<std::uint64_t>(index_slice->begin) * element_size,
                                   static_cast<std::uint64_t>(index_slice->end - index_slice->begin) * element_size);
            encoder.drawIndexed(static_cast<std::uint32_t>(index_slice->end - index_slice->begin), batch_size, 0, 0,
                                batch_range.first);
        } else {
            encoder.draw(static_cast<std::uint32_t>(vertex_slice->end - vertex_slice->begin), batch_size, 0,
                         batch_range.first);
        }
        return {};
    }
};

/** @brief Plugin that packs mesh GPU data into shared slab buffers and frees
 * removed/modified meshes (Bevy `MeshAllocatorPlugin`). */
EPIX_EXPORT struct MeshAllocatorPlugin {
    void attach(app::App& app);
    void ready(app::App& app);
};

/** @brief Process extracted mesh additions/modifications/removals and update
 * shared GPU slabs (Bevy `allocate_and_free_meshes`). */
EPIX_EXPORT void allocate_and_free_meshes(
    ecs::ResMut<MeshAllocator> mesh_allocator,
    ecs::Res<MeshAllocatorSettings> mesh_allocator_settings,
    ecs::Res<render::ExtractedAssets<Mesh>> extracted_meshes,
    ecs::ResMut<MeshVertexBufferLayouts> mesh_vertex_buffer_layouts,
    ecs::Res<wgpu::Device> device,
    ecs::Res<wgpu::Queue> queue);

/** @brief Plugin that sets up 2D mesh extraction, batching, and rendering. */
EPIX_EXPORT struct MeshRenderPlugin {
    void attach(app::App& app);
    void ready(app::App& app);
};
}  // namespace epix::mesh
