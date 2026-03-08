module;

export module epix.sprite:sprite;

import epix.assets;
import epix.core;
import epix.image;
import epix.transform;
import glm;
import std;

export namespace sprite {
struct Sprite {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    bool flip_x = false;
    bool flip_y = false;
    std::optional<glm::vec4> uv_rect;
    std::optional<glm::vec2> size;
    glm::vec2 anchor{0.0f, 0.0f};
};

struct SpriteBundle {
    Sprite sprite{};
    transform::Transform transform{};
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