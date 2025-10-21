#include "epix/core/schedule/system_dispatcher.hpp"

using namespace epix::core::schedule;

// smallvec
smallvec::smallvec() {}
smallvec::smallvec(const smallvec& other) : size_(other.size_) {
    if (other.is_small()) {
        std::copy(other.small_array, other.small_array + other.size_, small_array);
    } else {
        new (&large_array) std::vector<size_t>(other.large_array);
    }
}
smallvec::smallvec(smallvec&& other) noexcept : size_(other.size_) {
    if (other.is_small()) {
        std::copy(other.small_array, other.small_array + other.size_, small_array);
    } else {
        new (&large_array) std::vector<size_t>(std::move(other.large_array));
        other.large_array.~vector();
    }
    other.size_ = 0;
}
smallvec& smallvec::operator=(const smallvec& other) noexcept {
    if (this != &other) {
        if (is_small() && other.is_small()) {
            std::copy(other.small_array, other.small_array + other.size_, small_array);
        } else if (is_small() && !other.is_small()) {
            new (&large_array) std::vector<size_t>(other.large_array);
        } else if (!is_small() && other.is_small()) {
            large_array.~vector();
            std::copy(other.small_array, other.small_array + other.size_, small_array);
        } else {
            large_array = other.large_array;
        }
        size_ = other.size_;
    }
    return *this;
}
smallvec& smallvec::operator=(smallvec&& other) noexcept {
    if (this != &other) {
        this->~smallvec();
        new (this) smallvec(std::move(other));
    }
    return *this;
}
smallvec::~smallvec() {
    if (!is_small()) {
        large_array.~vector();
    }
}

void smallvec::push_back(size_t value) {
    if (is_small()) {
        if (size_ < 4) {
            small_array[size_++] = value;
        } else {
            // move to large vector
            new (&large_array) std::vector<size_t>(small_array, small_array + size_);
            large_array.push_back(value);
            size_++;
        }
    } else {
        large_array.push_back(value);
        size_++;
    }
}
size_t smallvec::pop_back() {
    if (is_small()) {
        return small_array[--size_];
    } else {
        size_--;
        size_t value = large_array.back();
        large_array.pop_back();
        if (size_ <= 4) {
            size_t cache[4];
            for (size_t i = 0; i < size_; i++) {
                cache[i] = large_array[i];
            }
            large_array.~vector();
            std::copy(cache, cache + size_, small_array);
        }
        return value;
    }
}

// async_queue
void async_queue::push(size_t index) {
    std::unique_lock<std::mutex> lock(mutex);
    queue.push_back(index);
    condition.notify_one();
}

smallvec async_queue::pop() {
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [this] { return !queue.empty(); });
    return std::move(queue);
}
smallvec async_queue::try_pop() {
    std::unique_lock<std::mutex> lock(mutex);
    return std::move(queue);
}

size_t async_queue::size() const {
    std::unique_lock<std::mutex> lock(mutex);
    return queue.size();
}

bool async_queue::empty() const {
    std::unique_lock<std::mutex> lock(mutex);
    return queue.empty();
}

// SystemDispatcher impl
void SystemDispatcher::tick() {
    std::lock_guard lock(mutex_);
    while (!pending_systems.empty()) {
        auto&& [access, config, func] = pending_systems.front();
        // check for conflicts
        bool conflict = false;
        for (const auto& existing_access : system_accesses) {
            if (existing_access && !access->is_compatible(*existing_access)) {
                conflict = true;
                break;
            }
        }
        if (conflict) break;
        // can schedule
        size_t index           = get_index();
        system_accesses[index] = access;
        thread_pool.detach_task([func = std::move(func), index] mutable { return func(index); });
        pending_systems.pop_front();
    }
}
void SystemDispatcher::finish(size_t index) {
    // collect finished system
    {
        std::lock_guard lock(mutex_);
        const auto& access     = system_accesses[index];
        system_accesses[index] = nullptr;
        free_indices.push_back(index);
    }
    tick();
}

SystemDispatcher::~SystemDispatcher() {
    // dispatch a world scope system to wait for all systems to finish.
    world_scope([](World& world) {}).wait();
}
