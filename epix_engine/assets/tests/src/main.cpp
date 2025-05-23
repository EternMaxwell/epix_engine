#include <epix/app.h>
#include <epix/assets.h>

#include <iostream>
#include <string>

struct NonCopy {
    NonCopy()                          = default;
    NonCopy(const NonCopy&)            = delete;
    NonCopy& operator=(const NonCopy&) = delete;
    NonCopy(NonCopy&&)                 = default;
    NonCopy& operator=(NonCopy&&)      = default;
};

struct string : public std::string, public NonCopy {};

void test_1() {
    static const auto* description = R"(
    Test 1: Asset Auto Destruction
    - Create an asset and get a strong handle to it
    - Create a weak handle from the strong handle
    - Check if the handles are valid
    - Destroy strong handle1
    - Handle handle destruction events
    - Check if the weak handle is still valid
    - Check if the strong handle is invalid
    )";

    std::cout << "===== Test 1: Asset Auto Destruction =====" << std::endl;
    std::cout << description << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    epix::assets::Assets<string> assets;
    // assets.set_log_level(spdlog::level::trace);
    assets.set_log_label("Assets");
    assets.set_log_level(spdlog::level::trace);

    // Create an asset and get a strong handle to it
    auto handle1 = assets.emplace("Hello Assets!");

    // Create a weak handle from the strong handle
    auto weak_handle1 = handle1.weak();

    // Check if the handles are valid
    if (auto&& opt = assets.get(handle1)) {
    } else {
        std::cerr
            << "Test fail: Handle1 is invalid after creation and handle assign."
            << std::endl;
        return;
    }

    // Check if the weak handle is valid
    if (auto&& opt = assets.get(weak_handle1)) {
    } else {
        std::cerr << "Test fail: Weak handle1 from handle1 is invalid but it "
                     "should be valid"
                  << std::endl;
        return;
    }

    // Destroy strong handle1
    handle1.~Handle();

    // Handle events
    assets.handle_events();

    // Check if the weak handle is still valid
    if (auto&& opt = assets.get(handle1)) {
        std::cerr << "Test fail: Handle1 is valid after destruction but it "
                     "should be invalid"
                  << std::endl;
        return;
    } else {
    }

    std::cout << "Test 1 passed!" << std::endl;
}

void test_2() {
    static const auto* description = R"(
    Test 2: Multi strong handle
    - Create an asset and get a strong handle to it
    - Create a second strong handle from the first one
    - Check if the handles are valid
    - Destroy the first strong handle
    - Handle handle destruction events
    - Check if the asset is still valid
    )";

    std::cout << "===== Test 2: Multi strong handle =====" << std::endl;
    std::cout << description << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    epix::assets::Assets<std::string> assets;
    // assets.set_log_level(spdlog::level::trace);
    assets.set_log_label("Assets");

    // Create an asset and get a strong handle to it
    auto handle1     = assets.emplace("Hello Assets!");
    auto handle2_opt = assets.get_strong_handle(handle1);
    if (!handle2_opt) {
        std::cerr << "Test fail: Cannot get strong handle from handle1"
                  << std::endl;
        return;
    }
    auto handle2 = *handle2_opt;

    // Check if the handles are valid
    if (auto&& opt = assets.get(handle1)) {
    } else {
        std::cerr << "Test fail: Handle1 is invalid after creation and handle "
                     "assign."
                  << std::endl;
        return;
    }
    if (auto&& opt = assets.get(handle2)) {
    } else {
        std::cerr << "Test fail: Handle2 is invalid after creation and handle "
                     "assign."
                  << std::endl;
        return;
    }

    // Destructing 1
    handle1.~Handle();
    // Handle events
    assets.handle_events();

    // Check if the asset is still valid
    if (auto&& opt = assets.get(handle2)) {
    } else {
        std::cerr << "Test fail: Handle2 is invalid after handle1 destruction "
                     "but it should be valid"
                  << std::endl;
        return;
    }

    std::cout << "Test 2 passed!" << std::endl;
}

