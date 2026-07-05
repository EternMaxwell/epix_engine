// Idle behavior — module build variant

#ifndef EPIX_IMPORT_STD
#include <chrono>
#include <iostream>
#include <thread>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.task;

using namespace epix::tasks;
using namespace std::chrono;

int main() {
    auto pool = TaskPoolBuilder{}.thread_name("Idle Behavior").build();

    pool.scope<int>([](Scope<int>& s) {
        for (int i = 0; i < 1; ++i) {
            s.spawn([i]() -> int {
                std::cout << "Blocking for 10 seconds\n";
                auto start = steady_clock::now();
                while (steady_clock::now() - start < seconds(10)) {}
                std::cout << "Thread " << std::this_thread::get_id() << " index " << i << " finished\n";
                return i;
            });
        }
    });

    std::cout << "all tasks finished\n";
    return 0;
}
