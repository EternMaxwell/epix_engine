#include <epix/sprite.hpp>

#include <spdlog/spdlog.h>

#include <utility>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

const sprite::Anchor sprite::Anchor::BOTTOM_LEFT{glm::vec2(-0.5f, -0.5f)};
const sprite::Anchor sprite::Anchor::BOTTOM_CENTER{glm::vec2(0.0f, -0.5f)};
const sprite::Anchor sprite::Anchor::BOTTOM_RIGHT{glm::vec2(0.5f, -0.5f)};
const sprite::Anchor sprite::Anchor::CENTER_LEFT{glm::vec2(-0.5f, 0.0f)};
const sprite::Anchor sprite::Anchor::CENTER{glm::vec2(0.0f)};
const sprite::Anchor sprite::Anchor::CENTER_RIGHT{glm::vec2(0.5f, 0.0f)};
const sprite::Anchor sprite::Anchor::TOP_LEFT{glm::vec2(-0.5f, 0.5f)};
const sprite::Anchor sprite::Anchor::TOP_CENTER{glm::vec2(0.0f, 0.5f)};
const sprite::Anchor sprite::Anchor::TOP_RIGHT{glm::vec2(0.5f, 0.5f)};

sprite::Sprite sprite::Sprite::sized(glm::vec2 size) {
    Sprite sprite;
    sprite.custom_size = size;
    return sprite;
}

sprite::Sprite sprite::Sprite::from_image(assets::Handle<image::Image> handle) {
    Sprite sprite;
    sprite.image = std::move(handle);
    return sprite;
}

sprite::Sprite sprite::Sprite::from_atlas_image(assets::Handle<image::Image> handle, image::TextureAtlas atlas) {
    Sprite sprite;
    sprite.image         = std::move(handle);
    sprite.texture_atlas = std::move(atlas);
    return sprite;
}

sprite::Sprite sprite::Sprite::from_color(glm::vec4 tint, glm::vec2 size) {
    Sprite sprite;
    sprite.color       = tint;
    sprite.custom_size = size;
    return sprite;
}

std::expected<glm::vec2, glm::vec2> sprite::Sprite::compute_pixel_space_point(
    glm::vec2 point_relative_to_sprite,
    Anchor anchor,
    const assets::Assets<image::Image>& images,
    const assets::Assets<image::TextureAtlasLayout>& texture_atlases) const {
    auto image_size = glm::uvec2(1u);
    if (const auto source = images.get(image.id())) {
        image_size = glm::uvec2(source->get().width(), source->get().height());
    }

    auto atlas_rect = texture_atlas.and_then(
        [&](const image::TextureAtlas& atlas) { return atlas.texture_rect(texture_atlases); });
    image::Rect texture_rect;
    if (atlas_rect) {
        texture_rect = atlas_rect->as_rect();
        if (rect) {
            texture_rect = image::Rect{rect->min + texture_rect.min, rect->max + texture_rect.min};
        }
    } else if (rect) {
        texture_rect = *rect;
    } else {
        texture_rect = image::Rect{glm::vec2(0.0f), glm::vec2(image_size)};
    }

    const auto sprite_size   = custom_size.value_or(texture_rect.size());
    const auto sprite_center = -anchor.as_vec() * sprite_size;
    auto relative_to_center  = point_relative_to_sprite - sprite_center;
    if (flip_x) relative_to_center.x *= -1.0f;
    if (!flip_y) relative_to_center.y *= -1.0f;
    if (sprite_size.x == 0.0f || sprite_size.y == 0.0f) {
        return std::unexpected(relative_to_center);
    }

    const auto texture_ratio = texture_rect.size() / sprite_size;
    const auto pixel         = relative_to_center * texture_ratio + texture_rect.center();
    // Bevy 0.18 also leaves SpriteImageMode support as a TODO here.
    if (texture_rect.contains(pixel)) return pixel;
    return std::unexpected(pixel);
}

namespace {
std::optional<glm::vec2> sprite_size(const sprite::Sprite& sprite,
                                     const assets::Assets<image::Image>& images,
                                     const assets::Assets<image::TextureAtlasLayout>& atlases) {
    if (sprite.custom_size) return sprite.custom_size;
    if (sprite.rect) return sprite.rect->size();
    if (sprite.texture_atlas) {
        return sprite.texture_atlas->texture_rect(atlases).transform(
            [](const image::URect& rect) { return glm::vec2(rect.size()); });
    }
    return images.get(sprite.image.id()).transform([](const auto& source) {
        return glm::vec2(static_cast<float>(source.get().width()), static_cast<float>(source.get().height()));
    });
}

camera::Aabb sprite_aabb(glm::vec2 size, const sprite::Anchor& anchor) {
    return camera::Aabb{
        .center       = glm::vec3(-anchor.as_vec() * size, 0.0f),
        .half_extents = glm::vec3(size * 0.5f, 0.0f),
    };
}
}  // namespace

