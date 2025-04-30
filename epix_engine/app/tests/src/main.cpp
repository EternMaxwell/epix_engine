#include <epix/app/app.h>
#include <epix/app/commands.h>
#include <epix/app/query.h>
#include <epix/app/schedule.h>
#include <epix/app/system.h>
#include <epix/app/systemparam.h>
#include <epix/app/world.h>
#include <epix/app/world_data.h>

#include <iostream>
#include <vector>

using namespace epix::app;

struct Command1 {
    int a;
    float b;
    static void apply(epix::app::World& world, const Command1& command) {
        std::cout << "Command1: " << command.a << ", " << command.b
                  << std::endl;
    }
    ~Command1() { std::cout << "Command1 destructor called" << std::endl; }
};

struct Command2 {
    std::string str;
    uint8_t padding;
    void apply(epix::app::World& world) {
        std::cout << "Command2: " << str << std::endl;
    }
    ~Command2() { std::cout << "Command2 destructor called" << std::endl; }
};

struct Command3 {
    std::vector<int> vec;
    void apply(epix::app::World& world) {
        std::cout << "Command3: ";
        for (const auto& i : vec) {
            std::cout << i << " ";
        }
        std::cout << std::endl;
    }
    ~Command3() { std::cout << "Command3 destructor called" << std::endl; }
};

struct Comp1 {
    int a;
    float b;
};
struct Comp2 {
    std::string str;
};
struct Comp3 {
    std::vector<int> vec;
};

struct bundle : epix::app::Bundle {
    int a;
    float b;
    std::string str;
    std::vector<int> vec;
    auto unpack() {
        return std::make_tuple(Comp1{a, b}, Comp2{str}, Comp3{vec});
    }
};

struct Testparam {
    Res<int> res;
    Query<Get<Comp1, Opt<Comp2>>, Filter<With<Comp3>>> q;
    Local<int> local;
    static Testparam from_param(
        Res<int> res,
        Query<Get<Comp1, Opt<Comp2>>, Filter<With<Comp3>>> q,
        Local<int> local
    ) {
        return Testparam{res, q, local};
    }
};
struct Testparam2 {
    Testparam param;
    Res<double> res;
    static Testparam2 from_param(
        std::optional<Extract<Testparam>> t1, const Res<double>& t2
    ) {
        return Testparam2{t1.value(), t2};
    }
};

void system1(Testparam2 param, World&, Extract<World>) {
    auto& testp2 = param;
    for (auto&& [comp1, opt_comp2] : testp2.param.q.iter()) {
        std::cout << "Comp1: " << comp1.a << ", " << comp1.b << std::endl;
        if (opt_comp2) {
            std::cout << "Comp2: " << opt_comp2->str << std::endl;
        } else {
            std::cout << "Comp2: null" << std::endl;
        }
    }
    std::cout << "Res: " << *testp2.res << std::endl;
    std::cout << "Local: " << *testp2.param.local << std::endl;
    std::cout << "Res2: " << *param.res << std::endl;
}

void system2() { std::cout << "System2 called" << std::endl; }

void system3(Res<bool>) { std::cout << "System3 called" << std::endl; }

void system4() { std::cout << "System4 called" << std::endl; }

void system5(EventWriter<AppExit>& exit) {
    std::cout << "System5 called" << std::endl;
    exit.write(AppExit{});
}

struct TestScheduleT {
} TestSchedule;
enum TestSets { Set1, Set2 };

int main() {
    App app         = App::create();
    World& worldsrc = app.world(MainWorld);
    worldsrc.insert_resource<int>(42);
    worldsrc.spawn(Comp1{1, 2.0f}, Comp3{{1, 2, 3}});
    worldsrc.spawn(Comp1{2, 3.0f}, Comp2{"Hello"}, Comp3{{4, 5, 6}});
    World& worlddst = app.world(MainWorld);
    worlddst.insert_resource<double>(3.14);
    Schedule& schedule = app.schedule(Update);
    schedule.add_systems(into(
        into(into(system1).after(system2), system2)
            .in_set(Set1)
            .set_name(0, "system1"),
        into(system3, system4).in_set(Set2)
    ));
    schedule.configure_sets(sets(Set2, Set1).chain());
    // schedule.build();
    // test if remove functions still make the schedule complete without
    // rebuild.
    // schedule.remove_set(Set1);
    // schedule.remove_system(system2);
    app.add_systems(Last, into(system5));
    app.add_plugins(LoopPlugin{});
    app.logger()->set_level(spdlog::level::debug);
    app.set_logger(app.logger());
    app.run();
    return 0;
}