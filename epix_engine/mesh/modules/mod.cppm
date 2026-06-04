module;
#include <epix/mesh.hpp>

export module epix.mesh;

export namespace epix::mesh {
using epix::mesh::BindMesh2dInstances;
using epix::mesh::BindMesh2dTexture;
using epix::mesh::DrawMesh2dBatch;
using epix::mesh::ExtractedMesh2d;
using epix::mesh::GPUMesh;
using epix::mesh::make_box2d;
using epix::mesh::make_box2d_uv;
using epix::mesh::make_circle;
using epix::mesh::Mesh;
using epix::mesh::Mesh2d;
using epix::mesh::MeshAlphaMode2d;
using epix::mesh::MeshAttribute;
using epix::mesh::MeshAttributeData;
using epix::mesh::MeshAttributeLayout;
using epix::mesh::MeshBatch;
using epix::mesh::MeshError;
using epix::mesh::MeshIndices;
using epix::mesh::MeshInstanceBuffer;
using epix::mesh::MeshInstanceData;
using epix::mesh::MeshMaterial2d;
using epix::mesh::MeshPlugin;
using epix::mesh::MeshRenderPlugin;
using epix::mesh::MeshTextureMaterial2d;
}  // namespace epix::mesh
