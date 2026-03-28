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

// Camera controls: WASD to pan, scroll to zoom, Space to reset
namespace {
struct CamControlPlugin {
    void build(core::App& app) {
        app.add_systems(core::Update,
                        core::into([](core::Query<core::Item<const render::camera::Camera&, render::camera::Projection&,
                                                             transform::Transform&>> camera,
                                      core::EventReader<input::MouseScroll> scroll_input,
                                      core::Res<input::ButtonInput<input::KeyCode>> key_states) {
                            auto opt = camera.single();
                            if (!opt) return;
                            auto&& [cam, proj, trans] = *opt;
                            if (key_states->pressed(input::KeyCode::KeySpace)) {
                                trans.translation = glm::vec3(0, 0, 0);
                                proj.as_orthographic().transform([&](render::camera::OrthographicProjection* ortho) {
                                    *ortho = render::camera::OrthographicProjection{};
                                    return true;
                                });
                                return;
                            }
                            glm::vec3 delta(0.0f);
                            if (key_states->pressed(input::KeyCode::KeyW)) delta += glm::vec3(0, 1, 0);
                            if (key_states->pressed(input::KeyCode::KeyS)) delta -= glm::vec3(0, 1, 0);
                            if (key_states->pressed(input::KeyCode::KeyA)) delta -= glm::vec3(1, 0, 0);
                            if (key_states->pressed(input::KeyCode::KeyD)) delta += glm::vec3(1, 0, 0);
                            if (glm::length(delta) > 0.0f) {
                                trans.translation += glm::normalize(delta) * 5.0f;
                            }
                            proj.as_orthographic().transform([&](render::camera::OrthographicProjection* ortho) {
                                for (const auto& e : scroll_input.read()) {
                                    ortho->scale *= std::exp(-static_cast<float>(e.yoffset) * 0.1f);
                                }
                                return true;
                            });
                        }).set_name("camera control"));
    }
};

// Spawns a grid of N x N mesh instances sharing the same mesh handles,
// so the batching system can merge them into fewer draw calls.
struct MeshBatchingTestPlugin {
    void finish(core::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();

        world.spawn(core_graph::core_2d::Camera2DBundle{});

        // Three shared mesh handles: box, circle, triangle
        auto box_handle    = mesh_assets.emplace(mesh::make_box2d(20.0f, 20.0f));
        auto circle_handle = mesh_assets.emplace(mesh::make_circle(10.0f, std::nullopt, 16));
        auto tri_handle    = mesh_assets.emplace(mesh::Mesh()
                                                     .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
                                                     .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                                                                     std::vector<glm::vec3>{
                                                                         {0.0f, 12.0f, 0.0f},
                                                                         {-10.0f, -8.0f, 0.0f},
                                                                         {10.0f, -8.0f, 0.0f},
                                                                     })
                                                     .with_indices<std::uint16_t>(std::vector<std::uint16_t>{0, 1, 2}));

        std::mt19937 rng(1337);
        std::uniform_real_distribution<float> pos_dist(-900.0f, 900.0f);
        std::uniform_real_distribution<float> color_dist(0.3f, 1.0f);

        constexpr int kCount = 3000;  // 1000 of each shape

        const std::array shape_handles = {box_handle, circle_handle, tri_handle};

        for (int i = 0; i < kCount; ++i) {
            glm::vec4 color(color_dist(rng), color_dist(rng), color_dist(rng), 1.0f);
            glm::vec3 translation(pos_dist(rng), pos_dist(rng), 0.0f);

            world.spawn(mesh::Mesh2d{shape_handles[i % 3]},
                        mesh::MeshMaterial2d{.color = color, .alpha_mode = mesh::MeshAlphaMode2d::Opaque},
                        transform::Transform{.translation = translation});
        }

        // Also add a few transparent instances to test transparent batching
        for (int i = 0; i < 300; ++i) {
            glm::vec4 color(color_dist(rng), color_dist(rng), color_dist(rng), 0.5f);
            glm::vec3 translation(pos_dist(rng), pos_dist(rng), 0.01f);

            world.spawn(mesh::Mesh2d{box_handle},
                        mesh::MeshMaterial2d{.color = color, .alpha_mode = mesh::MeshAlphaMode2d::Blend},
                        transform::Transform{.translation = translation});
        }
    }
};
}  // namespace

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title =
        "Mesh Batching Pressure Test (3000 opaque + 300 transparent) | WASD pan, scroll zoom, Space reset";
    primary_window.size = {1280, 720};

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
        .add_plugins(CamControlPlugin{})
        .add_plugins(MeshBatchingTestPlugin{});

    app.run();
}
