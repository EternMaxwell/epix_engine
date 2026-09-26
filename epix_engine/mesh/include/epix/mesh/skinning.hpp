#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#include <span>
#include <utility>
#include <vector>
#endif

namespace epix::mesh {

/** @brief Inverse bind-pose matrices used by a skinned mesh (Bevy
 * `SkinnedMeshInverseBindposes`). */
EPIX_EXPORT class SkinnedMeshInverseBindposes {
   public:
    explicit SkinnedMeshInverseBindposes(std::vector<glm::mat4> matrices) : matrices_(std::move(matrices)) {}

    std::span<const glm::mat4> matrices() const noexcept { return matrices_; }
    auto begin() const noexcept { return matrices_.begin(); }
    auto end() const noexcept { return matrices_.end(); }
    const glm::mat4& operator[](std::size_t index) const noexcept { return matrices_[index]; }
    std::size_t size() const noexcept { return matrices_.size(); }
    bool empty() const noexcept { return matrices_.empty(); }

   private:
    std::vector<glm::mat4> matrices_;
};

/** @brief Component associating a mesh with its inverse bind poses and joint entities (Bevy
 * `SkinnedMesh`). */
EPIX_EXPORT struct SkinnedMesh {
    assets::Handle<SkinnedMeshInverseBindposes> inverse_bindposes{
        assets::AssetId<SkinnedMeshInverseBindposes>::invalid()};
    std::vector<ecs::Entity> joints;

    bool operator==(const SkinnedMesh&) const = default;
};

}  // namespace epix::mesh

namespace epix::assets {

template <>
struct AsAssetId<mesh::SkinnedMesh> {
    using Asset = mesh::SkinnedMeshInverseBindposes;

    static AssetId<Asset> as_asset_id(const mesh::SkinnedMesh& mesh) noexcept { return mesh.inverse_bindposes.id(); }
};
static_assert(AsAssetIdImpl<mesh::SkinnedMesh>);

}  // namespace epix::assets
