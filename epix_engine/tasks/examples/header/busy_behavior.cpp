// Busy behavior — creates 4-thread pool, spawns 40 tasks each spinning 100ms.
// With 4 threads it should finish in ~1 second.

#include <chrono>
#include <cstddef>
#include <epix/tasks.hpp>
#include <iostream>
#include <thread>


using namespace epix::tasks;
using namespace std::chrono;

int main() {
    auto pool = TaskPoolBuilder{}.num_threads(4).thread_name("Busy Behavior").build();

    auto t0 = steady_clock::now();

    pool.scope<int>([](Scope<int>& s) {
        for (int i = 0; i < 40; ++i) {
            s.spawn([i]() -> int {
                auto start = steady_clock::now();
                while (steady_clock::now() - start < milliseconds(100)) {
                    // spin, simulating work
                }
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
