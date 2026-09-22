// Visual verification that 2D binning keys on the MESH asset id. Three different
// meshes - a box, a ring and a circle - share ONE ColorMaterial handle, so they
// share a pipeline and a material bind group and differ only by their mesh. If
// the bin key carried the material id, all three would land in a single bin and
// a single batch drawn with the representative entity's geometry; the fix makes
// each mesh its own bin, so the picture must show a square, a ring and a disc.

#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.app;
import epix.assets;
import epix.camera;
import epix.core_graph;
import epix.glfw.core;
import epix.glfw.render;
import epix.image;
import epix.input;
import epix.mesh;
import epix.render;
import epix.sprite;
import epix.sprite_render;
import epix.time;
import epix.transform;
import epix.window;

using namespace epix;

namespace {
struct SharedMaterialBinsVisualTestPlugin {
    void ready(app::App& app) {
        auto& world     = app.world_mut();
        auto& meshes    = world.resource_mut<assets::Assets<mesh::Mesh>>();
        auto& materials = world.resource_mut<assets::Assets<sprite_render::ColorMaterial>>();

        world.spawn(camera::Camera2d{}, transform::Transform{});

        // One material, shared by every entity below.
        const auto shared_material = materials.emplace(
            sprite_render::ColorMaterial{.color = glm::vec4(0.25f, 0.65f, 0.95f, 1.0f),
                                         .mode  = sprite_render::AlphaMode2dOpaque{}});

        const auto box    = meshes.emplace(mesh::make_box2d(220.0f, 220.0f));
        const auto ring   = meshes.emplace(mesh::AnnulusMeshBuilder{38.5f, 55.0f, 48}.build());
        const auto circle = meshes.emplace(mesh::CircleMeshBuilder{110.0f, 48}.build());

        constexpr float spacing = 280.0f;
        world.spawn(mesh::Mesh2d{box},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{shared_material},
                    transform::Transform{.translation = glm::vec3(-spacing, 0.0f, 0.0f)});
        world.spawn(mesh::Mesh2d{ring},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{shared_material},
                    transform::Transform{.translation = glm::vec3(0.0f, 0.0f, 0.0f)});
        world.spawn(mesh::Mesh2d{circle},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{shared_material},
                    transform::Transform{.translation = glm::vec3(spacing, 0.0f, 0.0f)});
    }
};
}  // namespace

int main() {
    app::App app = app::App::create();
    window::Window primary_window;
    primary_window.title = "Mesh2d Shared Material Bins Visual Test";
    primary_window.size  = {960, 540};

    app.add_plugins(app::TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{.primary_window = primary_window,
                                          .exit_condition = window::ExitCondition::OnPrimaryClosed})
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(sprite_render::Mesh2dRenderPlugin{})
        .add_plugins(sprite_render::ColorMaterialPlugin{})
        .add_plugins(SharedMaterialBinsVisualTestPlugin{});
    app.run();
}
