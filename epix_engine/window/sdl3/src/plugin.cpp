// The engine provides the process entry point; SDL must not generate one.
#define SDL_MAIN_HANDLED 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

#include <epix/sdl3/core.hpp>

using namespace epix::sdl3;
using namespace epix::ecs;
using namespace epix::app;

void SDL3Plugin::attach(App& app) {
    spdlog::debug("[sdl3] Initializing SDL3 video subsystem.");
    // The engine provides the process entry point itself, so SDL must be told
    // that its own main() wiring is not in use.
    SDL_SetMainReady();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("Failed to initialize SDL3 video: ") + SDL_GetError());
    }
    const int version      = SDL_GetVersion();
    const char* driver     = SDL_GetCurrentVideoDriver();
    spdlog::info("[sdl3] SDL3 initialized successfully with video driver '{}' (SDL {}.{}.{}).",
                 driver != nullptr ? driver : "unknown", SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
                 SDL_VERSIONNUM_MICRO(version));
    app.world_mut().insert_resource(Clipboard{});
    app.world_mut().init_resource<SDL3windows>();
    app.world_mut().init_resource<SDL3Events>();
    app.add_events<SetClipboardString>().set_runner(std::make_unique<SDL3Runner>(app));
}

void SDL3Plugin::detach(App& app) {
    // SDL's own shutdown already destroys windows when the app ran through the
    // main callbacks, so anything left here belongs to a live video subsystem.
    auto& windows = app.world_mut().resource_mut<SDL3windows>();
    for (auto&& [entity, window] : windows) {
        SDL_DestroyWindow(window);
    }
    windows.clear();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
