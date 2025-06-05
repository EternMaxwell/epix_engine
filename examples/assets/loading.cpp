#include <epix/app.h>
#include <epix/assets.h>

#include <iostream>
#include <string>

struct StringLoader {
    static constexpr std::array<const char*, 2> exts = {"txt", "log"};
    static auto extensions() noexcept { return exts; }
    static std::string load(
        const std::filesystem::path& path, epix::assets::LoadContext& context
    ) {
        // sleep for 1 second to simulate loading
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return "Loaded content from " + path.string();
    }
};

int main() {
    using namespace epix;
    App app    = App::create();
    auto start = std::chrono::steady_clock::now();
    app.add_plugins(LoopPlugin{});
    app.add_systems(
        Update, into([start](EventWriter<AppExit>& exit) {
            auto current   = std::chrono::steady_clock::now();
            double seconds = std::chrono::duration_cast<std::chrono::duration<
                double, std::chrono::seconds::period>>(current - start)
                                 .count();
            if (seconds > 5.0) {
                spdlog::info("Exiting app after 5 seconds.");
                exit.write(AppExit{});
            }
        })
    );
    app.add_plugins(
        assets::AssetPlugin{}.register_asset<std::string>().register_loader(
            StringLoader{}
        )
    );
    assets::Handle<std::string> handle1;
    app.add_systems(Startup, into([&](Res<assets::AssetServer>& asset_server) {
                        handle1 = asset_server->load<std::string>("test.txt");
                    }));
    app.add_systems(
        Update,
        into(
            [&](Res<assets::Assets<std::string>>& assets) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (auto opt = assets->get(handle1)) {
                    // spdlog::info("Asset loaded: {}", *opt);
                } else {
                    // spdlog::warn("Asset not loaded yet.");
                }
            },
            [](EventReader<assets::AssetEvent<std::string>>& events) {
                for (const auto& event : events.read()) {
                    if (event.is_loaded()) {
                        spdlog::info("Asset loaded: {}", event.id);
                    }
                }
            }
        )
    );
    app.run();
}