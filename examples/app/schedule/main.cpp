#include <epix/app.h>

using namespace epix;

int main() {
    async::RwLock<World> world(Main);
    auto write = world.write();
    // auto test_system = IntoSystem::into_system([]() {
    //     std::cout << "Test system running!" << std::endl;
    // });
    // test_system->initialize(*write);
    // test_system->run(*write);

    auto executors = std::make_shared<app::Executors>();
    executors->add_pool(ExecutorType::MultiThread, "MainPool", 4);
    Schedule schedule(Main);
    schedule.add_systems(
        into(
            []() { std::cout << "System 1 running!" << std::endl; },
            []() { std::cout << "System 2 running!" << std::endl; }
        ).chain()
    );
    schedule.initialize_systems(*write);
    app::RunState state(std::move(write), *executors);
    schedule.run(state);
}