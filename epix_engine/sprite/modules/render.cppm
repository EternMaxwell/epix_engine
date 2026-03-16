module;

export module epix.sprite:render;

import :sprite;
import glm;
import webgpu;
import epix.assets;
import epix.core;
import epix.image;
import epix.render;
import std;

export namespace sprite {
/** @brief Snapshot of a sprite extracted from the main world for rendering.
 *
 * Created during the extract phase so the render world has an immutable
 * copy of every visible sprite's transform, color, texture, and depth.
 */
struct ExtractedSprite {
    /** @brief Entity in the main world this sprite was extracted from. */
    core::Entity source_entity;
    /** @brief Copy of the sprite's visual properties. */
    Sprite sprite;
    /** @brief Model matrix representing the sprite's world transform. */
    glm::mat4 model;
    /** @brief Depth value used for sorting transparent sprites. */
    float depth;
    /** @brief Asset ID of the sprite's texture image. */
    assets::AssetId<image::Image> texture;
    /** @brief Pixel dimensions of the source image. */
    glm::vec2 image_size;
};

/** @brief GPU batch for sprites sharing the same texture bind group.
 *
 * Each batch references a texture bind group and the starting offset
 * into the shared instance buffer for instanced draw calls.
 */
struct SpriteBatch {
    /** @brief Bind group holding the texture and sampler for this batch. */
    wgpu::BindGroup texture_bind_group;
    /** @brief Index of the first instance in SpriteInstanceBuffer for this
     * batch. */
    std::uint32_t instance_start = 0;
};

/** @brief Per-instance GPU data for a single sprite in an instanced draw
 * call. */
struct SpriteInstanceData {
    /** @brief Model matrix for this sprite instance. */
    glm::mat4 model;
    /** @brief UV offset (xy) and scale (zw) within the texture atlas or
     * sheet. */
    glm::vec4 uv_offset_scale;
    /** @brief Tint color for this instance. */
    glm::vec4 color;
    /** @brief Position offset (xy) and scale (zw) applied to the quad
     * vertices. */
    glm::vec4 pos_offset_scale;
};

/** @brief Shared GPU buffers for the unit-quad geometry used by all sprites.
 *
 * Created once as a resource; all sprite instances share these vertex and
 * index buffers and are differentiated only through per-instance data.
 */
struct SpriteGeometryBuffers {
    /** @brief Vertex buffer containing quad corner positions. */
    wgpu::Buffer position_buffer;
    /** @brief Vertex buffer containing quad UV coordinates. */
    wgpu::Buffer uv_buffer;
    /** @brief Index buffer for the two-triangle quad. */
    wgpu::Buffer index_buffer;
    /** @brief Number of indices in the index buffer. */
    std::uint32_t index_count = 0;

    /** @brief Constructs the quad geometry buffers on the GPU.
     * @param world World providing the `wgpu::Device` and `wgpu::Queue`
     * resources. */
    explicit SpriteGeometryBuffers(core::World& world) {
        auto& device = world.resource<wgpu::Device>();
        auto& queue  = world.resource<wgpu::Queue>();

        constexpr std::array quad_positions = {
            glm::vec2(-0.5f, -0.5f),
            glm::vec2(0.5f, -0.5f),
            glm::vec2(0.5f, 0.5f),
            glm::vec2(-0.5f, 0.5f),
        };
        constexpr std::array quad_uvs = {
            glm::vec2(0.0f, 0.0f),
            glm::vec2(1.0f, 0.0f),
            glm::vec2(1.0f, 1.0f),
            glm::vec2(0.0f, 1.0f),
        };
        constexpr std::array<std::uint16_t, 6> quad_indices = {0, 1, 2, 2, 3, 0};

        position_buffer = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("SpriteQuadPositions")
                                                  .setSize(sizeof(quad_positions))
                                                  .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst));
        uv_buffer       = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("SpriteQuadUvs")
                                                  .setSize(sizeof(quad_uvs))
                                                  .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst));
        index_buffer    = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("SpriteQuadIndices")
                                                  .setSize(sizeof(quad_indices))
                                                  .setUsage(wgpu::BufferUsage::eIndex | wgpu::BufferUsage::eCopyDst));

        queue.writeBuffer(position_buffer, 0, quad_positions.data(), sizeof(quad_positions));
        queue.writeBuffer(uv_buffer, 0, quad_uvs.data(), sizeof(quad_uvs));
        queue.writeBuffer(index_buffer, 0, quad_indices.data(), sizeof(quad_indices));
        index_count = static_cast<std::uint32_t>(quad_indices.size());
    }
};

