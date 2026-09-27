#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/assets/asset_changed.hpp>
#include <epix/assets/handle.hpp>
#include <epix/common.hpp>
#include <utility>
#endif

namespace epix::pbr {

/** @brief A 3D mesh's material handle (Bevy `MeshMaterial3d<M>`,
 *  `bevy_pbr/src/mesh_material.rs:41`).
 *
 *  Bevy's tuple struct is constrained on `M: Material`; Epix's 3D `Material`
 *  counterpart does not exist yet, so the constraint is absent here and the type is
 *  usable the moment it lands. Its 2D sibling (`MeshMaterial2d<M>`,
 *  `sprite_render/material2d.hpp`) shows the shape it takes then: a CRTP base the
 *  material derives from, which is how the port expresses Bevy's associated-type
 *  trait.
 *
 *  The handle member stands for Bevy's tuple field `.0` - a tuple struct has no
 *  field name to keep, and the 2D sibling made the same choice. */
EPIX_EXPORT template <typename M>
struct MeshMaterial3d {
    /** @brief Handle to the material asset. */
    assets::Handle<M> handle;

    MeshMaterial3d() = default;
    explicit MeshMaterial3d(assets::Handle<M> material) : handle(std::move(material)) {}

    /** @brief The material's asset id (Bevy `MeshMaterial3d<M>::id`). */
    assets::AssetId<M> id() const noexcept { return handle.id(); }

    bool operator==(const MeshMaterial3d&) const noexcept = default;
};

}  // namespace epix::pbr

namespace epix::assets {
/** @brief A `MeshMaterial3d<M>` names the material asset it holds, which is what
 *  lets `AssetChanged<MeshMaterial3d<M>>` observe material swaps (Bevy's
 *  `impl AsAssetId for MeshMaterial3d<M>`).
 *
 *  The 2D sibling carries the same specialization for the same reason. */
template <typename M>
struct AsAssetId<pbr::MeshMaterial3d<M>> {
    using Asset = M;

    static AssetId<Asset> as_asset_id(const pbr::MeshMaterial3d<M>& material) noexcept { return material.id(); }
};
}  // namespace epix::assets
