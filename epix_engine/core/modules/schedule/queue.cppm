module;

export module epix.core:schedule.queue;

import std;
import epix.traits;
import BS.thread_pool;

import :query;
import :system;
import :world;
import :tick;

namespace epix::core {
struct smallvec : std::ranges::view_interface<smallvec> {
   public:
    smallvec();
    smallvec(const smallvec& other);
    smallvec(smallvec&& other) noexcept;
    smallvec& operator=(const smallvec& other) noexcept;
    smallvec& operator=(smallvec&& other) noexcept;
    ~smallvec();

    void push_back(std::size_t value);
    std::size_t pop_back();

    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }
    std::size_t* begin() {
        if (is_small()) {
            return &small_array[0];
        } else {
            return large_array.data();
        }
    }
    std::size_t* end() {
        if (is_small()) {
            return &small_array[size_];
        } else {
            return large_array.data() + large_array.size();
        }
    }

   private:
    union {
        std::size_t small_array[4];
        std::vector<std::size_t> large_array;
    };
    std::size_t size_ = 0;

    bool is_small() const { return size_ <= 4; }
};
struct async_queue {
    mutable std::mutex mutex;
    std::condition_variable condition;
    smallvec queue;
    void push(std::size_t index);
    smallvec pop();
    smallvec try_pop();
    std::size_t size() const;
    bool empty() const;
};
/** @brief Thread pool resource stored in World for parallel schedule execution. */
export struct ScheduleThreadPool {
    BS::thread_pool<BS::tp::none> pool;
    ScheduleThreadPool()
        : pool(std::thread::hardware_concurrency(), []() { BS::this_thread::set_os_thread_name("system"); }) {}
};
}  // namespace epix::core