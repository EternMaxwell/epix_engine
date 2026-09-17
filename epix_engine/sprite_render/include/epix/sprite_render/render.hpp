#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <bit>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <expected>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/sprite/sprite.hpp>

EPIX_EXPORT namespace epix::sprite_render {
    struct ExtractedSlice {
        glm::vec2 offset{0.0f};
        image::Rect rect{};
        glm::vec2 size{0.0f};
    };

    class ComputedTextureSlices {
        std::vector<sprite::TextureSlice> values_;

       public:
        explicit ComputedTextureSlices(std::vector<sprite::TextureSlice> values) : values_(std::move(values)) {}

        auto extract_slices(const sprite::Sprite& value, glm::vec2 anchor) const {
            const auto flip = glm::vec2(value.flip_x ? -1.0f : 1.0f, value.flip_y ? -1.0f : 1.0f);
            const auto anchor_offset =
                anchor * value.custom_size.value_or(value.rect.value_or(image::Rect{}).size());
            return values_ | std::views::transform([flip, anchor_offset](const sprite::TextureSlice& slice) {
                       return ExtractedSlice{
                           .offset = slice.offset * flip - anchor_offset,
                           .rect   = slice.texture_rect,
                           .size   = slice.draw_size,
                       };
                   });
        }
    };

    struct ExtractedSpriteSingle {
        glm::vec2 anchor{0.0f};
        std::optional<image::Rect> rect;
        std::optional<sprite::SpriteScalingMode> scaling_mode;
        std::optional<glm::vec2> custom_size;
    };

    struct ExtractedSpriteSlices {
        std::size_t begin = 0;
        std::size_t end   = 0;
    };

    using ExtractedSpriteKind = std::variant<ExtractedSpriteSingle, ExtractedSpriteSlices>;

    /** @brief Per-frame extracted slice storage (Bevy `ExtractedSlices`). */
    struct ExtractedSlices {
        std::vector<ExtractedSlice> slices;
    };

    /** @brief Snapshot of one visible sprite in the render world (Bevy `ExtractedSprite`). */
    struct ExtractedSprite {
        ecs::Entity main_entity;
        ecs::Entity render_entity;
        transform::GlobalTransform transform;
        glm::vec4 color{1.0f};
        assets::AssetId<image::Image> image_handle_id;
        bool flip_x = false;
        bool flip_y = false;
        ExtractedSpriteKind kind{ExtractedSpriteSingle{}};
    };

    struct ExtractedSprites {
        std::vector<ExtractedSprite> sprites;
    };

    struct SpriteAssetEvents {
        std::vector<assets::AssetEvent<image::Image>> images;
    };

    /** @brief Bit-packed sprite specialization key (Bevy `SpritePipelineKey`). */
    struct SpritePipelineKey {
        enum Bit : std::uint32_t {
            NONE                                             = 0,
            HDR                                              = 1u << 0u,
            TONEMAP_IN_SHADER                                = 1u << 1u,
            DEBAND_DITHER                                    = 1u << 2u,
            TONEMAP_METHOD_NONE                              = 0u << 26u,
            TONEMAP_METHOD_REINHARD                          = 1u << 26u,
            TONEMAP_METHOD_REINHARD_LUMINANCE                = 2u << 26u,
            TONEMAP_METHOD_ACES_FITTED                       = 3u << 26u,
            TONEMAP_METHOD_AGX                               = 4u << 26u,
            TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM = 5u << 26u,
            TONEMAP_METHOD_TONY_MC_MAPFACE                   = 6u << 26u,
            TONEMAP_METHOD_BLENDER_FILMIC                    = 7u << 26u,
            TONEMAP_METHOD_RESERVED_BITS                     = 0b111u << 26u,
            MSAA_RESERVED_BITS                               = 0b111u << 29u,
        };

        std::uint32_t value = NONE;

        constexpr SpritePipelineKey() noexcept = default;
        constexpr SpritePipelineKey(Bit bit) noexcept : value(static_cast<std::uint32_t>(bit)) {}
        explicit constexpr SpritePipelineKey(std::uint32_t bits) noexcept : value(bits) {}

        static constexpr SpritePipelineKey from_msaa_samples(std::uint32_t samples) noexcept {
            return SpritePipelineKey{(std::countr_zero(samples) & 0b111u) << 29u};
        }
        static constexpr SpritePipelineKey from_hdr(bool hdr) noexcept {
            return hdr ? SpritePipelineKey{HDR} : SpritePipelineKey{};
        }
        constexpr std::uint32_t bits() const noexcept { return value; }
        constexpr std::uint32_t msaa_samples() const noexcept { return 1u << ((value >> 29u) & 0b111u); }
        constexpr bool contains(Bit bit) const noexcept {
            const auto mask = static_cast<std::uint32_t>(bit);
            return (value & mask) == mask;
        }
        constexpr SpritePipelineKey intersection(Bit mask) const noexcept {
            return SpritePipelineKey{value & static_cast<std::uint32_t>(mask)};
        }
        constexpr SpritePipelineKey& operator|=(SpritePipelineKey other) noexcept {
            value |= other.value;
            return *this;
        }
        constexpr SpritePipelineKey& operator|=(Bit bit) noexcept {
            value |= static_cast<std::uint32_t>(bit);
            return *this;
        }
        auto operator<=>(const SpritePipelineKey&) const = default;
    };

    constexpr SpritePipelineKey operator|(SpritePipelineKey lhs, SpritePipelineKey rhs) noexcept {
        lhs |= rhs;
        return lhs;
    }
    constexpr SpritePipelineKey operator|(SpritePipelineKey lhs, SpritePipelineKey::Bit rhs) noexcept {
        lhs |= rhs;
        return lhs;
    }

    struct SpritePipeline {
        using Key = SpritePipelineKey;

        // Descriptors, as Bevy's are; the pipeline cache interns them and the
        // bind groups fetch the interned layout at their call sites.
        render::BindGroupLayoutDescriptor view_layout;
        render::BindGroupLayoutDescriptor material_layout;
        assets::Handle<shader::Shader> shader;

        render::RenderPipelineDescriptor specialize(Key key) const;
    };

    /** @brief Exact 80-byte instance ABI consumed as instance-rate vertex data. */
    struct SpriteInstance {
        glm::vec4 i_model_transpose_col0{0.0f};
        glm::vec4 i_model_transpose_col1{0.0f};
        glm::vec4 i_model_transpose_col2{0.0f};
        glm::vec4 i_color{1.0f};
        glm::vec4 i_uv_offset_scale{0.0f};
    };
    static_assert(sizeof(SpriteInstance) == 80);

    namespace detail {
    /** @brief Direct Bevy scaling arithmetic, exposed for parity-oracle tests. */
    void apply_scaling(sprite::SpriteScalingMode mode,
                       glm::vec2 texture_size,
                       glm::vec2& quad_size,
                       glm::vec2& quad_translation,
                       glm::vec4& uv_offset_scale);

    /** @brief Pack an affine transform into Bevy's transposed 3x4 sprite ABI. */
    SpriteInstance make_instance(const glm::mat4& transform, glm::vec4 color, glm::vec4 uv);
    }  // namespace detail

}  // namespace epix::sprite_render

