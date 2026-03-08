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
struct ExtractedSprite {
    core::Entity source_entity;
    Sprite sprite;
    glm::mat4 model;
    float depth;
    assets::AssetId<image::Image> texture;
    glm::vec2 image_size;
};

struct SpriteBatch {
    wgpu::BindGroup texture_bind_group;
    std::uint32_t instance_start = 0;
};

struct SpriteInstanceData {
    glm::mat4 model;
    glm::vec4 uv_offset_scale;
    glm::vec4 color;
    glm::vec4 pos_offset_scale;
};

struct SpriteGeometryBuffers {
    wgpu::Buffer position_buffer;
    wgpu::Buffer uv_buffer;
    wgpu::Buffer index_buffer;
    std::uint32_t index_count = 0;

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

struct SpriteInstanceBuffer {
    wgpu::Buffer buffer;
    wgpu::BindGroup bind_group;
    std::vector<SpriteInstanceData> instances;
};

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

struct SpritePlugin {
    void build(core::App& app);
    void finish(core::App& app);
};
}  // namespace sprite