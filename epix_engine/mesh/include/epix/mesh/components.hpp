#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/assets.hpp>
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

    static void register_required_components(ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<transform::Transform>([] { return transform::Transform{}; });
    }
};

/** @brief Arbitrary index used to identify a mesh instance while rendering
 * (Bevy `bevy_mesh::MeshTag`). */
EPIX_EXPORT struct MeshTag {
    std::uint32_t value = 0;

    bool operator==(const MeshTag&) const = default;
};

/** @brief Mark `Mesh3d` components changed when their referenced mesh asset is modified. */
EPIX_EXPORT void mark_3d_meshes_as_changed_if_their_assets_changed(
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
