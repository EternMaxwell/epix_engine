#include <epix/app.h>
#include <epix/assets.h>

#include <fstream>
#include <iostream>
#include <string>

struct StringLoader {
    static constexpr std::array<const char*, 2> exts = {"txt", "log"};
    static auto extensions() noexcept { return exts; }
    static std::string load(
        const std::filesystem::path& path, epix::assets::LoadContext& context
    ) {
        auto size = std::filesystem::file_size(path);
        std::ifstream file(path);
        if (!file) {
            spdlog::error("Failed to open file: {}", path.string());
            throw std::runtime_error("Failed to open file: " + path.string());
        }
        std::string content(size, '\0');
        file.read(content.data(), size);
        return content;
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
                exit.write(AppExit{1});
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
        Update, into([](EventReader<assets::AssetEvent<std::string>>& events,
                        Res<assets::Assets<std::string>>& assets,
                        EventWriter<AppExit>& exit) {
            for (const auto& event : events.read()) {
                if (event.is_loaded()) {
                    spdlog::info("Asset loaded: {}", event.id);
                    spdlog::info("Content: \n{}", *assets->get(event.id));
                    exit.write(AppExit{0});
                }
            }
        })
    );
    app.run();
}