#include <epix/assets/id.hpp>
#include <epix/pbr/mesh_material.hpp>
#include <gtest/gtest.h>

#include <utility>

namespace pbr    = epix::pbr;
namespace assets = epix::assets;

namespace {
/** @brief A stand-in material type. `Handle<T>` is unconstrained, so the component
 *  can be exercised before the 3D `Material` counterpart exists - which is the
 *  point of porting it unconstrained. */
struct StandInMaterial {};
}  // namespace

/** @brief A 3D mesh's material handle (Bevy `MeshMaterial3d<M>`,
 *  `bevy_pbr/src/mesh_material.rs:41`).
 *
 *  No device is involved: it is a handle and a comparison. What matters is that the
 *  handle names an asset the change-detection machinery can read, which is what the
 *  `AsAssetId` specialization provides, and that two components compare by handle. */
TEST(MeshMaterial3dTest, NamesAndComparesTheMaterialAsset) {
    using pbr::MeshMaterial3d;

    // The default constructor is deleted with the handle's own - Bevy's `Default`
    // hands back a default `Handle`, which Epix's deleted one cannot, so that
    // counterpart is a divergence rather than an omission (the 2D sibling records
    // the same).
    const MeshMaterial3d<StandInMaterial> one{assets::Handle<StandInMaterial>{assets::AssetId<StandInMaterial>::invalid()}};
    const MeshMaterial3d<StandInMaterial> two{assets::Handle<StandInMaterial>{assets::AssetId<StandInMaterial>::invalid()}};

    EXPECT_EQ(one.id(), assets::AssetId<StandInMaterial>::invalid()) << "the handle is what the component is about";
    EXPECT_TRUE(one == two) << "two handles to the same asset are the same material";
    EXPECT_EQ(assets::AsAssetId<MeshMaterial3d<StandInMaterial>>::as_asset_id(one), one.id())
        << "which is how AssetChanged<MeshMaterial3d<M>> observes a material swap";
}
