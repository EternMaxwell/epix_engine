module;

export module epix.mesh:render;

import glm;
import epix.assets;
import epix.core;
import epix.image;

import :mesh;
import :gpumesh;

namespace mesh {
export enum class MeshAlphaMode2d {
    Opaque,
    Blend,
};

export struct Mesh2d {
    assets::Handle<Mesh> handle;
};

export struct MeshMaterial2d {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    MeshAlphaMode2d alpha_mode = MeshAlphaMode2d::Opaque;
};

export struct MeshTextureMaterial2d {
    assets::Handle<image::Image> image;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    MeshAlphaMode2d alpha_mode = MeshAlphaMode2d::Blend;
};

export struct MeshRenderPlugin {
    void build(core::App& app);
    void finish(core::App& app);
};
}  // namespace mesh