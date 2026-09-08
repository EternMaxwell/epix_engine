#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#endif

EPIX_EXPORT namespace epix::sprite {
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

    /** @brief Installs CPU-side sprite components and bounds maintenance
     * (Bevy `bevy_sprite::SpritePlugin`). */
    struct SpritePlugin {
        void attach(app::App& app);
    };

    /** @brief Insert or update local mesh bounds for 2D visibility culling
     * (the mesh portion of Bevy `bevy_sprite::calculate_bounds_2d`). */
    EPIX_EXPORT void calculate_bounds_2d(
        ecs::Commands commands,
        ecs::Res<assets::Assets<mesh::Mesh>> meshes,
        ecs::Query<ecs::Item<ecs::Entity, const mesh::Mesh2d&>,
                   ecs::Filter<ecs::Without<camera::Aabb>,
                               ecs::Without<camera::NoFrustumCulling>,
                               ecs::Without<camera::NoAutoAabb>>> new_mesh_aabb,
        ecs::Query<ecs::Item<ecs::Ref<mesh::Mesh2d>, ecs::Mut<camera::Aabb>>,
                   ecs::Filter<ecs::Or<assets::AssetChanged<mesh::Mesh2d>, ecs::Modified<mesh::Mesh2d>>,
                               ecs::Without<camera::NoFrustumCulling>,
                               ecs::Without<camera::NoAutoAabb>>> update_mesh_aabb);
}  // namespace epix::sprite

template <>
struct epix::ecs::Bundle<epix::sprite::SpriteBundle> {
    static void get_components(sprite::SpriteBundle& bundle,
                               std::invocable<utils::function_ref<void(void*)>> auto&& write_component) noexcept {
        write_component([&](void* ptr) { new (ptr) sprite::Sprite(std::move(bundle.sprite)); });
        write_component([&](void* ptr) { new (ptr) transform::Transform(std::move(bundle.transform)); });
        write_component([&](void* ptr) { new (ptr) assets::Handle<image::Image>(std::move(bundle.texture)); });
    }

    static std::array<std::optional<TypeId>, 3> type_ids(const ecs::Components& components) {
        return std::array{
            components.get_id<sprite::Sprite>(),
            components.get_id<transform::Transform>(),
            components.get_id<assets::Handle<image::Image>>(),
        };
    }

    static std::vector<TypeId> register_components(ecs::ComponentsRegistrator& components) {
        std::vector<TypeId> ids;
        ids.push_back(components.template register_component<sprite::Sprite>());
        ids.push_back(components.template register_component<transform::Transform>());
        ids.push_back(components.template register_component<assets::Handle<image::Image>>());
        return ids;
    }
};

static_assert(epix::ecs::is_bundle<epix::sprite::SpriteBundle>);
