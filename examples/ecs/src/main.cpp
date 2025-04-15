#pragma once

#include <epix/prelude.h>

#include <iostream>
#include <random>

#include "basic_loop.h"
#include "event_test_with_condition_system.h"
#include "general_spawn_despawn.h"
#include "parallel_run_test.h"
#include "resource_test.h"
#include "runtime_system_adding_removal.h"
#include "spawn_with_child.h"
#include "state_nextstate.h"

struct test_t {
    int* a;
    test_t() { std::cout << "Constructor called" << std::endl; }
    test_t(int* a) : a(a) { std::cout << "Constructor called" << std::endl; }
    test_t(const test_t&) = delete;
    test_t(test_t&&) { std::cout << "Move constructor called" << std::endl; }
    test_t& operator=(const test_t&) = delete;
    test_t& operator=(test_t&&) {
        std::cout << "Move assignment called" << std::endl;
        return *this;
    }
    ~test_t() { std::cout << "Destructor called" << std::endl; }
};

int main() {
    std::cout << "==TEST GENERAL SPAWN DESPAWN===" << std::endl;
    test_spawn_despawn::test();
    std::cout << "=====TEST SPAWN WITH CHILD=====" << std::endl;
    test_with_child::test();
    std::cout << "==========TEST RESOURCE========" << std::endl;
    test_resource::test();
    std::cout << "===========TEST EVENT==========" << std::endl;
    test_event::test();
    std::cout << "===========TEST BASIC LOOP==========" << std::endl;
    test_loop::test();
    std::cout << "===========TEST STATE NEXTSTATE==========" << std::endl;
    test_state::test();
    std::cout << "===========TEST PARALLEL RUN==========" << std::endl;
    test_parallel_run::test();
    std::cout << "==========TEST RUNTIME SYSTEM ADD========" << std::endl;
    test_rt_system::test();

    return 0;
}