void sprite::calculate_bounds_2d(
    Commands commands,
    Res<assets::Assets<mesh::Mesh>> meshes,
    Res<assets::Assets<image::Image>> images,
    Res<assets::Assets<image::TextureAtlasLayout>> atlases,
    Query<Item<Entity, const mesh::Mesh2d&>,
          Filter<Without<camera::Aabb>, Without<camera::NoFrustumCulling>, Without<camera::NoAutoAabb>>>
        new_mesh_aabb,
    Query<Item<Ref<mesh::Mesh2d>, Mut<camera::Aabb>>,
          Filter<Or<assets::AssetChanged<mesh::Mesh2d>, Modified<mesh::Mesh2d>>,
                 Without<camera::NoFrustumCulling>,
                 Without<camera::NoAutoAabb>,
                 Without<Sprite>>>
        update_mesh_aabb,
    Query<Item<Entity, const Sprite&, const Anchor&>,
          Filter<Without<camera::Aabb>, Without<camera::NoFrustumCulling>, Without<camera::NoAutoAabb>>>
        new_sprite_aabb,
    Query<Item<const Sprite&, Mut<camera::Aabb>, const Anchor&>,
          Filter<Or<Modified<Sprite>, Modified<Anchor>>,
                 Without<camera::NoFrustumCulling>,
                 Without<camera::NoAutoAabb>,
                 Without<mesh::Mesh2d>>>
        update_sprite_aabb) {
    for (auto&& [entity, mesh2d] : new_mesh_aabb.iter()) {
        if (const auto mesh_asset = meshes->get(mesh2d.handle.id())) {
            if (const auto aabb = camera::MeshAabb<mesh::Mesh>::compute_aabb(mesh_asset->get())) {
                commands.entity(entity).insert_if_new(*aabb);
            }
        }
    }

    for (auto&& [mesh2d, old_aabb] : update_mesh_aabb.iter()) {
        if (const auto mesh_asset = meshes->get(mesh2d->handle.id())) {
            if (const auto aabb = camera::MeshAabb<mesh::Mesh>::compute_aabb(mesh_asset->get()); aabb) {
                const auto& old = old_aabb.get();
                if (glm::any(glm::notEqual(old.center, aabb->center)) ||
                    glm::any(glm::notEqual(old.half_extents, aabb->half_extents))) {
                    old_aabb.get_mut() = *aabb;
                }
            }
        }
    }

    for (auto&& [entity, value, anchor] : new_sprite_aabb.iter()) {
        if (const auto size = sprite_size(value, *images, *atlases)) {
            commands.entity(entity).insert_if_new(sprite_aabb(*size, anchor));
        }
    }

    for (auto&& [value, old_aabb, anchor] : update_sprite_aabb.iter()) {
        if (const auto size = sprite_size(value, *images, *atlases)) {
            const auto next = sprite_aabb(*size, anchor);
            const auto& old = old_aabb.get();
            if (glm::any(glm::notEqual(old.center, next.center)) ||
                glm::any(glm::notEqual(old.half_extents, next.half_extents))) {
                old_aabb.get_mut() = next;
            }
        }
    }
}

void sprite::SpritePlugin::attach(App& app) {
    spdlog::debug("[sprite] Attaching SpritePlugin.");
    if (!app.get_plugin<image::TextureAtlasPlugin>()) {
        app.add_plugins(image::TextureAtlasPlugin{});
    }
    app.world_mut().register_required_components<sprite::Sprite, transform::Transform>();
    app.world_mut().register_required_components<sprite::Sprite, camera::Visibility>();
    app.world_mut().register_required_components<sprite::Sprite, sprite::Anchor>();
    app.world_mut().register_required_components_with<sprite::Sprite>(
        [] { return camera::VisibilityClass{meta::type_index(meta::type_id<sprite::Sprite>())}; });
    app.add_systems(app::PostUpdate, into(sprite::calculate_bounds_2d)
                                         .in_set(camera::VisibilitySystems::CalculateBounds)
                                         .before(camera::VisibilitySystems::CheckVisibility)
                                         .set_name("calculate bounds 2d"));
}
