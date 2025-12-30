module;

#ifdef EPIX_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#endif

#include <cstddef>
#include <mutex>
#include <vector>

module epix.core;

import BS.thread_pool;

import :schedule.dispatcher;

namespace core {
// smallvec
smallvec::smallvec() {}
smallvec::smallvec(const smallvec& other) : size_(other.size_) {
    if (other.is_small()) {
        std::copy(other.small_array, other.small_array + other.size_, small_array);
    } else {
        new (&large_array) std::vector<std::size_t>(other.large_array);
    }
}
smallvec::smallvec(smallvec&& other) noexcept : size_(other.size_) {
    if (other.is_small()) {
        std::copy(other.small_array, other.small_array + other.size_, small_array);
    } else {
        new (&large_array) std::vector<std::size_t>(std::move(other.large_array));
        other.large_array.~vector();
    }
    other.size_ = 0;
}
smallvec& smallvec::operator=(const smallvec& other) noexcept {
    if (this != &other) {
        if (is_small() && other.is_small()) {
            std::copy(other.small_array, other.small_array + other.size_, small_array);
        } else if (is_small() && !other.is_small()) {
            new (&large_array) std::vector<std::size_t>(other.large_array);
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
        if (other.is_small()) {
            if (!is_small()) {
                large_array.~vector();
            }
            std::copy(other.small_array, other.small_array + other.size_, small_array);
        } else {
            if (is_small()) {
                new (&large_array) std::vector<std::size_t>(std::move(other.large_array));
            } else {
                large_array = std::move(other.large_array);
            }
        }
        size_       = other.size_;
        other.size_ = 0;
    }
    return *this;
}
smallvec::~smallvec() {
    if (!is_small()) {
        large_array.~vector();
    }
}

void smallvec::push_back(std::size_t value) {
    if (is_small()) {
        if (size_ < 4) {
            small_array[size_++] = value;
        } else {
            // move to large vector
            std::array<std::size_t, 4> temp;
            std::copy(small_array, small_array + 4, temp.data());
            new (&large_array) std::vector<std::size_t>(temp.begin(), temp.end());
            large_array.push_back(value);
            size_++;
        }
    } else {
        large_array.push_back(value);
        size_++;
    }
}
std::size_t smallvec::pop_back() {
    if (is_small()) {
        return small_array[--size_];
    } else {
        size_--;
        std::size_t value = large_array.back();
        large_array.pop_back();
        if (size_ <= 4) {
            std::size_t cache[4];
            for (std::size_t i = 0; i < size_; i++) {
                cache[i] = large_array[i];
            }
            large_array.~vector();
            std::copy(cache, cache + size_, small_array);
        }
        return value;
    }
}

// async_queue
void async_queue::push(std::size_t index) {
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

std::size_t async_queue::size() const {
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
        std::size_t index      = get_index();
        system_accesses[index] = access;
        thread_pool->detach_task([this, func = std::move(func), index, config = std::move(config)]() mutable {
            if (!config.enable_tracy) {
                func();
            } else {
#ifdef EPIX_ENABLE_TRACY
                ZoneScopedN("Run dispatched system");
                if (config.debug_name && config.debug_name->size() > 0) {
                    ZoneName(config.debug_name->data(), config.debug_name->size());
                }
#endif
                func();
            }
            if (config.on_finish) config.on_finish();
            finish(index);
        });
        pending_systems.pop_front();
    }
}
void SystemDispatcher::finish(std::size_t index) {
    // collect finished system
    std::lock_guard lock(mutex_);
    const auto& access     = system_accesses[index];
    system_accesses[index] = nullptr;
    free_indices.push_back(index);
    running--;
    cv_.notify_all();
    tick();
}
BS::thread_pool<BS::tp::none>& SystemDispatcher::get_thread_pool(std::size_t thread_count) {
    return world->resource_or_emplace<SystemDispatcher::ThreadPoolWrapper>(
        thread_count, []() { BS::this_thread::set_os_thread_name("system"); });
}

SystemDispatcher::~SystemDispatcher() {
    // dispatch a world scope system to wait for all systems to finish.
    if (world) world_scope([](World& world) {}).wait();
}

std::unique_ptr<World> SystemDispatcher::release_world() {
    std::unique_lock lock(mutex_);
    if (world) cv_.wait(lock, [this] { return running == 0 && pending_systems.empty(); });
    if (!world_own) return nullptr;
    world = nullptr;
    return std::move(world_own);
}
}  // namespace core