void test_3() {
    static const auto* description = R"(
    Test 3: AssetIndex recycle
    - Create an asset and get a strong handle to it
    - Get the AssetIndex from the handle
    - Destroy the handle
    - Handle handle destruction events
    - Create a new asset and get a strong handle to it
    - Get the AssetIndex from the new handle
    - Check if the AssetIndex from the new handle is equal to the AssetIndex from the old handle
    - Check if the generation of the new handle is equal to the generation of the old handle + 1
    )";

    std::cout << "===== Test 3: AssetIndex recycle =====" << std::endl;
    std::cout << description << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    epix::assets::Assets<std::string> assets;
    // assets.set_log_level(spdlog::level::trace);
    assets.set_log_label("Assets");

    // Create an asset and get a strong handle to it
    auto handle1 = assets.emplace("Hello Assets!");

    epix::assets::AssetId<std::string> index1 = handle1;

    handle1.~Handle();
    // Handle events
    assets.handle_events();

    auto handle2 = assets.emplace("Hello Assets2!");
    epix::assets::AssetId<std::string> index2 = handle2;

    // Compare the indexes
    // if (index1.index != index2.index) {
    //     std::cerr << "Test fail: Index2 is not equal to Index1 after handle1
    //     "
    //                  "destruction but it should be equal"
    //               << std::endl;
    //     return;
    // }
    // if (index1.generation + 1 != index2.generation) {
    //     std::cerr
    //         << "Test fail: Index2 generation is not equal to Index1 "
    //            "generation + 1 after handle1 destruction but it should be "
    //            "equal"
    //         << std::endl;
    //     return;
    // }

    std::cout << "Test 3 passed!" << std::endl;
}

void test_4() {
    static const auto* description = R"(
    Test 4: Force removal and replacement
    - Create an asset and get a strong handle to it
    - Force remove the asset
    - Check if the handle is valid
    - Replace the asset with a new value
    - Check if the handle is valid
    - Check if the value is the expected value
    )";

    std::cout << "===== Test 4: Force removal and replacement ====="
              << std::endl;
    std::cout << description << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    epix::assets::Assets<std::string> assets;
    // assets.set_log_level(spdlog::level::trace);
    assets.set_log_label("Assets");

    // Create an asset and get a strong handle to it
    auto handle1 = assets.emplace("Hello Assets!");
    auto res     = assets.remove(handle1);
    if (!res) {
        std::cerr << "Test fail: Cannot remove handle1" << std::endl;
        return;
    }

    // Check if the handle is valid
    if (auto&& opt = assets.get(handle1)) {
        std::cerr << "Test fail: Handle should be invalid after force removal"
                  << std::endl;
        return;
    }

    // Replacing value
    std::optional<bool> res2 = assets.insert(handle1, "Hello Assets2!");
    if (!res2) {
        std::cerr << "Test fail: Unable to insert new value at the index that "
                     "has been force removed: index not valid(gen mismatch or "
                     "no asset slot at given index)"
                  << std::endl;
        return;
    } else if (res2.value()) {
        std::cerr << "Test fail: Insert value replaced old value, but old "
                     "value should have been removed"
                  << std::endl;
        return;
    }

    // Check value
    if (auto&& opt = assets.get(handle1)) {
        auto& str = *opt;
        if (str != "Hello Assets2!") {
            std::cerr << "Test fail: Insert value is not the expected value."
                      << std::endl;
            return;
        }
    } else {
        std::cerr << "Test fail: Handle invalid, but it should be valid after "
                     "inserting new value."
                  << std::endl;
        return;
    }

    std::cout << "Test 4 pass!" << std::endl;
}

namespace test5 {
using namespace epix;
void end_app(EventWriter<AppExit> exit, Local<std::optional<int>> count) {
    if (!*count) {
        *count = std::make_optional<int>(5);
    }
    (**count)--;
    if (!**count) {
        exit.write(AppExit{});
    }
}
void set_log(ResMut<assets::Assets<string>> ass) {
    ass->set_log_label("Assets");
    ass->set_log_level(spdlog::level::trace);
}
std::optional<assets::Handle<string>> handle;
void load_str(ResMut<assets::AssetLoader<string>> loader) {
    handle = loader->reserve("Hello Assets!");
}
void print_asset(Res<assets::Assets<string>> assets) {
    if (handle) {
        if (auto&& opt = assets->get(*handle)) {
            auto&& str = *opt;
            std::cout << str << std::endl;
        } else {
            std::cout << "Asset not loaded yet." << std::endl;
        }
    }
}
void frame_wait() {
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
}
}  // namespace test5

template <>
template <>
std::optional<string> epix::assets::AssetLoader<string>::load(
    const std::string& path
) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    return std::make_optional<string>(path);
}

void test_5() {
    std::cout << "===== Test 5: Assets<T> and loader in App =====" << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    using namespace epix;

    App app = App::create();
    app.add_plugin(
           assets::AssetPlugin{}.register_asset<string>().add_loader<string>()
    )
        .add_plugin(LoopPlugin{})
        .add_systems(Startup, into(test5::set_log, test5::load_str))
        .add_systems(
            Update, into(test5::end_app, test5::print_asset, test5::frame_wait)
        )
        .run();
}

