module;

#include <spdlog/spdlog.h>

#include <SFML/Window/WindowBase.hpp>


module epix.sfml.core;

using namespace epix::sfml;
using namespace epix::core;

void SFMLPlugin::build(App& app) {
    spdlog::debug("[sfml] Building SFMLPlugin.");
    app.add_plugins(image::ImagePlugin{});
    app.world_mut().insert_resource(Clipboard{});
    app.world_mut().init_resource<SFMLwindows>();
    app.add_events<SetClipboardString>().set_runner(std::make_unique<SFMLRunner>(app));
}
