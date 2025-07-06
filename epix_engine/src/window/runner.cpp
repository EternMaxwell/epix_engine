#include "epix/window.h"

using namespace epix::glfw;
using namespace epix::window;
using namespace epix;

EPIX_API int GLFWRunner::run(App& app) {
    auto check_exit = IntoSystem::into_system(
        [](EventReader<AppExit>& exit_event) -> std::optional<int> {
            for (auto event : exit_event.read()) {
                return event.code;
            }
            return std::nullopt;
        }
    );
    auto create_windows_system =
        IntoSystem::into_system(GLFWPlugin::create_windows);
    auto update_size_system = IntoSystem::into_system(GLFWPlugin::update_size);
    auto update_pos_system  = IntoSystem::into_system(GLFWPlugin::update_pos);
    auto toggle_window_mode_system =
        IntoSystem::into_system(GLFWPlugin::toggle_window_mode);
    auto update_window_states_system =
        IntoSystem::into_system(GLFWPlugin::update_window_states);
    auto destroy_windows_system =
        IntoSystem::into_system(GLFWPlugin::destroy_windows);
    auto send_cached_events_system =
        IntoSystem::into_system(GLFWPlugin::send_cached_events);
    auto clipboard_set_text_system =
        IntoSystem::into_system(Clipboard::set_text);
    auto clipboard_update_system = IntoSystem::into_system(Clipboard::update);
    auto update_focus_system =
        IntoSystem::into_system([](ResMut<window::Focus> focus,
                                   Local<window::Focus> last,
                                   ResMut<GLFWwindows> glfw_windows) {
            if (focus->focus != last->focus) {
                if (auto it = glfw_windows->find(*focus->focus);
                    it != glfw_windows->end()) {
                    auto window = it->second.first;
                    glfwFocusWindow(window);
                }
            }
            last->focus = focus->focus;
        });
    auto glfw_work_load = [&] {
        GLFWPlugin::poll_events();
        app.run_system(toggle_window_mode_system.get());
        app.run_system(update_size_system.get());
        app.run_system(update_pos_system.get());
        app.run_system(create_windows_system.get());
        app.run_system(send_cached_events_system.get());
        app.run_system(update_window_states_system.get());
        app.run_system(destroy_windows_system.get());
        app.run_system(update_focus_system.get());
        app.run_system(clipboard_set_text_system.get());
        app.run_system(clipboard_update_system.get());
    };
    std::optional<int> exit_code;
    if (glfwInit() == GLFW_FALSE) {
        spdlog::error("Failed to initialize GLFW");
        return -1;
    }
    glfw_work_load();
    auto profiler  = app.get_resource<AppProfiler>();
    auto last_time = std::chrono::steady_clock::now();
    std::optional<std::future<void>> render_app_future;
    while (true) {
        app.update().wait();
        if (render_app_future.has_value()) {
            render_app_future->wait();
            render_app_future.reset();
        }
        if (app.config.enable_tracy) {
            ZoneScopedN("glfw work load");
            glfw_work_load();
        } else {
            glfw_work_load();
        }
        if (app.config.mark_frame) {
            FrameMark;
        }
        exit_code = app.run_system(check_exit.get()).value_or(-1);
        auto time = std::chrono::steady_clock::now();
        double delta_time =
            std::chrono::duration<double, std::milli>(time - last_time).count();
        last_time = time;
        if (profiler) {
            profiler->push_time(delta_time);
        }
        if (exit_code.has_value()) {
            // should exit app, remove all windows.
            app.run_system([](Commands& commands,
                              Query<Get<Entity, Mut<window::Window>>>& windows
                           ) {
                for (auto&& [id, window] : windows.iter()) {
                    commands.entity(id).despawn();
                }
            });
            app.run_system([](World& world) {
                world.command_queue().apply(world);
            });
            break;
        }
        if (auto* render_app = app.get_sub_app(Render)) {
            render_app->extract(app);
            render_app_future = render_app->update();
        }
    }
    spdlog::info("[app] Exiting app.");
    app.run_system(destroy_windows_system.get());
    glfwTerminate();
    app.exit();
    spdlog::info("[app] App terminated.");
    return exit_code.value();
}