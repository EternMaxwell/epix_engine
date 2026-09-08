#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/transform.hpp>
#include <expected>
#include <glm/glm.hpp>
#include <optional>
#include <variant>
#endif

#include <epix/sprite/texture_slice.hpp>

EPIX_EXPORT namespace epix::sprite {

/** @brief Proportional image scaling modes (Bevy `SpriteScalingMode`). */
enum class SpriteScalingMode {
    FillCenter,
    FillStart,
    FillEnd,
    FitCenter,
    FitStart,
    FitEnd,
};

struct SpriteImageModeAuto {
    bool operator==(const SpriteImageModeAuto&) const = default;
};
struct SpriteImageModeScale {
    SpriteScalingMode mode = SpriteScalingMode::FillCenter;
    bool operator==(const SpriteImageModeScale&) const = default;
};
struct SpriteImageModeSliced {
    TextureSlicer slicer;
    bool operator==(const SpriteImageModeSliced&) const = default;
};
struct SpriteImageModeTiled {
    bool tile_x         = true;
    bool tile_y         = true;
    float stretch_value = 1.0f;
    bool operator==(const SpriteImageModeTiled&) const = default;
};

/** @brief Controls how a sprite image is altered when scaled. */
struct SpriteImageMode
    : std::variant<SpriteImageModeAuto, SpriteImageModeScale, SpriteImageModeSliced, SpriteImageModeTiled> {
    using Auto   = SpriteImageModeAuto;
    using Scale  = SpriteImageModeScale;
    using Sliced = SpriteImageModeSliced;
    using Tiled  = SpriteImageModeTiled;
    using Base   = std::variant<Auto, Scale, Sliced, Tiled>;
    using Base::Base;

    SpriteImageMode() : Base(Auto{}) {}
    bool uses_slices() const noexcept {
        return std::holds_alternative<Sliced>(*this) || std::holds_alternative<Tiled>(*this);
    }
    std::optional<SpriteScalingMode> scale() const noexcept {
        if (const auto* value = std::get_if<Scale>(this)) return value->mode;
        return std::nullopt;
    }
};

/** @brief Normalized pivot offset of a 2D renderable from its Transform. */
struct Anchor {
    glm::vec2 value{0.0f};

    static const Anchor BOTTOM_LEFT;
    static const Anchor BOTTOM_CENTER;
    static const Anchor BOTTOM_RIGHT;
    static const Anchor CENTER_LEFT;
    static const Anchor CENTER;
    static const Anchor CENTER_RIGHT;
    static const Anchor TOP_LEFT;
    static const Anchor TOP_CENTER;
    static const Anchor TOP_RIGHT;

    constexpr Anchor() noexcept = default;
    constexpr Anchor(glm::vec2 value) noexcept : value(value) {}
    constexpr glm::vec2 as_vec() const noexcept { return value; }
    bool operator==(const Anchor&) const = default;
};

/** @brief Visual sprite component matching Bevy's CPU-side `Sprite`. */
struct Sprite {
    assets::Handle<image::Image> image{image::DEFAULT_IMAGE_HANDLE};
    std::optional<image::TextureAtlas> texture_atlas;
    glm::vec4 color{1.0f};
    bool flip_x = false;
    bool flip_y = false;
    std::optional<glm::vec2> custom_size;
    std::optional<image::Rect> rect;
    SpriteImageMode image_mode{};

    static Sprite sized(glm::vec2 custom_size);
    static Sprite from_image(assets::Handle<image::Image> image);
    static Sprite from_atlas_image(assets::Handle<image::Image> image, image::TextureAtlas atlas);
    static Sprite from_color(glm::vec4 color, glm::vec2 size);

    std::expected<glm::vec2, glm::vec2> compute_pixel_space_point(
        glm::vec2 point_relative_to_sprite,
        Anchor anchor,
        const assets::Assets<image::Image>& images,
        const assets::Assets<image::TextureAtlasLayout>& texture_atlases) const;
};

/** @brief Installs CPU-side sprite components and bounds maintenance. */
struct SpritePlugin {
    void attach(app::App& app);
};

/** @brief Insert or update local bounds for 2D mesh and sprite visibility. */
EPIX_EXPORT void calculate_bounds_2d(
    ecs::Commands commands,
    ecs::Res<assets::Assets<mesh::Mesh>> meshes,
    ecs::Res<assets::Assets<image::Image>> images,
    ecs::Res<assets::Assets<image::TextureAtlasLayout>> atlases,
    ecs::Query<ecs::Item<ecs::Entity, const mesh::Mesh2d&>,
               ecs::Filter<ecs::Without<camera::Aabb>,
                           ecs::Without<camera::NoFrustumCulling>,
                           ecs::Without<camera::NoAutoAabb>>> new_mesh_aabb,
    ecs::Query<ecs::Item<ecs::Ref<mesh::Mesh2d>, ecs::Mut<camera::Aabb>>,
               ecs::Filter<ecs::Or<assets::AssetChanged<mesh::Mesh2d>, ecs::Modified<mesh::Mesh2d>>,
                           ecs::Without<camera::NoFrustumCulling>,
                           ecs::Without<camera::NoAutoAabb>,
                           ecs::Without<Sprite>>> update_mesh_aabb,
    ecs::Query<ecs::Item<ecs::Entity, const Sprite&, const Anchor&>,
               ecs::Filter<ecs::Without<camera::Aabb>,
                           ecs::Without<camera::NoFrustumCulling>,
                           ecs::Without<camera::NoAutoAabb>>> new_sprite_aabb,
    ecs::Query<ecs::Item<const Sprite&, ecs::Mut<camera::Aabb>, const Anchor&>,
               ecs::Filter<ecs::Or<ecs::Modified<Sprite>, ecs::Modified<Anchor>>,
                           ecs::Without<camera::NoFrustumCulling>,
                           ecs::Without<camera::NoAutoAabb>,
                           ecs::Without<mesh::Mesh2d>>> update_sprite_aabb);

}  // namespace epix::sprite

namespace epix::assets {

template <>
struct AsAssetId<sprite::Sprite> {
    using Asset = image::Image;

    static AssetId<Asset> as_asset_id(const sprite::Sprite& sprite) noexcept { return sprite.image.id(); }
};
static_assert(AsAssetIdImpl<sprite::Sprite>);

}  // namespace epix::assets
