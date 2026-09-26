#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <epix/mesh.hpp>
#include <optional>
#endif

#include <epix/camera/visibility.hpp>

template <>
struct EPIX_API epix::camera::MeshAabb<epix::mesh::Mesh> {
    static std::optional<epix::camera::Aabb> compute_aabb(const epix::mesh::Mesh& mesh);
};

static_assert(epix::camera::MeshAabbImpl<epix::mesh::Mesh>);
