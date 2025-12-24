#pragma once

#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>

#include "nvrhi/nvrhi.h"
#include "render.hpp"

namespace epix::mesh {
template <typename T>
struct Material2d {};
/// A color material used for rendering a mesh with a solid color.
/// Adding this component to entity with Mesh2d means this mesh will be rendered using ColorMaterialPlugin.
struct ColorMaterial {
    glm::vec4 color = glm::vec4(1.0f);
};
/// An image material used for rendering a mesh with a texture.
struct ImageMaterial {
    assets::Handle<image::Image> image;
};
/// A no material used for rendering a mesh with no material.
/// Requires the mesh to have color attribute.
struct NoMaterial {};
struct MeshColorMaterials : std::unordered_map<Entity, ColorMaterial> {};
template <render::render_phase::PhaseItem P>
struct PushColorMaterial {
    nvrhi::BindingSetHandle binding_set;
    void prepare(World& world) {
        auto device         = world.resource<nvrhi::DeviceHandle>();
        auto& mesh_pipeline = world.resource<MeshPipeline>();
        binding_set         = device->createBindingSet(
            nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(glm::mat4))),
            mesh_pipeline.push_constants_layout());
    }
    bool render(const P& item,
                Item<> entity_item,
                std::optional<Item<>> view_item,
                ParamSet<Res<nvrhi::DeviceHandle>, Res<MeshColorMaterials>> params,
                render::render_phase::DrawContext& ctx) {
        ctx.graphics_state.bindings.resize(3);
        ctx.graphics_state.bindings[2] = binding_set;

        auto&& [device, color_materials] = params.get();
        auto entity                      = item.entity();
        if (!color_materials->contains(entity)) {
            return false;
        }
        auto&& color_material = color_materials->at(entity);
        ctx.setPushConstants(&color_material, sizeof(ColorMaterial));
        return true;
    }
};
struct ColorMaterialPlugin {
    void build(epix::App& app);
};
};  // namespace epix::mesh