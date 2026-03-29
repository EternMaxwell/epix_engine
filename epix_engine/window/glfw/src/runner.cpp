module;

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

module epix.glfw.core;

import std;

using namespace core;
using namespace glfw;
using namespace window;

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

    create_windows_system = make_system_unique(GLFWPlugin::create_windows);
    create_windows_system->set_name("glfw_create_windows");
    update_size_system = make_system_unique(GLFWPlugin::update_size);
    update_size_system->set_name("glfw_update_size");
    update_pos_system = make_system_unique(GLFWPlugin::update_pos);
    update_pos_system->set_name("glfw_update_pos");
    toggle_window_mode_system = make_system_unique(GLFWPlugin::toggle_window_mode);
    toggle_window_mode_system->set_name("glfw_toggle_window_mode");
    update_window_states_system = make_system_unique(GLFWPlugin::update_window_states);
    update_window_states_system->set_name("glfw_update_window_states");
    destroy_windows_system = make_system_unique(GLFWPlugin::destroy_windows);
    destroy_windows_system->set_name("glfw_destroy_windows");
    send_cached_events_system = make_system_unique(GLFWPlugin::send_cached_events);
    send_cached_events_system->set_name("glfw_send_cached_events");
    clipboard_set_text_system = make_system_unique(Clipboard::set_text);
    clipboard_set_text_system->set_name("glfw_clipboard_set_text");
    clipboard_update_system = make_system_unique(Clipboard::update);
    clipboard_update_system->set_name("glfw_clipboard_update");

    auto glfw_systems = std::array{
        toggle_window_mode_system.get(), update_size_system.get(),        update_pos_system.get(),
        create_windows_system.get(),     send_cached_events_system.get(), update_window_states_system.get(),
        destroy_windows_system.get(),    clipboard_set_text_system.get(), clipboard_update_system.get(),
    };
    app.world_scope(
        [&](World& world) { std::ranges::for_each(glfw_systems, [&](auto& sys) { sys->initialize(world); }); });
}
template <typename... Ts>
struct visitor : Ts... {
    using Ts::operator()...;
};
bool GLFWRunner::step(App& app) {
    auto glfw_systems = std::array{
        toggle_window_mode_system.get(), update_size_system.get(),        update_pos_system.get(),
        create_windows_system.get(),     send_cached_events_system.get(), update_window_states_system.get(),
        destroy_windows_system.get(),    clipboard_set_text_system.get(), clipboard_update_system.get(),
    };
    GLFWPlugin::poll_events();
    app.world_scope([&](World& world) {
        for (auto&& sys : glfw_systems) {
            sys->run({}, world)
                .transform([&]() { sys->apply_deferred(world); })
                .transform_error([&](const RunSystemError& error) {
                    std::visit(
                        visitor{[&](const ValidateParamError& validate_error) {
                                    spdlog::error("GLFW System [{}] parameter validation error: type: {}, msg: {}",
                                                  sys->name(), validate_error.param_type.short_name(),
                                                  validate_error.message);
                                },
                                [&](const SystemException& sys_exception) {
                                    try {
                                        if (sys_exception.exception) std::rethrow_exception(sys_exception.exception);
                                    } catch (const std::exception& e) {
                                        spdlog::error("GLFW System [{}] exception during run: {}", sys->name(),
                                                      e.what());
                                    } catch (...) {
                                        spdlog::error("GLFW System [{}] unknown exception during run.", sys->name());
                                    }
                                }},
                        error);
                    return error;
                });
        }
        for (auto&& sys : extra_systems) {
            if (!sys->initialized()) sys->initialize(world);
            sys->run({}, world)
                .transform([&]() { sys->apply_deferred(world); })
                .transform_error([&](const RunSystemError& error) {
                    std::visit(visitor{[&](const ValidateParamError& validate_error) {
                                           spdlog::error(
                                               "GLFW extra system [{}] parameter validation error: type: {}, msg: {}",
                                               sys->name(), validate_error.param_type.short_name(),
                                               validate_error.message);
                                       },
                                       [&](const SystemException& sys_exception) {
                                           try {
                                               if (sys_exception.exception)
                                                   std::rethrow_exception(sys_exception.exception);
                                           } catch (const std::exception& e) {
                                               spdlog::error("GLFW extra system [{}] exception during run: {}",
                                                             sys->name(), e.what());
                                           } catch (...) {
                                               spdlog::error("GLFW extra system [{}] unknown exception during run.",
                                                             sys->name());
                                           }
                                       }},
                               error);
                    return error;
                });
        }
        // std::ranges::for_each(glfw_systems, [&](auto& sys) { sys->run({}, world); });
    });
    app.update();
    std::optional<int> exit_code;
    app.world_scope([&](World& world) {
        auto res = check_exit->run({}, world);
        if (res.has_value() && res.value().has_value()) exit_code = res.value();
    });
    if (render_app_future) {
        auto sub = render_app_future->get();
        if (sub) app.insert_sub_app(*render_app_label, std::move(sub));
        render_app_future.reset();
    }
    if (exit_code.has_value()) return false;
    if (render_app_label) {
        auto sub = app.take_sub_app(*render_app_label);
        if (sub) {
            sub->extract(app);
            render_app_future =
                std::async(std::launch::async, [sub = std::move(sub)]() mutable -> std::unique_ptr<App> {
                    sub->update();
                    return std::move(sub);
                });
        }
    }
    return true;
}
void GLFWRunner::exit(App& app) {
    if (render_app_future) {
        auto sub = render_app_future->get();
        if (sub) app.insert_sub_app(*render_app_label, std::move(sub));
        render_app_future.reset();
    }
    app.world_scope([&](World& world) {
        auto res = remove_window->run({}, world);
        res      = destroy_windows_system->run({}, world);
    });
    glfwTerminate();
    app.run_schedules(PreExit, Exit, PostExit);
}