#include <spdlog/spdlog.h>

#include <SFML/Window/WindowBase.hpp>

#include <epix/sfml/core.hpp>

using namespace epix::sfml;
using namespace epix::core;

void SFMLPlugin::attach(App& app) {
    spdlog::debug("[sfml] Attaching SFMLPlugin.");
    app.add_plugins(image::ImagePlugin{});
    app.world_mut().insert_resource(Clipboard{});
    app.world_mut().init_resource<SFMLwindows>();
    app.world_mut().init_resource<PendingWindowPositions>();
    app.add_events<SetClipboardString>().set_runner(std::make_unique<SFMLRunner>(app));
}
