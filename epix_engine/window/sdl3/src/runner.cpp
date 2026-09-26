// The engine provides the process entry point; SDL must not generate one.
#define SDL_MAIN_HANDLED 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <epix/sdl3/core.hpp>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>
#include <variant>
using namespace epix::ecs;
using namespace epix::app;
using namespace epix::sdl3;
using namespace epix::window;

namespace {

SDL3Runner* active_runner = nullptr;

SDL_AppResult SDLCALL sdl_app_init(void** appstate, int argc, char** argv) {
    (void)appstate;
    (void)argc;
    (void)argv;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDLCALL sdl_app_iterate(void* appstate) {
    (void)appstate;
    if (active_runner == nullptr) return SDL_APP_SUCCESS;
    return active_runner->iterate() ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}

SDL_AppResult SDLCALL sdl_app_event(void* appstate, SDL_Event* event) {
    (void)appstate;
    if (active_runner == nullptr || event == nullptr) return SDL_APP_CONTINUE;
    active_runner->stage_event(event);
    return SDL_APP_CONTINUE;
}

void SDLCALL sdl_app_quit(void* appstate, SDL_AppResult result) {
    (void)appstate;
    (void)result;
    if (active_runner != nullptr) active_runner->on_sdl_quit();
}

}  // namespace

SDL3Runner::SDL3Runner(App& app) {
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

    create_windows_system = make_system_unique(SDL3Plugin::create_windows);
    create_windows_system->set_name("sdl3_create_windows");
    update_size_system = make_system_unique(SDL3Plugin::update_size);
    update_size_system->set_name("sdl3_update_size");
    update_pos_system = make_system_unique(SDL3Plugin::update_pos);
    update_pos_system->set_name("sdl3_update_pos");
    toggle_window_mode_system = make_system_unique(SDL3Plugin::toggle_window_mode);
    toggle_window_mode_system->set_name("sdl3_toggle_window_mode");
    update_window_states_system = make_system_unique(SDL3Plugin::update_window_states);
    update_window_states_system->set_name("sdl3_update_window_states");
    destroy_windows_system = make_system_unique(SDL3Plugin::destroy_windows);
    destroy_windows_system->set_name("sdl3_destroy_windows");
    destroy_all_windows_system = make_system_unique(SDL3Plugin::destroy_all_windows);
    destroy_all_windows_system->set_name("sdl3_destroy_all_windows");
    send_cached_events_system = make_system_unique(SDL3Plugin::send_cached_events);
    send_cached_events_system->set_name("sdl3_send_cached_events");
    clipboard_update_system = make_system_unique(SDL3Plugin::update_clipboard);
    clipboard_update_system->set_name("sdl3_clipboard_update");

    auto sdl3_systems = std::array{
        toggle_window_mode_system.get(),  update_size_system.get(),         update_pos_system.get(),
        create_windows_system.get(),      send_cached_events_system.get(),  update_window_states_system.get(),
        destroy_windows_system.get(),     destroy_all_windows_system.get(), clipboard_update_system.get(),
    };
    app.world_scope(
        [&](World& world) { std::ranges::for_each(sdl3_systems, [&](auto& sys) { sys->initialize(world); }); });
}

void SDL3Runner::run_frame_systems(World& world) {
    auto sdl3_systems = std::array{
        toggle_window_mode_system.get(),  update_size_system.get(),         update_pos_system.get(),
        create_windows_system.get(),      send_cached_events_system.get(),  update_window_states_system.get(),
        destroy_windows_system.get(),     clipboard_update_system.get(),
    };
    for (auto&& sys : sdl3_systems) {
        auto res = sys->run({}, world).transform_error([&](const RunSystemError& error) {
            std::visit(
                epix::utils::visitor{
                    [&](const ValidateParamError& validate_error) {
                        spdlog::error("SDL3 System [{}] parameter validation error: type: {}, msg: {}", sys->name(),
                                      validate_error.param_type.short_name(), validate_error.message);
                    },
                    [&](const SystemException& sys_exception) {
                        try {
                            if (sys_exception.exception) std::rethrow_exception(sys_exception.exception);
                        } catch (const std::exception& e) {
                            spdlog::error("SDL3 System [{}] exception during run: {}", sys->name(), e.what());
                        } catch (...) {
                            spdlog::error("SDL3 System [{}] unknown exception during run.", sys->name());
                        }
                    },
                    [&](const SystemResultError& result_error) {
                        spdlog::error("SDL3 System [{}] error result: type: {}, msg: {}", sys->name(),
                                      result_error.error_type.short_name(), result_error.message);
                    }},
                error);
            return error;
        });
    }
    for (auto&& sys : extra_systems) {
        if (!sys->initialized()) sys->initialize(world);
        auto res = sys->run({}, world).transform_error([&](const RunSystemError& error) {
            std::visit(
                epix::utils::visitor{
                    [&](const ValidateParamError& validate_error) {
                        spdlog::error("SDL3 extra system [{}] parameter validation error: type: {}, msg: {}",
                                      sys->name(), validate_error.param_type.short_name(), validate_error.message);
                    },
                    [&](const SystemException& sys_exception) {
                        try {
                            if (sys_exception.exception) std::rethrow_exception(sys_exception.exception);
                        } catch (const std::exception& e) {
                            spdlog::error("SDL3 extra system [{}] exception during run: {}", sys->name(), e.what());
                        } catch (...) {
                            spdlog::error("SDL3 extra system [{}] unknown exception during run.", sys->name());
                        }
                    },
                    [&](const SystemResultError& result_error) {
                        spdlog::error("SDL3 extra system [{}] error result: type: {}, msg: {}", sys->name(),
                                      result_error.error_type.short_name(), result_error.message);
                    }},
                error);
            return error;
        });
    }
}

void SDL3Runner::shutdown_render_app(App& app) {
    if (render_app_future) {
        auto sub = render_app_future->get();
        if (sub) sub.reset();  // destroy sub app
        render_app_future.reset();
    }
    if (render_app_label) {
        app.take_sub_app(*render_app_label).reset();
    }
}

bool SDL3Runner::iterate() {
    if (app == nullptr) return false;
    run_frame_systems(app->world_mut());
    app->world_mut().resource_mut<SDL3Events>().events.clear();
    app->update();

    std::optional<int> exit_code;
    app->world_scope([&](World& world) {
        auto res = check_exit->run({}, world);
        if (res.has_value() && res.value().has_value()) exit_code = res.value();
    });
    if (render_app_future) {
        auto sub = render_app_future->get();
        if (sub) app->insert_sub_app(*render_app_label, std::move(sub));
        render_app_future.reset();
    }
    if (exit_code.has_value()) {
        // SDL tears its video subsystem down as soon as this callback reports
        // termination, so the render sub-app has to stop before that happens.
        shutdown_render_app(*app);
        return false;
    }
    if (render_app_label) {
        auto sub = app->take_sub_app(*render_app_label);
        if (sub) {
            sub->extract(*app);
            render_app_future =
                std::async(std::launch::async, [sub = std::move(sub)]() mutable -> std::unique_ptr<App> {
                    sub->update();
                    return std::move(sub);
                });
        }
    }
    return true;
}

void SDL3Runner::stage_event(const void* sdl_event) {
    if (app == nullptr || sdl_event == nullptr) return;
    const auto* bytes = static_cast<const std::byte*>(sdl_event);
    auto& queue       = app->world_mut().resource_mut<SDL3Events>().events;
    queue.insert(queue.end(), bytes, bytes + sizeof(SDL_Event));
}

void SDL3Runner::on_sdl_quit() {
    spdlog::debug("[sdl3] SDL is shutting its video subsystem down.");
    if (app == nullptr) return;
    // SDL destroys every window as part of its own shutdown; the entity map
    // would otherwise keep pointers to freed windows.
    app->world_mut().resource_mut<SDL3windows>().clear();
}

bool SDL3Runner::step(App& app_ref) {
    if (main_loop_finished) return false;
    main_loop_finished = true;
    app                = &app_ref;
    active_runner      = this;
    spdlog::debug("[sdl3] Handing the main loop to SDL's application callbacks.");
    // SDL keeps calling the callbacks below until the callbacks report
    // termination, including from the timer it installs during a Windows modal
    // move/resize loop - which is what keeps frames flowing while the user
    // drags or resizes a window.
    SDL_EnterAppMainCallbacks(0, nullptr, &sdl_app_init, &sdl_app_iterate, &sdl_app_event, &sdl_app_quit);
    active_runner = nullptr;
    app           = nullptr;
    return false;
}

void SDL3Runner::exit(App& app_ref) {
    spdlog::debug("[sdl3] Runner exiting.");
    shutdown_render_app(app_ref);
    app_ref.world_scope([&](World& world) {
        auto res = remove_window->run({}, world);
        res      = destroy_windows_system->run({}, world);
        res      = destroy_all_windows_system->run({}, world);
    });
    app_ref.run_schedules(PreExit, Exit, PostExit);
}