void test_6() {
    static const auto* description = R"(
    Test 6: Reserving handles outside Asset<T>
    - Get the handle provider
    - Reserve a handle
    - Insert value to Assets<T> at index the handle points to
    - Check the value
    )";

    std::cout << "===== Test 6: Reserving handles outside Asset<T> ====="
              << std::endl;
    std::cout << description << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    epix::assets::Assets<std::string> assets;
    // assets.set_log_level(spdlog::level::trace);
    assets.set_log_label("Assets");

    auto provider = assets.get_handle_provider();
    auto handle1  = provider->reserve().typed<std::string>();

    auto res = assets.insert(handle1, "Hello Assets!");
    if (!res) {
        std::cerr
            << "Test fail: Unable to insert new value at the index: "
               "index not valid(gen mismatch or no asset slot at given index)"
            << std::endl;
        return;
    }

    // Check value
    if (auto&& opt = assets.get(handle1)) {
        auto& str = *opt;
        if (str != "Hello Assets!") {
            std::cerr << "Test fail: Insert value is not the expected value."
                      << std::endl;
            return;
        }
    } else {
        std::cerr << "Test fail: Handle invalid, but it should be valid after "
                     "inserting new value."
                  << std::endl;
        return;
    }

    std::cout << "Test 6 pass!" << std::endl;
}

void test_7() {
    static const auto* description =
        R"(
    Test 7: Asset<T> using uuid handle test
    - Create an Handle with uuid
    - Check if the handle is invalid before inserting
    - Insert value to Assets<T> at uuid the handle points to
    - Check the value
    - Destroy the handle
    - The handle should be valid since there is no strong handle
    - Get a strong handle to it
    - Check the value
    - Destroy the strong handle
    - Handle events
    - Check if the handle is invalid after destruction
    )";

    std::cout << "===== Test 7: Asset<T> using uuid handle test ====="
              << std::endl;
    std::cout << description << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    epix::assets::Assets<std::string> assets;
    assets.set_log_level(spdlog::level::trace);
    assets.set_log_label("Assets");

    std::mt19937 rng(std::random_device{}());
    uuids::uuid uuid = uuids::uuid_random_generator(&rng)();
    epix::assets::Handle<std::string> handle(uuid);
    // Check if the handle is invalid before inserting
    if (auto&& opt = assets.get(handle)) {
        std::cerr << "Test fail: Handle should be invalid before inserting"
                  << std::endl;
        return;
    }

    // Insert value to Assets<T> at uuid the handle points to
    auto res = assets.insert(handle, "Hello Assets!");
    if (!res) {
        std::cerr << "Test fail: Unable to insert value at uuid, but it should "
                     "always be valid"
                  << std::endl;
        return;
    }

    auto weak_handle = handle.weak();
    // Destroy the handle
    handle.~Handle();

    assets.handle_events();

    // Check if the handle is invalid after destruction
    if (auto&& opt = assets.get(weak_handle)) {
        auto& str = *opt;
    } else {
        std::cerr << "Handle is valid after destruction, but it should be "
                     "invalid"
                  << std::endl;
    }

    // Get a strong handle to it
    auto strong = assets.get_strong_handle(weak_handle);
    if (!strong) {
        std::cerr << "Test fail: Cannot get strong handle from weak handle"
                  << std::endl;
        return;
    }

    // Check the value
    if (auto&& opt = assets.get(*strong)) {
        auto& str = *opt;
        if (str != "Hello Assets!") {
            std::cerr << "Test fail: Insert value is not the expected value."
                      << std::endl;
            return;
        }
    } else {
        std::cerr << "Test fail: Handle invalid, but it should be valid after "
                     "inserting new value."
                  << std::endl;
        return;
    }

    // Destroy the strong handle
    strong->~Handle();
    // Handle events
    assets.handle_events();

    // Check if the handle is invalid after destruction
    if (auto&& opt = assets.get(weak_handle)) {
        std::cerr << "Test fail: Handle is valid after destruction, but it "
                     "should be invalid"
                  << std::endl;
        return;
    }

    std::cout << "Test 7 pass!" << std::endl;
}

void test(void (*f)()) {
    f();
    std::cout << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

int main() {
    using namespace epix::assets;

    std::cout << "===== Unit tests for Assets<T> =====" << std::endl;
    test(test_1);
    test(test_2);
    test(test_3);
    test(test_4);
    test(test_5);
    test(test_6);
    test(test_7);
}