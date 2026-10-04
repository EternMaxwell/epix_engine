#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/color.hpp>
#include <epix/common.hpp>
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
    /** @brief C++ counterpart to `SpriteImageMode::Scale(SpriteScalingMode)`.
     *
     *  Bevy's variant carries the mode as a required payload and gives it no default, so
     *  the member has no initialiser and every construction site supplies the value
     *  (SPR-5). The struct stays an aggregate, which is what lets call sites spell the
     *  payload by field name the way Rust's variant literals do. */
    struct SpriteImageModeScale {
        SpriteScalingMode mode;
        bool operator==(const SpriteImageModeScale&) const = default;
    };
    struct SpriteImageModeSliced {
        TextureSlicer slicer;
        bool operator==(const SpriteImageModeSliced&) const = default;
    };
    /** @brief C++ counterpart to `SpriteImageMode::Tiled { .. }`.
     *
     *  All three values are supplied, as they are in Bevy, which gives this variant's
     *  fields no defaults (SPR-5). */
    struct SpriteImageModeTiled {
        bool tile_x;
        bool tile_y;
        float stretch_value;
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
    struct EPIX_API Anchor {
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
    struct EPIX_API Sprite {
        assets::Handle<image::Image> image{image::DEFAULT_IMAGE_HANDLE};
        std::optional<image::TextureAtlas> texture_atlas;
        ::epix::color::Color color = ::epix::color::Color::WHITE;
        bool flip_x = false;
        bool flip_y = false;
        std::optional<glm::vec2> custom_size;
        std::optional<image::Rect> rect;
        SpriteImageMode image_mode{};

        static Sprite sized(glm::vec2 custom_size);
        static Sprite from_image(assets::Handle<image::Image> image);
        static Sprite from_atlas_image(assets::Handle<image::Image> image, image::TextureAtlas atlas);
        static Sprite from_color(::epix::color::Color color, glm::vec2 size);

        /** @brief Appends `Sprite` to this entity's `VisibilityClass`, keeping any
         * class already there (Bevy
         * `#[component(on_add = visibility::add_visibility_class::<Sprite>)]`,
         * `bevy_sprite/src/sprite.rs:17`). */
        static void on_add(ecs::World& world, ecs::HookContext context);
        /** @brief Bevy's `#[require(Transform, Visibility, VisibilityClass, Anchor)]`
         * (`bevy_sprite/src/sprite.rs:15`). */
        static void register_required_components(ecs::RequiredComponentsRegistrator& registrator);

        std::expected<glm::vec2, glm::vec2> compute_pixel_space_point(
            glm::vec2 point_relative_to_sprite,
            Anchor anchor,
            const assets::Assets<image::Image>& images,
            const assets::Assets<image::TextureAtlasLayout>& texture_atlases) const;
    };

    /** @brief Installs CPU-side sprite components and bounds maintenance. */
    struct EPIX_API SpritePlugin {
        void attach(app::App& app);
    };

    /** @brief Insert or update local bounds for 2D mesh and sprite visibility. */
    EPIX_EXPORT EPIX_API void calculate_bounds_2d(
        ecs::Commands commands, ecs::Res<assets::Assets<mesh::Mesh>> meshes,
        ecs::Res<assets::Assets<image::Image>> images, ecs::Res<assets::Assets<image::TextureAtlasLayout>> atlases,
        ecs::Query<ecs::Item<ecs::Entity, const mesh::Mesh2d&>,
                   ecs::Filter<ecs::Without<camera::Aabb>, ecs::Without<camera::NoFrustumCulling>,
                               ecs::Without<camera::NoAutoAabb>>> new_mesh_aabb,
        ecs::Query<ecs::Item<ecs::Ref<mesh::Mesh2d>, ecs::Mut<camera::Aabb>>,
                   ecs::Filter<ecs::Or<assets::AssetChanged<mesh::Mesh2d>, ecs::Modified<mesh::Mesh2d>>,
                               ecs::Without<camera::NoFrustumCulling>, ecs::Without<camera::NoAutoAabb>,
                               ecs::Without<Sprite>>> update_mesh_aabb,
        ecs::Query<ecs::Item<ecs::Entity, const Sprite&, const Anchor&>,
                   ecs::Filter<ecs::Without<camera::Aabb>, ecs::Without<camera::NoFrustumCulling>,
                               ecs::Without<camera::NoAutoAabb>>> new_sprite_aabb,
        ecs::Query<
            ecs::Item<const Sprite&, ecs::Mut<camera::Aabb>, const Anchor&>,
            ecs::Filter<ecs::Or<ecs::Modified<Sprite>, ecs::Modified<Anchor>>, ecs::Without<camera::NoFrustumCulling>,
                        ecs::Without<camera::NoAutoAabb>, ecs::Without<mesh::Mesh2d>>> update_sprite_aabb);

}  // namespace epix::sprite

namespace epix::assets {

template <>
struct AsAssetId<sprite::Sprite> {
    using Asset = image::Image;

    static AssetId<Asset> as_asset_id(const sprite::Sprite& sprite) noexcept { return sprite.image.id(); }
};
static_assert(AsAssetIdImpl<sprite::Sprite>);

}  // namespace epix::assets