template <>
struct epix::render::render_resource::RawBufferElementInfo<epix::sprite_render::SpriteInstance> {
    static constexpr bool has_no_uninit = true;
};

EPIX_EXPORT namespace epix::sprite_render {
    struct SpriteMeta {
        render::render_resource::RawBufferVec<std::uint32_t> sprite_index_buffer{wgpu::BufferUsage::eIndex};
        render::render_resource::RawBufferVec<SpriteInstance> sprite_instance_buffer{wgpu::BufferUsage::eVertex};
    };

    struct SpriteViewBindGroup {
        wgpu::BindGroup value;
    };

    struct SpriteBatch {
        assets::AssetId<image::Image> image_handle_id;
        std::pair<std::uint32_t, std::uint32_t> range{0, 0};
    };

    struct SpriteBatchKey {
        render::view::RetainedViewEntity view;
        ecs::Entity entity;
        bool operator==(const SpriteBatchKey&) const = default;
    };

    struct SpriteBatchKeyHash {
        std::size_t operator()(const SpriteBatchKey& key) const noexcept {
            std::size_t result = std::hash<render::view::RetainedViewEntity>{}(key.view);
            result ^= std::hash<ecs::Entity>{}(key.entity) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
            return result;
        }
    };

    struct SpriteBatches {
        std::unordered_map<SpriteBatchKey, SpriteBatch, SpriteBatchKeyHash> values;
    };

    struct ImageBindGroups {
        std::unordered_map<assets::AssetId<image::Image>, wgpu::BindGroup> values;
    };

    namespace detail {
    using PrepareSpriteImage = std::function<std::optional<glm::vec2>(assets::AssetId<image::Image>)>;

    SpritePipelineKey view_pipeline_key(bool hdr,
                                        std::uint32_t msaa_samples,
                                        std::optional<core_graph::Tonemapping> tonemapping,
                                        std::optional<core_graph::DebandDither> dither);

    void prepare_sprite_batches(SpriteMeta& sprite_meta,
                                const ExtractedSprites& extracted_sprites,
                                const ExtractedSlices& extracted_slices,
                                render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>& phases,
                                SpriteBatches& batches,
                                const PrepareSpriteImage& prepare_image);

    bool prepare_sprite_indices(SpriteMeta& sprite_meta);
    }  // namespace detail

    template <std::size_t Slot>
    struct SetSpriteViewBindGroup {
        template <render::phase::PhaseItem PhaseItem>
        struct Command {
            void prepare(const ecs::World&) noexcept {}

            std::expected<void, render::phase::RenderCommandError> render(
                const PhaseItem&,
                ecs::Item<const render::view::ViewUniformOffset&, const SpriteViewBindGroup&> view,
                std::optional<ecs::Item<>>,
                std::tuple<>,
                const wgpu::RenderPassEncoder& encoder) {
                const auto& offset     = std::get<0>(view);
                const auto& bind_group = std::get<1>(view);
                encoder.setBindGroup(Slot, bind_group.value, offset.offset);
                return {};
            }
        };
    };

    template <std::size_t Slot>
    struct SetSpriteTextureBindGroup {
        template <render::phase::PhaseItem PhaseItem>
        struct Command {
            void prepare(const ecs::World&) noexcept {}

            std::expected<void, render::phase::RenderCommandError> render(
                const PhaseItem& item,
                ecs::Item<const render::view::ExtractedView&> view_item,
                std::optional<ecs::Item<>>,
                std::tuple<ecs::Res<ImageBindGroups>, ecs::Res<SpriteBatches>> params,
                const wgpu::RenderPassEncoder& encoder) {
                const auto& view                    = std::get<0>(view_item);
                auto&& [image_bind_groups, batches] = params;
                const auto batch = batches->values.find(SpriteBatchKey{view.retained_view_entity, item.entity()});
                if (batch == batches->values.end()) {
                    return std::unexpected(render::phase::RenderCommandError{
                        .type    = render::phase::RenderCommandError::Type::Skip,
                        .message = "sprite item has no prepared batch",
                    });
                }
                const auto bind_group = image_bind_groups->values.find(batch->second.image_handle_id);
                if (bind_group == image_bind_groups->values.end()) {
                    return std::unexpected(render::phase::RenderCommandError{
                        .type    = render::phase::RenderCommandError::Type::Failure,
                        .message = "sprite batch has no prepared image bind group",
                    });
                }
                encoder.setBindGroup(Slot, bind_group->second, std::span<const std::uint32_t>{});
                return {};
            }
        };
    };

    template <render::phase::PhaseItem PhaseItem>
    struct DrawSpriteBatch {
        void prepare(const ecs::World&) noexcept {}

        std::expected<void, render::phase::RenderCommandError> render(
            const PhaseItem& item,
            ecs::Item<const render::view::ExtractedView&> view_item,
            std::optional<ecs::Item<>>,
            std::tuple<ecs::Res<SpriteMeta>, ecs::Res<SpriteBatches>> params,
            const wgpu::RenderPassEncoder& encoder) {
            const auto& view              = std::get<0>(view_item);
            auto&& [sprite_meta, batches] = params;
            const auto batch = batches->values.find(SpriteBatchKey{view.retained_view_entity, item.entity()});
            if (batch == batches->values.end()) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type    = render::phase::RenderCommandError::Type::Skip,
                    .message = "sprite item has no prepared batch",
                });
            }
            const auto* indices   = sprite_meta->sprite_index_buffer.buffer();
            const auto* instances = sprite_meta->sprite_instance_buffer.buffer();
            if (!indices || !instances) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type    = render::phase::RenderCommandError::Type::Failure,
                    .message = "sprite batch buffers are not prepared",
                });
            }
            encoder.setIndexBuffer(*indices, wgpu::IndexFormat::eUint32, 0, indices->getSize());
            encoder.setVertexBuffer(0, *instances, 0, instances->getSize());
            encoder.drawIndexed(6, batch->second.range.second - batch->second.range.first, 0, 0,
                                batch->second.range.first);
            return {};
        }
    };

    using DrawSprite =
        render::phase::RenderCommandSequence<core_graph::core_2d::Transparent2D, render::phase::SetItemPipeline,
                                             SetSpriteViewBindGroup<0>::Command, SetSpriteTextureBindGroup<1>::Command,
                                             DrawSpriteBatch>;

    struct SpriteRenderPlugin {
        void attach(app::App& app);
        void ready(app::App& app);
    };

    enum class SpriteSystems {
        ExtractSprites,
        ComputeSlices,
    };

    void compute_slices_on_asset_event(
        ecs::Commands commands,
        ecs::EventReader<assets::AssetEvent<image::Image>> events,
        ecs::Res<assets::Assets<image::Image>> images,
        ecs::Res<assets::Assets<image::TextureAtlasLayout>> atlas_layouts,
        ecs::Query<ecs::Item<ecs::Entity, const sprite::Sprite&>> sprites);

    void compute_slices_on_sprite_change(
        ecs::Commands commands,
        ecs::Res<assets::Assets<image::Image>> images,
        ecs::Res<assets::Assets<image::TextureAtlasLayout>> atlas_layouts,
        ecs::Query<ecs::Item<ecs::Entity, const sprite::Sprite&>, ecs::Filter<ecs::Modified<sprite::Sprite>>>
            changed_sprites);

    void extract_sprite_events(ecs::ResMut<SpriteAssetEvents> events,
                               app::Extract<ecs::EventReader<assets::AssetEvent<image::Image>>> image_events);

    void extract_sprites(
        ecs::ResMut<ExtractedSprites> extracted_sprites, ecs::ResMut<ExtractedSlices> extracted_slices,
        app::Extract<ecs::Query<ecs::Item<ecs::Entity, render::sync_world::RenderEntity, const camera::ViewVisibility&,
                                          const sprite::Sprite&, const transform::GlobalTransform&,
                                          const sprite::Anchor&, ecs::Opt<const ComputedTextureSlices&>>,
                                ecs::Filter<ecs::Without<render::CustomRendered>>>> sprites,
        app::Extract<ecs::Res<assets::Assets<image::TextureAtlasLayout>>> atlases);

    void queue_sprites_2d(
        ecs::Local<std::unordered_set<std::uint32_t>> view_entities,
        ecs::Query<ecs::Item<const render::view::RenderVisibleEntities&, const render::view::ExtractedView&,
                             const render::view::Msaa&, ecs::Opt<const core_graph::Tonemapping&>,
                             ecs::Opt<const core_graph::DebandDither&>>> views,
        ecs::Res<ExtractedSprites> extracted_sprites,
        ecs::Res<render::phase::DrawFunctions<core_graph::core_2d::Transparent2D>> draw_functions,
        ecs::Res<SpritePipeline> sprite_pipeline,
        ecs::ResMut<render::SpecializedRenderPipelines<SpritePipeline>> pipelines,
        ecs::Res<render::PipelineServer> pipeline_server,
        ecs::ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>> phases);

    void prepare_sprite_view_bind_groups(
        ecs::Commands commands, ecs::Res<wgpu::Device> device, ecs::Res<SpritePipeline> sprite_pipeline,
        ecs::Res<render::PipelineServer> pipeline_server,
        ecs::Res<render::view::ViewUniforms> view_uniforms,
        ecs::Query<ecs::Item<ecs::Entity, const core_graph::Tonemapping&>, ecs::With<render::view::ExtractedView>>
            views,
        ecs::Res<core_graph::TonemappingLuts> tonemapping_luts, ecs::Res<render::RenderAssets<image::Image>> images,
        ecs::Res<render::texture::FallbackImage> fallback_image);

    void prepare_sprite_image_bind_groups(
        ecs::Res<wgpu::Device> device, ecs::Res<wgpu::Queue> queue, ecs::ResMut<SpriteMeta> sprite_meta,
        ecs::Res<SpritePipeline> sprite_pipeline, ecs::ResMut<ImageBindGroups> image_bind_groups,
        ecs::Res<render::RenderAssets<image::Image>> gpu_images, ecs::Res<ExtractedSprites> extracted_sprites,
        ecs::Res<ExtractedSlices> extracted_slices,
        ecs::ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>> phases,
        ecs::Res<SpriteAssetEvents> events, ecs::ResMut<SpriteBatches> batches,
        ecs::Res<render::PipelineServer> pipeline_server);
}  // namespace epix::sprite_render

template <>
struct std::hash<epix::sprite_render::SpritePipelineKey> {
    std::size_t operator()(epix::sprite_render::SpritePipelineKey key) const noexcept {
        return std::hash<std::uint32_t>{}(key.bits());
    }
};
