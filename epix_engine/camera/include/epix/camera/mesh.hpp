#pragma once


#ifndef EPIX_CXX_MODULE
#include <epix/mesh.hpp>
#include <optional>
#include <epix/common.hpp>
#endif

#include <epix/camera/visibility.hpp>

template <>
struct epix::camera::MeshAabb<epix::mesh::Mesh> {
    static std::optional<epix::camera::Aabb> compute_aabb(const epix::mesh::Mesh& mesh);
};

static_assert(epix::camera::MeshAabbImpl<epix::mesh::Mesh>);
