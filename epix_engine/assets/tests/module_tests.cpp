#include <gtest/gtest.h>

import epix.assets;
import epix.core;
import std;

namespace tests {
struct NonCopy {
    NonCopy()                          = default;
    NonCopy(const NonCopy&)            = delete;
    NonCopy& operator=(const NonCopy&) = delete;
    NonCopy(NonCopy&&)                 = default;
    NonCopy& operator=(NonCopy&&)      = default;
};
struct string : public std::string, public NonCopy {};

TEST(assets, auto_destruct) {
    using namespace assets;
    Assets<string> assets;
    // Create an asset and get a strong handle to it
    std::optional handle1 = assets.emplace("Hello Assets!");
    // Create a weak handle from the strong handle
    auto weak_handle1 = handle1->weak();
    // Check if the handles are valid
    EXPECT_TRUE(assets.get(*handle1).has_value()) << "Handle1 is invalid after creation and handle assign.";
    // Check if the weak handle is valid
    EXPECT_TRUE(assets.get(weak_handle1).has_value()) << "Weak handle1 from handle1 is invalid but it should be valid";
    // Destroy strong handle1
    handle1.reset();
    // Handle events
    assets.handle_events_manual();
    // Check if the weak handle is still valid
    EXPECT_FALSE(assets.get(weak_handle1).has_value()) << "Handle1 is valid after destruction but it should be invalid";
}
TEST(assets, multi_strong_handle) {
    using namespace assets;
    Assets<std::string> assets;
    // Create an asset and get a strong handle to it
    std::optional handle1 = assets.emplace("Hello Assets!");
    auto handle2_opt      = assets.get_strong_handle(*handle1);
    ASSERT_TRUE(handle2_opt.has_value()) << "Cannot get strong handle from handle1";
    auto handle2 = *handle2_opt;
    // Check if the handles are valid
    EXPECT_TRUE(assets.get(*handle1).has_value()) << "Handle1 is invalid after creation and handle assign.";
    EXPECT_TRUE(assets.get(handle2).has_value()) << "Handle2 is invalid after creation and handle assign.";
    // Destructing 1
    handle1.reset();
    // Handle events
    assets.handle_events_manual();
    // Check if the asset is still valid
    EXPECT_TRUE(assets.get(handle2).has_value())
        << "Handle2 is invalid after handle1 destruction but it should be valid";
}
TEST(assets, index_recycle) {
    using namespace assets;
    Assets<std::string> assets;
    // Create an asset and get a strong handle to it
    std::optional handle1 = assets.emplace("Hello Assets!");
    AssetIndex index1     = std::get<AssetIndex>(handle1->id());
    handle1.reset();
    // Handle events
    assets.handle_events_manual();
    auto handle2      = assets.emplace("Hello Assets2!");
    AssetIndex index2 = std::get<AssetIndex>(handle2.id());
    // Compare the indexes
    EXPECT_EQ(index1.index(), index2.index())
        << "Index2 is not equal to Index1 after handle1 destruction but it should be equal";
    EXPECT_EQ(index1.generation() + 1, index2.generation())
        << "Index2 generation is not equal to Index1 generation + 1 after handle1 destruction but it should be equal";
}
TEST(assets, force_remove) {
    using namespace assets;
    Assets<std::string> assets;
    // Create an asset and get a strong handle to it
    auto handle1 = assets.emplace("Hello Assets!");
    EXPECT_TRUE(assets.remove(handle1)) << "Cannot remove handle1";
    // Check if the handle is valid
    ASSERT_FALSE(assets.get(handle1).has_value()) << "Handle should be invalid after force removal";
    // Replacing value
    auto res2 = assets.insert(handle1, "Hello Assets2!");
    ASSERT_TRUE(res2.has_value()) << "Unable to insert new value at the index that has been force removed: index not "
                                     "valid(gen mismatch or no asset slot at given index)";
    ASSERT_FALSE(res2.value()) << "Insert value replaced old value, but old value should have been removed";
    // Check value
    auto&& opt = assets.try_get(handle1);
    ASSERT_TRUE(opt.has_value()) << "Handle invalid, but it should be valid after inserting new value.";
    auto& str = opt.value().get();
    ASSERT_EQ(str, "Hello Assets2!") << "Insert value is not the expected value.";
}
TEST(assets, reserve) {
    using namespace assets;
    Assets<std::string> assets;

    auto provider               = assets.get_handle_provider();
    Handle<std::string> handle1 = provider->reserve();

    EXPECT_TRUE(assets.insert(handle1, "Hello Assets!").has_value())
        << "Unable to insert new value at the reserved index: index not valid(gen mismatch or no asset slot at given "
           "index)";

    // Check value
    auto&& opt = assets.get(handle1);
    ASSERT_TRUE(opt.has_value()) << "Handle invalid, but it should be valid after inserting new value.";
    auto& str = opt.value().get();
    ASSERT_EQ(str, "Hello Assets!") << "Insert value is not the expected value.";
}
}  // namespace tests

