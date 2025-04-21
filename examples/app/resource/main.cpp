#include <epix/app.h>

using namespace epix;

struct Resource {
    int data = 0;
};

struct Resource2 {
    int data = 0;
};

struct Resource3 {
    int data = 0;
};

void insert_resource(Command command) {
    spdlog::info("Inserting resources...");
    command.init_resource<Resource>();
    command.insert_resource(Resource2{});
    command.emplace_resource<Resource3>(100);
}

void check_resource(
    Res<Resource> res, Res<Resource2> res2, Res<Resource3> res3
) {
    bool any_absent = false;
    if (!res) {
        spdlog::error("Resource not found!");
        any_absent = true;
    }
    if (!res2) {
        spdlog::error("Resource2 not found!");
        any_absent = true;
    }
    if (!res3) {
        spdlog::error("Resource3 not found!");
        any_absent = true;
    }
    if (any_absent) {
        return;
    }
    spdlog::info(
        "Resource data: {}, {}, {}", res->data, res2->data, res3->data
    );
}

void modify_resource(
    ResMut<Resource> res, ResMut<Resource2> res2, ResMut<Resource3> res3
) {
    spdlog::info("Modifying resources...");
    res->data += 1;
    res2->data += 2;
    res3->data += 3;
}

void remove_resource(Command command) {
    spdlog::info("Removing resources...");
    command.remove_resource<Resource>();
    command.remove_resource<Resource2>();
    command.remove_resource<Resource3>();
}

/**
 * @brief Epix App framework resource adding, removing, and checking example.
 *
 * In this example, you might also see that the sequence of output of check and
 * remove system in Update schedule may differ in two runs. This is because the
 * order of system execution of systems in same schedule and have no dependency
 * is not guaranteed, and will be runned parrallely.
 */
int main() {
    {
        App app = App::create();
        app.init_resource<Resource>();
        app.insert_resource<Resource2>(Resource2{});
        app.emplace_resource<Resource3>(100);
        app.add_system(Startup, into(check_resource, modify_resource).chain())
            .add_system(Update, into(check_resource, remove_resource));
        app.add_system(Exit, check_resource);
        app.run();
    }
    std::cout << "------------------------" << std::endl;
    {
        App app = App::create();
        app.add_system(
               Startup,
               into(insert_resource, check_resource, modify_resource).chain()
        )
            .add_system(Update, into(check_resource, remove_resource));
        app.add_system(Exit, check_resource);
        app.run();
    }
}