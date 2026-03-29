module;

#include <spdlog/spdlog.h>

#include <SFML/Window/WindowBase.hpp>

module epix.sfml.core;

import std;

using namespace epix::core;
using namespace epix::sfml;
using namespace epix::window;

SFMLRunner::SFMLRunner(App& app) {
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

    create_windows_system = make_system_unique(SFMLPlugin::create_windows);
    create_windows_system->set_name("sfml_create_windows");
    update_size_system = make_system_unique(SFMLPlugin::update_size);
    update_size_system->set_name("sfml_update_size");
    update_pos_system = make_system_unique(SFMLPlugin::update_pos);
    update_pos_system->set_name("sfml_update_pos");
    toggle_window_mode_system = make_system_unique(SFMLPlugin::toggle_window_mode);
    toggle_window_mode_system->set_name("sfml_toggle_window_mode");
    update_window_states_system = make_system_unique(SFMLPlugin::update_window_states);
    update_window_states_system->set_name("sfml_update_window_states");
    destroy_windows_system = make_system_unique(SFMLPlugin::destroy_windows);
    destroy_windows_system->set_name("sfml_destroy_windows");
    poll_and_send_events_system = make_system_unique(SFMLPlugin::poll_and_send_events);
    poll_and_send_events_system->set_name("sfml_poll_and_send_events");
    clipboard_set_text_system = make_system_unique(Clipboard::set_text);
    clipboard_set_text_system->set_name("sfml_clipboard_set_text");
    clipboard_update_system = make_system_unique(Clipboard::update);
    clipboard_update_system->set_name("sfml_clipboard_update");

    auto sfml_systems = std::array{
        toggle_window_mode_system.get(), update_size_system.get(),          update_pos_system.get(),
        create_windows_system.get(),     poll_and_send_events_system.get(), update_window_states_system.get(),
        destroy_windows_system.get(),    clipboard_set_text_system.get(),   clipboard_update_system.get(),
    };
    app.world_scope(
        [&](World& world) { std::ranges::for_each(sfml_systems, [&](auto& sys) { sys->initialize(world); }); });
}

template <typename... Ts>
struct visitor : Ts... {
    using Ts::operator()...;
};

bool SFMLRunner::step(App& app) {
    auto sfml_systems = std::array{
        toggle_window_mode_system.get(), update_size_system.get(),          update_pos_system.get(),
        create_windows_system.get(),     poll_and_send_events_system.get(), update_window_states_system.get(),
        destroy_windows_system.get(),    clipboard_set_text_system.get(),   clipboard_update_system.get(),
    };
    app.world_scope([&](World& world) {
        for (auto&& sys : sfml_systems) {
            sys->run({}, world)
                .transform([&]() { sys->apply_deferred(world); })
                .transform_error([&](const RunSystemError& error) {
                    std::visit(
                        visitor{[&](const ValidateParamError& validate_error) {
                                    spdlog::error("SFML System [{}] parameter validation error: type: {}, msg: {}",
                                                  sys->name(), validate_error.param_type.short_name(),
                                                  validate_error.message);
                                },
                                [&](const SystemException& sys_exception) {
                                    try {
                                        if (sys_exception.exception) std::rethrow_exception(sys_exception.exception);
                                    } catch (const std::exception& e) {
                                        spdlog::error("SFML System [{}] exception during run: {}", sys->name(),
                                                      e.what());
                                    } catch (...) {
                                        spdlog::error("SFML System [{}] unknown exception during run.", sys->name());
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
                                               "SFML extra system [{}] parameter validation error: type: {}, msg: {}",
                                               sys->name(), validate_error.param_type.short_name(),
                                               validate_error.message);
                                       },
                                       [&](const SystemException& sys_exception) {
                                           try {
                                               if (sys_exception.exception)
                                                   std::rethrow_exception(sys_exception.exception);
                                           } catch (const std::exception& e) {
                                               spdlog::error("SFML extra system [{}] exception during run: {}",
                                                             sys->name(), e.what());
                                           } catch (...) {
                                               spdlog::error("SFML extra system [{}] unknown exception during run.",
                                                             sys->name());
                                           }
                                       }},
                               error);
                    return error;
                });
        }
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

void SFMLRunner::exit(App& app) {
    if (render_app_future) {
        auto sub = render_app_future->get();
        if (sub) app.insert_sub_app(*render_app_label, std::move(sub));
        render_app_future.reset();
    }
    app.world_scope([&](World& world) {
        auto res = remove_window->run({}, world);
        res      = destroy_windows_system->run({}, world);
    });
    app.run_schedules(PreExit, Exit, PostExit);
}
