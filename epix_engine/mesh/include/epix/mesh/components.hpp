#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/assets/asset_changed.hpp>
#include <epix/camera/visibility.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/transform.hpp>
#include <utility>
#endif

#include <epix/mesh/mesh.hpp>

namespace epix::mesh {

/** @brief Component associating an entity with a mesh asset for 2D rendering
 * (Bevy `bevy_mesh::Mesh2d`). */
EPIX_EXPORT struct Mesh2d {
    assets::Handle<Mesh> handle{assets::AssetId<Mesh>::invalid()};

    Mesh2d() = default;
    explicit Mesh2d(assets::Handle<Mesh> handle) : handle(std::move(handle)) {}

    const assets::Handle<Mesh>& operator*() const noexcept { return handle; }
    assets::Handle<Mesh>& operator*() noexcept { return handle; }
    const assets::Handle<Mesh>* operator->() const noexcept { return &handle; }
    assets::Handle<Mesh>* operator->() noexcept { return &handle; }
    operator assets::AssetId<Mesh>() const noexcept { return handle.id(); }

    bool operator==(const Mesh2d&) const = default;

    /** @brief Append the Mesh2d visibility class (Bevy
     *  `#[component(on_add = visibility::add_visibility_class::<Mesh2d>)]`,
     *  `bevy_camera/src/visibility/mod.rs:433-435`).
     *
     *  Appended rather than supplied only when absent, so an entity already in another
     *  class - a `Sprite` that also carries a `Mesh2d`, say - ends up in both. Required
     *  components are skipped when the archetype already has one (`bundle.cpp:91-95`),
     *  which is why the class comes from this hook rather than from the registration
     *  below (CAM-20). */
    static void on_add(ecs::World& world, ecs::HookContext context) {
        camera::add_visibility_class<Mesh2d>(world, context);
    }

    static void register_required_components(ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<transform::Transform>([] { return transform::Transform{}; });
    }
};

/** @brief Component associating an entity with a mesh asset for 3D rendering
 * (Bevy `bevy_mesh::Mesh3d`). */
EPIX_EXPORT struct Mesh3d {
    assets::Handle<Mesh> handle{assets::AssetId<Mesh>::invalid()};

    Mesh3d() = default;
    explicit Mesh3d(assets::Handle<Mesh> handle) : handle(std::move(handle)) {}

    const assets::Handle<Mesh>& operator*() const noexcept { return handle; }
    assets::Handle<Mesh>& operator*() noexcept { return handle; }
    const assets::Handle<Mesh>* operator->() const noexcept { return &handle; }
    assets::Handle<Mesh>* operator->() noexcept { return &handle; }
    operator assets::AssetId<Mesh>() const noexcept { return handle.id(); }

    bool operator==(const Mesh3d&) const = default;

    /** @brief Append the Mesh3d visibility class (Bevy
     *  `#[component(on_add = visibility::add_visibility_class::<Mesh3d>)]`,
     *  `bevy_camera/src/visibility/mod.rs:430-435`).
     *
     *  Appended rather than supplied only when absent, so an entity that is already in
     *  another class - a `Mesh3d` that also carries a `Sprite`, say - ends up in both.
     *  The class-less form is what `Mesh2d` still uses, recorded as its own row. */
    static void on_add(ecs::World& world, ecs::HookContext context) {
        camera::add_visibility_class<Mesh3d>(world, context);
    }

    static void register_required_components(ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<transform::Transform>([] { return transform::Transform{}; });
    }
};

/** @brief Give every `Mesh3d` an `Aabb` and keep it current (Bevy `calculate_bounds`,
 *  `bevy_camera/src/visibility/mod.rs:451-486`).
 *
 *  Declared here rather than in the camera module for the layering reason the definition
 *  states: camera cannot see `Mesh3d`, while mesh already sees camera. Registered by
 *  `MeshPlugin` into `VisibilitySystems::CalculateBounds`, the set `VisibilityPlugin`
 *  configures and which was previously left empty (`CAM-21`). */
EPIX_EXPORT EPIX_API void calculate_bounds(
    ecs::Commands commands, ecs::Res<assets::Assets<Mesh>> meshes,
    ecs::Query<ecs::Item<ecs::Entity, const Mesh3d&>,
               ecs::Filter<ecs::Without<camera::Aabb>, ecs::Without<camera::NoFrustumCulling>,
                           ecs::Without<camera::NoAutoAabb>>>
        new_mesh_aabb,
    ecs::Query<ecs::Item<ecs::Ref<Mesh3d>, ecs::Mut<camera::Aabb>>,
               ecs::Filter<ecs::Modified<Mesh3d>,
                           ecs::Without<camera::NoFrustumCulling>, ecs::Without<camera::NoAutoAabb>>>
        update_mesh_aabb);
/** @brief Arbitrary index used to identify a mesh instance while rendering
 * (Bevy `bevy_mesh::MeshTag`). */
EPIX_EXPORT struct MeshTag {
    std::uint32_t value = 0;

    bool operator==(const MeshTag&) const = default;
};

/** @brief Mark `Mesh3d` components changed when their referenced mesh asset is modified. */
EPIX_EXPORT EPIX_API void mark_3d_meshes_as_changed_if_their_assets_changed(
    ecs::Query<ecs::Item<ecs::Mut<Mesh3d>>> meshes_3d, ecs::EventReader<assets::AssetEvent<Mesh>> mesh_asset_events);

}  // namespace epix::mesh

namespace epix::assets {

template <>
struct AsAssetId<mesh::Mesh2d> {
    using Asset = mesh::Mesh;

    static AssetId<Asset> as_asset_id(const mesh::Mesh2d& mesh) noexcept { return mesh.handle.id(); }
};
static_assert(AsAssetIdImpl<mesh::Mesh2d>);

template <>
struct AsAssetId<mesh::Mesh3d> {
    using Asset = mesh::Mesh;

    static AssetId<Asset> as_asset_id(const mesh::Mesh3d& mesh) noexcept { return mesh.handle.id(); }
};
static_assert(AsAssetIdImpl<mesh::Mesh3d>);

}  // namespace epix::assets
