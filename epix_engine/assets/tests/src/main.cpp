#include <epix/assets.h>

#include <iostream>
#include <string>

int main() {
    using namespace epix::assets;
    {
        std::cout << "===== Single thread test =====" << std::endl;
        std::cout << "Press any to continue" << std::endl;
        std::cin.get();
        Assets<std::string> assets;
        assets.set_log_level(spdlog::level::trace);
        assets.set_log_label("Assets");
        auto handle = assets.emplace("Hello, World!");
        {
            std::cout << "check handle 1" << std::endl;
            if (auto&& str_opt = assets.get(handle)) {
                std::cout << str_opt.value().get() << std::endl;
            }
        }
        std::cout << "create weak handle from handle 1" << std::endl;
        auto weak_handle = handle.weak();
        std::cout << "create a new strong handle from handle 1" << std::endl;
        auto strong2 = assets.get_strong_handle(handle);
        std::cout << "destruct handle 1" << std::endl;
        handle.~Handle();
        std::cout << "emplace new asset and assign to handle 1" << std::endl;
        handle = assets.emplace("Hello, World2!");
        assets.handle_events();
        {
            std::cout << "check handle 1" << std::endl;
            if (auto&& str_opt = assets.get(handle)) {
                std::cout << str_opt.value().get() << std::endl;
            }
        }
        handle.~Handle();
        assets.handle_events();
        {
            std::cout << "check weak handle" << std::endl;
            if (auto&& str_opt = assets.get(weak_handle)) {
                std::cout << str_opt.value().get() << std::endl;
            }
        }
        std::cout << "Emplace new asset, this time it should be allocated with "
                     "index 1, gen 1."
                  << std::endl;
        auto handle2 = assets.emplace("Hello, World3!");
        {
            std::cout << "check handle 2" << std::endl;
            AssetIndex index = handle2;
            std::cout << "handle2 index: " << index.index << std::endl;
            std::cout << "handle2 gen: " << index.generation << std::endl;
            if (auto&& str_opt = assets.get(handle2)) {
                std::cout << str_opt.value().get() << std::endl;
            }
        }
    }
    // {
    //     std::cout << "===== Multi thread test =====" << std::endl;
    //     std::cout << "Note that Assets struct it self is not thread safe"
    //               << std::endl;
    //     std::cout << "But the handles are thread safe" << std::endl;
    //     std::cout << "Thread safety of Assets is guaranteed by epix::Res and
    //     "
    //                  "epix::ResMut which are the preferred way to access
    //                  assets"
    //               << std::endl;
    //     std::cout << "Press any to continue" << std::endl;
    //     std::cin.get();
    //     Assets<std::string> assets;
    //     assets.set_log_label("Assets");
    //     assets.set_log_level(spdlog::level::trace);
    //     std::cout << std::endl;
    //     auto handle = assets.emplace("Hello, World!");
    //     auto weak   = handle;
    //     Handle<std::string> handle2;
    //     handle.~Handle();
    //     std::thread t1([&assets, weak, &handle2]() {
    //         auto handle = assets.get_strong_handle(weak);
    //         if (handle) {
    //             handle2 = std::move(handle.value());
    //         }
    //         std::this_thread::sleep_for(std::chrono::seconds(1));
    //         if (auto&& str_opt = assets.get(weak)) {
    //             std::cout << "Thread 1: " << str_opt.value().get() <<
    //             std::endl;
    //         }
    //     });
    //     std::thread t2([&assets, &handle2]() {
    //         std::this_thread::sleep_for(std::chrono::milliseconds(500));
    //         if (auto&& str_opt = assets.get(handle2)) {
    //             std::cout << "Thread 2: " << str_opt.value().get() <<
    //             std::endl;
    //         }
    //     });
    //     t1.join();
    //     t2.join();
    //     handle2.~Handle();
    //     assets.handle_events();
    // }
}