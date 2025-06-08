#include <epix/app.h>
#include <epix/assets.h>
#include <epix/image.h>

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
struct AnotherLoader {
    static constexpr std::array<const char*, 1> exts = {"txt"};
    static auto extensions() noexcept { return std::views::all(exts); }
    static int load(
        const std::filesystem::path& path, epix::assets::LoadContext& context
    ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return 42;  // Just a dummy implementation
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
    app.add_plugins(assets::AssetPlugin{}
                        .register_asset<std::string>()
                        .register_loader(StringLoader{})
                        .register_asset<int>()
                        .register_loader(AnotherLoader{}));
    app.add_plugins(epix::image::ImagePlugin{});
    assets::Handle<std::string> handle1;
    assets::Handle<int> handle2;
    assets::Handle<image::Image> image_handle;
    app.add_systems(Startup, into([&](Res<assets::AssetServer>& asset_server) {
                        handle1 = asset_server->load<std::string>("test.txt");
                        handle2 = asset_server->load<int>("test.txt");
                        image_handle =
                            asset_server->load<image::Image>("test.png");
                    }));
    app.add_systems(
        Update,
        into(
            [str_loaded = false, int_loaded = false](
                EventReader<assets::AssetEvent<std::string>>& events,
                EventReader<assets::AssetEvent<int>>& int_events,
                Res<assets::Assets<std::string>>& assets,
                Res<assets::Assets<int>>& int_assets, EventWriter<AppExit>& exit
            ) mutable {
                for (const auto& event : events.read()) {
                    if (event.is_loaded()) {
                        str_loaded = true;
                        spdlog::info("Asset loaded: {}", event.id);
                        spdlog::info("Content: \n{}", *assets->get(event.id));
                    }
                }
                for (const auto& event : int_events.read()) {
                    if (event.is_loaded()) {
                        int_loaded = true;
                        spdlog::info("Asset loaded: {}", event.id);
                        spdlog::info("Content: {}", *int_assets->get(event.id));
                    }
                }
                if (str_loaded && int_loaded) {
                    spdlog::info("All assets loaded, exiting app.");
                    exit.write(AppExit{0});
                }
            },
            [](EventReader<assets::AssetEvent<image::Image>>& image_events,
               Res<assets::Assets<image::Image>>& images) {
                for (const auto& event : image_events.read()) {
                    if (event.is_loaded()) {
                        spdlog::info("Image asset loaded: {}", event.id);
                        const auto& img = *images->get(event.id);
                        spdlog::info(
                            "Image size: {}x{}", img.info.extent.width,
                            img.info.extent.height
                        );
                    }
                }
            }
        )
    );
    app.run();
}