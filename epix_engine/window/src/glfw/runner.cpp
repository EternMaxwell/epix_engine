#include "epix/core.hpp"
#include "epix/glfw/glfw.hpp"
#include "epix/window.hpp"

using namespace epix::glfw;
using namespace epix::window;
using namespace epix;

GLFWRunner::GLFWRunner(App& app) {
    check_exit    = make_system_unique([](EventReader<AppExit> exit_event) -> std::optional<int> {
        for (auto event : exit_event.read()) {
            return event.code;
        }
        return std::nullopt;
    });
    remove_window = make_system_unique([](Commands commands, Query<Item<Entity, Mut<window::Window>>> windows) {
        for (auto&& [id, window] : windows.iter()) {
            commands.entity(id).despawn();
        }
    });
    exit_access   = check_exit->initialize(app.world_mut());
    remove_access = remove_window->initialize(app.world_mut());

    create_windows_system       = make_system_unique(GLFWPlugin::create_windows);
    update_size_system          = make_system_unique(GLFWPlugin::update_size);
    update_pos_system           = make_system_unique(GLFWPlugin::update_pos);
    toggle_window_mode_system   = make_system_unique(GLFWPlugin::toggle_window_mode);
    update_window_states_system = make_system_unique(GLFWPlugin::update_window_states);
    destroy_windows_system      = make_system_unique(GLFWPlugin::destroy_windows);
    send_cached_events_system   = make_system_unique(GLFWPlugin::send_cached_events);
    clipboard_set_text_system   = make_system_unique(Clipboard::set_text);
    clipboard_update_system     = make_system_unique(Clipboard::update);

    auto glfw_systems = std::array{
        toggle_window_mode_system.get(), update_size_system.get(),        update_pos_system.get(),
        create_windows_system.get(),     send_cached_events_system.get(), update_window_states_system.get(),
        destroy_windows_system.get(),    clipboard_set_text_system.get(), clipboard_update_system.get(),
    };
    app.world_scope(
        [&](World& world) { std::ranges::for_each(glfw_systems, [&](auto& sys) { sys->initialize(world); }); });
}
bool GLFWRunner::step(App& app) {
    auto glfw_systems = std::array{
        toggle_window_mode_system.get(), update_size_system.get(),        update_pos_system.get(),
        create_windows_system.get(),     send_cached_events_system.get(), update_window_states_system.get(),
        destroy_windows_system.get(),    clipboard_set_text_system.get(), clipboard_update_system.get(),
    };
    GLFWPlugin::poll_events();
    app.world_scope(
        [&](World& world) { std::ranges::for_each(glfw_systems, [&](auto& sys) { sys->run({}, world); }); });
    app.update().wait();
    std::optional<int> exit_code;
    exit_code = app.system_dispatcher()->dispatch_system(*check_exit, {}, exit_access).get().value_or(-1);
    if (render_app_future) render_app_future->wait();
    render_app_future.reset();
    if (exit_code.has_value()) return false;
    if (auto render_app = app.get_sub_app_mut(core::AppLabel::from_type<render::RenderT>())) {
        render_app.value().get().extract(app);
        render_app_future = render_app.value().get().update();
    }
    return true;
}
void GLFWRunner::exit(App& app) {
    if (render_app_future) render_app_future->wait();
    render_app_future.reset();
    app.world_scope([&](World& world) {
        auto res = remove_window->run({}, world);
        res      = destroy_windows_system->run({}, world);
    });
    glfwTerminate();
    app.run_schedules(PreExit, Exit, PostExit).wait();
}