module;

export module epix.sprite:sprite;

import epix.assets;
import epix.core;
import epix.image;
import epix.transform;
import glm;
import std;

export namespace sprite {
/** @brief Visual sprite component with color, flipping, UV region, and anchor
 * settings.
 *
 * Attach to an entity along with a texture handle to render a 2D image. When
 * `uv_rect` is unset the full texture is used; when `size` is unset the
 * texture's native size is used.
 */
struct Sprite {
    /** @brief Tint color multiplied with the texture. Defaults to opaque
     * white. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Whether to mirror the sprite horizontally. */
    bool flip_x = false;
    /** @brief Whether to mirror the sprite vertically. */
    bool flip_y = false;
    /** @brief Optional UV sub-rectangle (x, y, width, height) within the
     * texture. */
    std::optional<glm::vec4> uv_rect;
    /** @brief Optional override for the sprite's display size in world
     * units. */
    std::optional<glm::vec2> size;
    /** @brief Anchor point offset from the sprite center. (0,0) is center. */
    glm::vec2 anchor{0.0f, 0.0f};
};

/** @brief Convenience bundle that groups a Sprite, Transform, and texture
 * Handle for spawning a complete sprite entity. */
struct SpriteBundle {
    /** @brief The sprite visual properties. */
    Sprite sprite{};
    /** @brief The transform positioning the sprite in world space. */
    transform::Transform transform{};
    /** @brief Handle to the Image asset used as the sprite texture. */
    assets::Handle<image::Image> texture;
};
}  // namespace sprite

template <>
struct core::Bundle<sprite::SpriteBundle> {
    static std::size_t write(sprite::SpriteBundle& bundle, std::span<void*> dests) {
        new (dests[0]) sprite::Sprite(std::move(bundle.sprite));
        new (dests[1]) transform::Transform(std::move(bundle.transform));
        new (dests[2]) assets::Handle<image::Image>(std::move(bundle.texture));
        return 3;
    }

    static std::array<TypeId, 3> type_ids(const TypeRegistry& registry) {
        return std::array{registry.type_id<sprite::Sprite>(), registry.type_id<transform::Transform>(),
                          registry.type_id<assets::Handle<image::Image>>()};
    }

    static void register_components(const TypeRegistry&, Components& components) {
        components.register_info<sprite::Sprite>();
        components.register_info<transform::Transform>();
        components.register_info<assets::Handle<image::Image>>();
    }
};

static_assert(core::is_bundle<sprite::SpriteBundle>);