/** @brief GPU buffer holding all sprite instance data for the current frame.
 *
 * Sprite instances are collected, uploaded to `buffer`, and bound via
 * `bind_group` during the prepare phase. */
struct SpriteInstanceBuffer {
    /** @brief GPU buffer storing the array of SpriteInstanceData. */
    wgpu::Buffer buffer;
    /** @brief Bind group exposing the instance buffer to shaders. */
    wgpu::BindGroup bind_group;
    /** @brief CPU-side instance data staged before uploading. */
    std::vector<SpriteInstanceData> instances;
};

/** @brief Render command that binds the sprite instance buffer at the given
 * bind group slot.
 * @tparam Slot Bind group index for the instance buffer. */
template <size_t Slot>
struct BindSpriteInstances {
    template <render::phase::PhaseItem PhaseItem>
    struct Command {
        void prepare(const core::World&) {}

        std::expected<void, render::phase::RenderCommandError> render(
            const PhaseItem& item,
            core::Item<const render::view::ViewBindGroup&>,
            std::optional<core::Item<const SpriteBatch&>>,
            core::ParamSet<core::Res<SpriteInstanceBuffer>> params,
            const wgpu::RenderPassEncoder& encoder) {
            auto&& [instances] = params.get();
            if (!instances->bind_group) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type    = render::phase::RenderCommandError::Type::Failure,
                    .message = std::format("[sprite] Sprite entity {:#x} is missing prepared instance bind group.",
                                           item.entity().index),
                });
            }

            encoder.setBindGroup(Slot, instances->bind_group, std::span<const std::uint32_t>{});
            return {};
        }
    };
};

/** @brief Render command that binds a sprite batch's texture at the given
 * bind group slot.
 * @tparam Slot Bind group index for the texture. */
template <size_t Slot>
struct BindSpriteTexture {
    template <render::phase::PhaseItem PhaseItem>
    struct Command {
        void prepare(const core::World&) {}

        std::expected<void, render::phase::RenderCommandError> render(
            const PhaseItem& item,
            core::Item<const render::view::ViewBindGroup&>,
            std::optional<core::Item<const SpriteBatch&>> entity_item,
            core::ParamSet<>,
            const wgpu::RenderPassEncoder& encoder) {
            if (!entity_item) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Failure,
                    .message =
                        std::format("[sprite] Sprite batch entity {:#x} is missing SpriteBatch.", item.entity().index),
                });
            }

            auto&& [sprite_batch] = **entity_item;
            if (!sprite_batch.texture_bind_group) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type    = render::phase::RenderCommandError::Type::Failure,
                    .message = std::format("[sprite] Sprite entity {:#x} is missing prepared texture bind group.",
                                           item.entity().index),
                });
            }

            encoder.setBindGroup(Slot, sprite_batch.texture_bind_group, std::span<const std::uint32_t>{});
            return {};
        }
    };
};

/** @brief Render command that issues the instanced draw call for a sprite
 * batch.
 * @tparam PhaseItem The render phase item type driving draw ordering. */
template <render::phase::PhaseItem PhaseItem>
struct DrawSpriteBatch {
    void prepare(const core::World&) {}

    std::expected<void, render::phase::RenderCommandError> render(
        const PhaseItem& item,
        core::Item<const render::view::ViewBindGroup&>,
        std::optional<core::Item<const SpriteBatch&>> entity_item,
        core::ParamSet<core::Res<SpriteGeometryBuffers>> params,
        const wgpu::RenderPassEncoder& encoder) {
        if (!entity_item) {
            return std::unexpected(render::phase::RenderCommandError{
                .type = render::phase::RenderCommandError::Type::Failure,
                .message =
                    std::format("[sprite] Sprite batch entity {:#x} is missing SpriteBatch.", item.entity().index),
            });
        }

        auto&& [sprite_batch] = **entity_item;
        auto&& [geometry]     = params.get();
        encoder.setVertexBuffer(0, geometry->position_buffer, 0, geometry->position_buffer.getSize());
        encoder.setVertexBuffer(1, geometry->uv_buffer, 0, geometry->uv_buffer.getSize());
        encoder.setIndexBuffer(geometry->index_buffer, wgpu::IndexFormat::eUint16, 0, geometry->index_buffer.getSize());
        encoder.drawIndexed(geometry->index_count, static_cast<std::uint32_t>(item.batch_size()), 0, 0,
                            sprite_batch.instance_start);
        return {};
    }
};

/** @brief Plugin that registers the sprite rendering pipeline, including
 * extraction, batching, and draw commands. */
struct SpritePlugin {
    void build(core::App& app);
    void finish(core::App& app);
};
}  // namespace sprite