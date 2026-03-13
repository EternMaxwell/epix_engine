import epix.core;
import epix.window;
import epix.glfw.core;
import epix.glfw.render;
import epix.render;
import epix.core_graph;
import epix.mesh;
import epix.transform;
import epix.input;
import epix.experimental.fallingsand;

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title = "Falling Sand Interactive Test (LMB add, RMB erase, Space pause, R reset)";
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
        .add_plugins(ext::fallingsand::SimpleFallingSandPlugin{});

    app.run();
}