namespace tests {
using namespace assets;
struct StringLoader {
    static constexpr std::array<const char*, 2> exts = {"txt", "log"};
    static auto extensions() noexcept { return exts; }
    static std::string load(const std::filesystem::path& path, LoadContext& context) {
        auto size = std::filesystem::file_size(path);
        std::ifstream file(path);
        if (!file) {
            std::println(std::cerr, "Failed to open file: {}", path.string());
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
    static int load(const std::filesystem::path& path, LoadContext& context) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 42;  // Just a dummy implementation
    }
};

TEST(assets, loading) {
    using namespace core;
    using namespace assets;
    App app    = App::create();
    auto start = std::chrono::steady_clock::now();
    app.add_plugins(LoopPlugin{});
    app.add_systems(
        Update, into([start](EventWriter<AppExit> exit) {
            auto current = std::chrono::steady_clock::now();
            double seconds =
                std::chrono::duration_cast<std::chrono::duration<double, std::chrono::seconds::period>>(current - start)
                    .count();
            if (seconds > 0.5) {
                std::println(std::cout, "Exiting app after 0.5 seconds.");
                exit.write(AppExit{1});
            }
        }));
    app.add_plugins(assets::AssetPlugin{}
                        .register_asset<std::string>()
                        .register_loader(StringLoader{})
                        .register_asset<int>()
                        .register_loader(AnotherLoader{}));
    std::optional<assets::Handle<std::string>> handle1;
    std::optional<assets::Handle<int>> handle2;
    app.add_systems(Startup, into([&](Res<assets::AssetServer> asset_server) {
                        handle1 = asset_server->load<std::string>("test.txt");
                        handle2 = asset_server->load<int>("test.txt");
                    }));
    app.add_systems(Update,
                    into([str_loaded = false, int_loaded = false](
                             EventReader<assets::AssetEvent<std::string>> events,
                             EventReader<assets::AssetEvent<int>> int_events, Res<assets::Assets<std::string>> assets,
                             Res<assets::Assets<int>> int_assets, EventWriter<AppExit> exit) mutable {
                        for (const auto& event : events.read()) {
                            if (event.is_loaded()) {
                                str_loaded = true;
                                std::println(std::cout, "Asset loaded: {}", event.id);
                                std::println(std::cout, "Content: \n{}", assets->get(event.id).value().get());
                            }
                        }
                        for (const auto& event : int_events.read()) {
                            if (event.is_loaded()) {
                                int_loaded = true;
                                std::println(std::cout, "Asset loaded: {}", event.id);
                                std::println(std::cout, "Content: {}", int_assets->get(event.id).value().get());
                            }
                        }
                        if (str_loaded && int_loaded) {
                            std::println(std::cout, "All assets loaded, exiting app.");
                            exit.write(AppExit{0});
                        }
                    }));
    app.run();
}
}  // namespace tests

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}