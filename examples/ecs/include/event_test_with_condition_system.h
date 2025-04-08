#include <epix/prelude.h>

#include <format>
#include <iomanip>
#include <iostream>
#include <random>

namespace test_event {
using namespace epix;
using namespace prelude;

struct TestEvent {
    int data;
};

void write_event_s(EventWriter<TestEvent> event) {
    std::cout << "write_event" << std::endl;
    event.write(TestEvent{.data = 100}).write(TestEvent{.data = 200});
    std::cout << std::endl;
}

void read_event_s(EventReader<TestEvent> event) {
    std::cout << "read_event" << std::endl;
    for (auto& e : event.read()) {
        std::cout << "event: " << e.data << std::endl;
    }
    std::cout << std::endl;
}

void write_event_u(EventWriter<TestEvent> event) {
    std::cout << "write_event" << std::endl;
    event.write(TestEvent{.data = 100}).write(TestEvent{.data = 200});
    std::cout << std::endl;
}

void read_event_u(EventReader<TestEvent> event) {
    std::cout << "read_event" << std::endl;
    for (auto& e : event.read()) {
        std::cout << "event: " << e.data << std::endl;
    }
    std::cout << std::endl;
}

void read_event_u2(EventReader<TestEvent> event) {
    std::cout << "read_event" << std::endl;
    for (auto& e : event.read()) {
        std::cout << "event: " << e.data << std::endl;
    }
    std::cout << std::endl;
}

bool check_if_event_exist(EventReader<TestEvent> event) {
    std::cout << "check_if_event_exist" << std::endl;
    bool exist = !event.empty();
    std::cout << "exist: " << std::boolalpha << exist << std::endl;
    std::cout << std::endl;
    return exist;
}

class EventTestPlugin : public Plugin {
   public:
    void build(App& app) override {
        app.add_event<TestEvent>();
        app.add_system(Startup, into(write_event_s))
            ->add_system(Startup, into(read_event_s).after(write_event_s))
            ->add_system(Update, read_event_u)
            ->add_system(Update, into(write_event_u).after(read_event_u))
            ->add_system(Update, into(read_event_u2).after(write_event_u));
    }
};

void test() {
    App app = App::create();
    app.add_plugin(EventTestPlugin{}).add_event<TestEvent>().run();
}
}  // namespace test_event