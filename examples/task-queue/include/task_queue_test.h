﻿#pragma once

#include <pixel_engine/prelude.h>

namespace test_queue_test {
using namespace pixel_engine;
using namespace prelude;
using namespace task_queue;

void test_fun(ResMut<TaskQueue> task_queue) {
    auto pool = task_queue->get_pool();
    pool->detach_task([]() {
        for (int i = 0; i < 10; i++) std::cout << "Hello, 1!\n";
    });
    pool->detach_task([]() {
        for (int i = 0; i < 10; i++) std::cout << "Hello, 2!\n";
    });
    pool->wait();
}

class TestPlugin : public Plugin {
   public:
    TestPlugin() {}

    void build(App& app) { app.add_system(Update, test_fun); }
};
}  // namespace test_queue_test