#include <epix/prelude.h>

#include <format>
#include <iomanip>
#include <iostream>
#include <random>

namespace test_resource {
using namespace epix;
using namespace prelude;

struct Res {
    int data;
};

void access_resource1(ResMut<Res> resource) {
    std::cout << "access_resource" << std::endl;
    if (resource.has_value()) {
        std::cout << "resource: " << resource->data << std::endl;
    } else {
        std::cout << "resource: null" << std::endl;
    }
    std::cout << std::endl;
}

void access_resource2(ResMut<Res> resource) {
    std::cout << "access_resource" << std::endl;
    if (resource.has_value()) {
        std::cout << "resource: " << resource->data << std::endl;
    } else {
        std::cout << "resource: null" << std::endl;
    }
    std::cout << std::endl;
}

void access_resource3(ResMut<Res> resource) {
    std::cout << "access_resource" << std::endl;
    if (resource.has_value()) {
        std::cout << "resource: " << resource->data << std::endl;
    } else {
        std::cout << "resource: null" << std::endl;
    }
    std::cout << std::endl;
}

void set_resource(Command command) {
    std::cout << "set_resource" << std::endl;
    command.insert_resource(Res{.data = 100});
    std::cout << std::endl;
}

void set_resource_with_init(Command command) {
    std::cout << "set_resource_with_init" << std::endl;
    command.init_resource<Res>();
    std::cout << std::endl;
}

void remove_resource1(Command command) {
    std::cout << "remove_resource" << std::endl;
    command.remove_resource<Res>();
    std::cout << std::endl;
}

void remove_resource2(Command command) {
    std::cout << "remove_resource" << std::endl;
    command.remove_resource<Res>();
    std::cout << std::endl;
}

void change_resource(ResMut<Res> resource) {
    std::cout << "change_resource" << std::endl;
    resource->data = 200;
    std::cout << std::endl;
}

class ResourceTestPlugin : public Plugin {
   public:
    void build(App& app) override {
        app.add_system(Startup, set_resource)
            ->add_system(Startup, access_resource1)
            .after(set_resource)
            ->add_system(Startup, remove_resource1)
            .after(access_resource1)
            ->add_system(Startup, set_resource_with_init)
            .after(remove_resource1)
            ->add_system(Startup, access_resource2)
            .after(set_resource_with_init)
            ->add_system(Startup, change_resource)
            .after(access_resource2)
            ->add_system(Startup, access_resource3)
            .after(change_resource)
            ->add_system(Startup, remove_resource2)
            .after(access_resource3);
    }
};

void test() {
    App app = App::create();
    app.add_plugin(ResourceTestPlugin{}).run();
}
}  // namespace test_resource