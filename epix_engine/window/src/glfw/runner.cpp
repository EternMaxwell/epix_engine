#include "epix/core.hpp"
#include "epix/glfw/glfw.hpp"
#include "epix/window.hpp"

using namespace epix::glfw;
using namespace epix::window;
using namespace epix;

void GLFWRunner::run(App& app) {
    auto check_exit    = make_system_unique([](EventReader<AppExit> exit_event) -> std::optional<int> {
        for (auto event : exit_event.read()) {
            return event.code;
        }
        return std::nullopt;
    });
    auto remove_window = make_system_unique([](Commands commands, Query<Item<Entity, Mut<window::Window>>> windows) {
        for (auto&& [id, window] : windows.iter()) {
            commands.entity(id).despawn();
        }
    });
    auto exit_access   = check_exit->initialize(app.world_mut());
    auto remove_access = remove_window->initialize(app.world_mut());

    auto create_windows_system       = make_system_unique(GLFWPlugin::create_windows);
    auto update_size_system          = make_system_unique(GLFWPlugin::update_size);
    auto update_pos_system           = make_system_unique(GLFWPlugin::update_pos);
    auto toggle_window_mode_system   = make_system_unique(GLFWPlugin::toggle_window_mode);
    auto update_window_states_system = make_system_unique(GLFWPlugin::update_window_states);
    auto destroy_windows_system      = make_system_unique(GLFWPlugin::destroy_windows);
    auto send_cached_events_system   = make_system_unique(GLFWPlugin::send_cached_events);
    auto clipboard_set_text_system   = make_system_unique(Clipboard::set_text);
    auto clipboard_update_system     = make_system_unique(Clipboard::update);

    auto glfw_systems = std::array{
        toggle_window_mode_system.get(), update_size_system.get(),        update_pos_system.get(),
        create_windows_system.get(),     send_cached_events_system.get(), update_window_states_system.get(),
        destroy_windows_system.get(),    clipboard_set_text_system.get(), clipboard_update_system.get(),
    };
    app.world_scope(
        [&](World& world) { std::ranges::for_each(glfw_systems, [&](auto& sys) { sys->initialize(world); }); });
    // auto update_focus_system         = make_system_unique(
    //     [](ResMut<window::Focus> focus, Local<window::Focus> last, ResMut<GLFWwindows> glfw_windows) {
    //         if (focus->focus != last->focus) {
    //             if (auto it = glfw_windows->find(*focus->focus); it != glfw_windows->end()) {
    //                 auto window = it->second.first;
    //                 glfwFocusWindow(window);
    //             }
    //         }
    //         last->focus = focus->focus;
    //     });
    auto glfw_work_load = [&] {
        GLFWPlugin::poll_events();
        app.world_scope(
            [&](World& world) { std::ranges::for_each(glfw_systems, [&](auto& sys) { sys->run({}, world); }); });
    };
    std::optional<int> exit_code;
    glfw_work_load();
    // auto profiler  = app.get_resource<AppProfiler>();
    auto last_time = std::chrono::steady_clock::now();
    std::optional<std::future<bool>> render_app_future;
    while (true) {
        glfw_work_load();
        app.update().wait();
        if (render_app_future.has_value()) {
            render_app_future->wait();
            render_app_future.reset();
        }
        exit_code = app.system_dispatcher()->dispatch_system(*check_exit, {}, exit_access).get().value_or(-1);
        // double delta_time = std::chrono::duration<double, std::milli>(time - last_time).count();
        // last_time         = time;
        // if (profiler) {
        //     profiler->push_time(delta_time);
        // }
        if (exit_code.has_value()) {
            // should exit app, remove all windows.
            app.world_scope([&](World& world) { auto res = remove_window->run({}, world); });
            break;
        }
        if (auto render_app = app.get_sub_app_mut(core::AppLabel::from_type<render::Render>())) {
            render_app.value().get().extract(app);
            render_app_future = render_app.value().get().update();
        }
    }
    spdlog::info("[app] Exiting app.");
    app.world_scope([&](World& world) { auto res = destroy_windows_system->run({}, world); });
    glfwTerminate();
    app.run_schedules(PreExit, Exit, PostExit).wait();
    spdlog::info("[app] App terminated.");
}