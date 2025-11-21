#pragma once

#include "epix/render/assets.hpp"
#include "gpumesh.hpp"
#include "mesh.hpp"
#include "nvrhi/nvrhi.h"

template <>
struct epix::render::assets::RenderAsset<epix::mesh::Mesh> {
    using ProcessedAsset = epix::mesh::GPUMesh;
    using Param          = epix::Res<nvrhi::DeviceHandle>;

    ProcessedAsset process(const epix::mesh::Mesh& mesh, Param) {
        return ProcessedAsset::create_from_mesh(mesh, nullptr);
    }

    epix::render::assets::RenderAssetUsage usage(const epix::mesh::Mesh& mesh) {
        return epix::render::assets::RenderAssetUsageBits::RENDER_WORLD;
    }
};
static_assert(epix::render::assets::RenderAssetImpl<epix::mesh::Mesh>);