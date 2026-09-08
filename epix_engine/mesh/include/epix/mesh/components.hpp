#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#endif

#include <epix/mesh/mesh.hpp>

namespace epix::mesh {

/** @brief Component associating an entity with a mesh asset for 2D rendering
 * (Bevy `bevy_mesh::Mesh2d`). */
EPIX_EXPORT struct Mesh2d {
    assets::Handle<Mesh> handle;
};

}  // namespace epix::mesh

namespace epix::assets {

template <>
struct AsAssetId<mesh::Mesh2d> {
    using Asset = mesh::Mesh;

    static AssetId<Asset> as_asset_id(const mesh::Mesh2d& mesh) noexcept { return mesh.handle.id(); }
};
static_assert(AsAssetIdImpl<mesh::Mesh2d>);

}  // namespace epix::assets
