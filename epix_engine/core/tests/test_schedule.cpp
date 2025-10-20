#include <cassert>
#include <iostream>

#include "epix/core/schedule/schedule.hpp"
#include "epix/core/system/system.hpp"
#include "epix/core/type_system/type_registry.hpp"
#include "epix/core/world.hpp"

using namespace epix::core;
using namespace epix::core::schedule;
using namespace epix::core::system;

int main() {
    auto registry = std::make_shared<type_system::TypeRegistry>();
    World world(WorldId(1), std::move(registry));

    Schedule sched;

    // Create three named lambdas and reuse them so the SystemSetLabel constructed
    // from the same callable type/value will match.
    auto af = []() {};
    auto bf = []() {};
    auto cf = []() {};

    SetConfig sa = make_sets(af);
    SetConfig sb = make_sets(bf);
    SetConfig sc = make_sets(cf);

    sa.set_name("A");
    sb.set_name("B");
    sc.set_name("C");

    // Build config: C has parents A and B
    sc.in_set(af);
    sc.in_set(bf);

    // Add a dependency: A depends on B (A -> B)
    sa.after(bf);

    // Add configs to schedule (SetConfig is non-copyable, move it)
    sched.configure_sets(std::move(sa));
    sched.configure_sets(std::move(sb));
    sched.configure_sets(std::move(sc));

    // Prepare should detect parents with dependencies and return error
    auto res = sched.prepare(true);
    assert(!res.has_value());
    auto err = res.error();
    assert(err.type == SchedulePrepareError::Type::ParentsWithDeps);
    std::cout << "test_schedule (conflict) passed\n";

    // Non-conflicting case: D has parents E and F with no deps between them
    Schedule sched2;
    auto ef      = []() {};
    auto ff      = []() {};
    auto df      = []() {};
    SetConfig se = make_sets(ef);
    SetConfig sf = make_sets(ff);
    SetConfig sd = make_sets(df);
    sd.in_set(ef);
    sd.in_set(ff);
    sched2.configure_sets(std::move(se));
    sched2.configure_sets(std::move(sf));
    sched2.configure_sets(std::move(sd));
    auto res2 = sched2.prepare(true);
    assert(res2.has_value());
    std::cout << "test_schedule (no conflict) passed\n";

    // Now test execution order using a simple chain: B -> A -> C
    Schedule exec_sched;
    auto order = std::make_shared<std::vector<int>>();
    auto mtx   = std::make_shared<std::mutex>();

    auto bf_exec = [order, mtx]() {
        std::lock_guard lock(*mtx);
        order->push_back(1);
        std::cout << "ran B\n";
    };
    auto af_exec = [order, mtx]() {
        std::lock_guard lock(*mtx);
        order->push_back(2);
        std::cout << "ran A\n";
    };
    auto cf_exec = [order, mtx]() {
        std::lock_guard lock(*mtx);
        order->push_back(3);
        std::cout << "ran C\n";
    };

    SetConfig sb_exec = epix::into(bf_exec);
    SetConfig sa_exec = epix::into(af_exec);
    SetConfig sc_exec = epix::into(cf_exec);

    // A depends on B, C depends on A
    sa_exec.after(SystemSetLabel(bf_exec));
    sc_exec.after(SystemSetLabel(af_exec));

    exec_sched.add_systems(std::move(sb_exec));
    exec_sched.add_systems(std::move(sa_exec));
    exec_sched.add_systems(std::move(sc_exec));

    auto pres = exec_sched.prepare(true);
    assert(pres.has_value());

    // Create dispatcher and execute
    auto registry2 = std::make_shared<type_system::TypeRegistry>();
    World world2(WorldId(2), std::move(registry2));
    schedule::SystemDispatcher dispatcher(world2, 2);
    exec_sched.initialize_systems(world);
    exec_sched.execute(dispatcher);
    std::cout << "after execute, order size = " << order->size() << "\n";

    // After execution order should be B(1), A(2), C(3)
    assert(order->size() == 3);
    assert(order->at(0) == 1);
    assert(order->at(1) == 2);
    assert(order->at(2) == 3);
    std::cout << "test_schedule execution order passed\n";

    return 0;
}
