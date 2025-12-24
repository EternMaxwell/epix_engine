#pragma once

#include <epix/assets.hpp>
#include <epix/render/assets.hpp>
#include <epix/render/pipeline.hpp>
#include <epix/render/shader.hpp>
#include <epix/render/view.hpp>
#include <optional>
#include <unordered_map>

#include "extract.hpp"
#include "gpumesh.hpp"
#include "mesh.hpp"

namespace epix::mesh {
struct MeshUniform {
    glm::mat4 model;
};
struct Mesh2d {
    assets::Handle<Mesh> handle;
};
struct RenderMesh2d {
    assets::AssetId<Mesh> mesh;
    size_t uniform_offset = 0;
};
struct MeshUniforms {
    std::vector<MeshUniform> uniforms;
    nvrhi::BufferHandle buffer;
};
struct RenderMesh2dInstances : std::unordered_map<Entity, RenderMesh2d> {};
struct Mesh3d {
    assets::Handle<Mesh> handle;
};
struct MeshPipeline {
   public:
    /// Binding Layout for View uniform.
    nvrhi::BindingLayoutHandle view_layout() const { return _view_layout; }
    /// Binding Layout for Mesh uniform. Containing model matrix.
    nvrhi::BindingLayoutHandle mesh_layout() const { return _mesh_layout; }
    /// Binding Layout for texture sampler and texture. Maybe used for both texture and shadow map.
    nvrhi::BindingLayoutHandle texture_layout() const { return _texture_layout; }
    nvrhi::BindingLayoutHandle push_constants_layout() const { return _push_constant_layout; }

    std::optional<render::RenderPipelineId> specialize(render::PipelineServer& pipeline_server,
                                                       const MeshAttributeLayout& mesh_layout);

   private:
    std::optional<render::RenderPipelineId> _get_cached_pipeline(size_t flag) const {
        auto it = _cached_pipelines.find(static_cast<uint8_t>(flag));
        if (it != _cached_pipelines.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    void _set_cached_pipeline(size_t flag, render::RenderPipelineId id) {
        _cached_pipelines[static_cast<uint8_t>(flag)] = id;
    }
    std::optional<assets::AssetId<render::Shader>> _get_vertex_shader(size_t flag) const {
        auto it = vertex_shaders.find(static_cast<uint8_t>(flag));
        if (it != vertex_shaders.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    std::optional<assets::AssetId<render::Shader>> _get_fragment_shader(size_t flag) const {
        auto it = fragment_shaders.find(static_cast<uint8_t>(flag));
        if (it != fragment_shaders.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    std::map<size_t, render::RenderPipelineId> _cached_pipelines;
    nvrhi::BindingLayoutHandle _view_layout;
    nvrhi::BindingLayoutHandle _mesh_layout;
    nvrhi::BindingLayoutHandle _texture_layout;
    nvrhi::BindingLayoutHandle _push_constant_layout;

    enum PipelineFlag : size_t {
        pos    = 1 << 0,
        color  = 1 << 1,
        normal = 1 << 2,
        uv0    = 1 << 3,
        uv1    = 1 << 4,
    };
    std::map<uint8_t, assets::AssetId<render::Shader>> vertex_shaders;
    std::map<uint8_t, assets::AssetId<render::Shader>> fragment_shaders;

    friend struct MeshPipelinePlugin;
};
struct MeshPipelinePlugin {
    void build(epix::App& app);
    void finish(epix::App& app);
};
template <size_t Slot>
struct BindMeshUniform {
    template <render::render_phase::PhaseItem P>
    struct Command {
        nvrhi::BufferHandle mesh_uniform_buffer;
        nvrhi::BindingLayoutHandle mesh_layout;
        nvrhi::BindingSetHandle binding_set;
        void prepare(World& world) {
            auto device         = world.resource<nvrhi::DeviceHandle>();
            auto mesh_layout    = world.resource<MeshPipeline>().mesh_layout();
            auto& mesh_uniforms = world.resource<MeshUniforms>();
            mesh_uniform_buffer = mesh_uniforms.buffer;
            this->mesh_layout   = mesh_layout;
        }
        bool render(const P& item,
                    Item<> entity_item,
                    std::optional<Item<>> view_item,
                    ParamSet<Res<nvrhi::DeviceHandle>, Res<RenderMesh2dInstances>> params,
                    render::render_phase::DrawContext& ctx) {
            auto&& [device, render_meshes] = params.get();

            auto entity = item.entity();
            if (!render_meshes->contains(entity)) return false;
            const RenderMesh2d& render_mesh = render_meshes->at(entity);
            // bind as a raw storage buffer (storage buffer) so offsets need not obey constant-buffer alignment
            binding_set = device.get()->createBindingSet(
                nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::RawBuffer_SRV(
                    0, mesh_uniform_buffer,
                    nvrhi::BufferRange{render_mesh.uniform_offset * sizeof(MeshUniform), sizeof(MeshUniform)})),
                mesh_layout);
            ctx.graphics_state.bindings.resize(Slot + 1);
            ctx.graphics_state.bindings[Slot] = binding_set;
            return true;
        }
    };
};
template <render::render_phase::PhaseItem P>
struct BindGpuMesh {
    void prepare(World&) {}
    bool render(const P& item,
                Item<> entity_item,
                std::optional<Item<>> view_item,
                ParamSet<Res<render::assets::RenderAssets<Mesh>>, Res<RenderMesh2dInstances>> params,
                render::render_phase::DrawContext& ctx) {
        auto&& [render_assets, render_meshes] = params.get();
        auto entity                           = item.entity();
        const GPUMesh* gpu_mesh               = render_assets->try_get(render_meshes->at(entity).mesh);
        if (!gpu_mesh) return false;
        gpu_mesh->bind_state(ctx.graphics_state);
        return true;
    }
};
template <render::render_phase::PhaseItem P>
struct DrawMesh {
    void prepare(World&) {}
    bool render(const P& item,
                Item<> entity_item,
                std::optional<Item<>> view_item,
                ParamSet<Res<render::assets::RenderAssets<Mesh>>, Res<RenderMesh2dInstances>> params,
                render::render_phase::DrawContext& ctx) {
        auto&& [render_assets, render_meshes] = params.get();
        auto entity                           = item.entity();
        auto& gpu_mesh                        = render_assets->get(render_meshes->at(entity).mesh);
        ctx.setGraphicsState();
        if (gpu_mesh.is_indexed()) {
            ctx.commandlist->drawIndexed(nvrhi::DrawArguments().setVertexCount(gpu_mesh.vertex_count()));
        } else {
            ctx.commandlist->draw(nvrhi::DrawArguments().setVertexCount(gpu_mesh.vertex_count()));
        }
        return true;
    }
};
struct MeshRenderPlugin {
    void build(epix::App& app);
    void finish(epix::App& app);
};
};  // namespace epix::mesh