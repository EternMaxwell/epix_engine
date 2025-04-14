#pragma once

#include <epix/prelude.h>

namespace test_rt_system {
using namespace epix;
void system_to_add() { std::cout << "this is the system added." << std::endl; }
void adding(ResMut<AppSystems> systems, Local<std::optional<int>> count) {
    if (!count->has_value()) {
        *count = 5;
    }
    if (!**count) {
        std::cout << "adding system" << std::endl;
        systems->remove_system(Update, adding);
        systems->add_system(Update, system_to_add);
    }
    (**count)--;
}
void exit(EventWriter<AppExit> event, Local<std::optional<int>> count) {
    if (!count->has_value()) {
        *count = 10;
    }
    if (!**count) {
        event.write(AppExit{});
    }
    (**count)--;
}

void test() {
    App app = App::create();
    app.enable_loop().add_system(Update, into(exit, adding)).run();
}
}  // namespace test_rt_system