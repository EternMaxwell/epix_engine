import std;
import glm;
import epix.core;
import epix.window;
import epix.glfw.core;
import epix.glfw.render;
import epix.render;
import epix.core_graph;
import epix.mesh;
import epix.transform;
import epix.input;
import epix.image;

namespace {
mesh::Mesh make_gradient_quad(float width, float height) {
    float half_width  = width * 0.5f;
    float half_height = height * 0.5f;

    std::vector<glm::vec3> positions = {
        {-half_width, -half_height, 0.0f},
        {half_width, -half_height, 0.0f},
        {half_width, half_height, 0.0f},
        {-half_width, half_height, 0.0f},
    };
    std::vector<glm::vec4> colors = {
        {0.97f, 0.36f, 0.22f, 1.0f},
        {0.98f, 0.78f, 0.18f, 1.0f},
        {0.16f, 0.75f, 0.52f, 1.0f},
        {0.14f, 0.39f, 0.92f, 1.0f},
    };
    std::vector<std::uint16_t> indices = {0, 1, 2, 2, 3, 0};

    return mesh::Mesh()
        .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
        .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
        .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
        .with_indices<std::uint16_t>(indices);
}

struct MeshRenderingVisualTestPlugin {
    void finish(core::App& app) {
        auto& world        = app.world_mut();
        auto& mesh_assets  = world.resource_mut<assets::Assets<mesh::Mesh>>();
        auto& image_assets = world.resource_mut<assets::Assets<image::Image>>();

        world.spawn(core_graph::core_2d::Camera2DBundle{});

        std::vector<std::uint8_t> texture_data = {
            255, 50, 50, 255, 50, 255, 50, 255, 50, 50, 255, 255, 255, 255, 50, 255,
        };
        auto texture_image  = image::Image::create2d(2, 2, image::Format::RGBA8, texture_data).value();
        auto texture_handle = image_assets.emplace(std::move(texture_image));

        auto solid_box          = mesh_assets.emplace(mesh::make_box2d(180.0f, 120.0f));
        auto gradient_quad      = mesh_assets.emplace(make_gradient_quad(180.0f, 180.0f));
        auto transparent_circle = mesh_assets.emplace(mesh::make_circle(78.0f, std::nullopt, 64));
        auto transparent_box    = mesh_assets.emplace(mesh::make_box2d(160.0f, 160.0f));
        auto textured_box       = mesh_assets.emplace(mesh::make_box2d_uv(160.0f, 160.0f));

        world.spawn(mesh::Mesh2d{solid_box}, mesh::MeshMaterial2d{.color = glm::vec4(0.95f, 0.31f, 0.20f, 1.0f)},
                    transform::Transform{
                        .translation = glm::vec3(-300.0f, 0.0f, 0.0f),
                    });

        world.spawn(mesh::Mesh2d{gradient_quad}, mesh::MeshMaterial2d{.color = glm::vec4(1.0f)},
                    transform::Transform{
                        .translation = glm::vec3(0.0f, 0.0f, 0.0f),
                    });

        world.spawn(mesh::Mesh2d{transparent_circle},
                    mesh::MeshMaterial2d{
                        .color      = glm::vec4(0.16f, 0.62f, 0.96f, 0.55f),
                        .alpha_mode = mesh::MeshAlphaMode2d::Blend,
                    },
                    transform::Transform{
                        .translation = glm::vec3(250.0f, 36.0f, 0.1f),
                    });

        world.spawn(mesh::Mesh2d{transparent_box},
                    mesh::MeshMaterial2d{
                        .color      = glm::vec4(0.99f, 0.68f, 0.20f, 0.50f),
                        .alpha_mode = mesh::MeshAlphaMode2d::Blend,
                    },
                    transform::Transform{
                        .translation = glm::vec3(300.0f, -18.0f, 0.0f),
                    });

        world.spawn(mesh::Mesh2d{textured_box},
                    mesh::MeshTextureMaterial2d{
                        .image      = texture_handle,
                        .color      = glm::vec4(1.0f),
                        .alpha_mode = mesh::MeshAlphaMode2d::Opaque,
                    },
                    transform::Transform{
                        .translation = glm::vec3(-120.0f, -220.0f, 0.0f),
                    });
    }
};
}  // namespace

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title = "Mesh Rendering Visual Test";
    primary_window.size  = {1280, 720};

    app.add_plugins(window::WindowPlugin{
                        .primary_window = primary_window,
                        .exit_condition = window::ExitCondition::OnPrimaryClosed,
                    })
        .add_plugins(input::InputPlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::RenderPlugin{}.set_validation(0))
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshRenderPlugin{})
        .add_plugins(MeshRenderingVisualTestPlugin{});

    app.run();
}