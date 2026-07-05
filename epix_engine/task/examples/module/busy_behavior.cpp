// Busy behavior — module build variant

#ifndef EPIX_IMPORT_STD
#include <chrono>
#include <cstddef>
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
    auto pool = TaskPoolBuilder{}.num_threads(4).thread_name("Busy Behavior").build();

    auto t0 = steady_clock::now();

    pool.scope<int>([](Scope<int>& s) {
        for (int i = 0; i < 40; ++i) {
            s.spawn([i]() -> int {
                auto start = steady_clock::now();
                while (steady_clock::now() - start < milliseconds(100)) {}
                std::cout << "Thread " << std::this_thread::get_id() << " index " << i << " finished\n";
                return i;
            });
        }
    });

    auto t1   = steady_clock::now();
    auto secs = duration<float>(t1 - t0).count();
    std::cout << "all tasks finished in " << secs << " secs\n";

    return 0;
}
