#pragma once

#include <concepts>
#include <condition_variable>
#include <deque>
#include <expected>
#include <functional>
#include <future>
#include <mutex>
#include <ranges>
#include <vector>

#define BS_THREAD_POOL_NATIVE_EXTENSIONS
#include <BS_thread_pool.hpp>

#include "../query/access.hpp"
#include "../system/system.hpp"

namespace epix::core::schedule {
struct smallvec : std::ranges::view_interface<smallvec> {
   public:
    smallvec() {}
    smallvec(const smallvec& other) : size_(other.size_) {
        if (other.is_small()) {
            std::copy(other.small_array, other.small_array + other.size_, small_array);
        } else {
            new (&large_array) std::vector<size_t>(other.large_array);
        }
    }
    smallvec(smallvec&& other) noexcept : size_(other.size_) {
        if (other.is_small()) {
            std::copy(other.small_array, other.small_array + other.size_, small_array);
        } else {
            new (&large_array) std::vector<size_t>(std::move(other.large_array));
            other.large_array.~vector();
        }
        other.size_ = 0;
    }
    smallvec& operator=(const smallvec& other) noexcept {
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
    smallvec& operator=(smallvec&& other) noexcept {
        if (this != &other) {
            this->~smallvec();
            new (this) smallvec(std::move(other));
        }
        return *this;
    }
    ~smallvec() {
        if (!is_small()) {
            large_array.~vector();
        }
    }

    void push_back(size_t value) {
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
    size_t pop_back() {
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

    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }
    size_t* begin() {
        if (is_small()) {
            return &small_array[0];
        } else {
            return large_array.data();
        }
    }
    size_t* end() {
        if (is_small()) {
            return &small_array[size_];
        } else {
            return large_array.data() + large_array.size();
        }
    }

   private:
    union {
        size_t small_array[4];
        std::vector<size_t> large_array;
    };
    size_t size_ = 0;

    bool is_small() const { return size_ < 4; }
};
struct async_queue {
    mutable std::mutex mutex;
    std::condition_variable condition;
    smallvec queue;

    void push(size_t index) {
        std::unique_lock<std::mutex> lock(mutex);
        queue.push_back(index);
        condition.notify_one();
    }

    smallvec pop() {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return !queue.empty(); });
        return std::move(queue);
    }
    smallvec try_pop() {
        std::unique_lock<std::mutex> lock(mutex);
        return std::move(queue);
    }

    size_t size() const {
        std::unique_lock<std::mutex> lock(mutex);
        return queue.size();
    }

    bool empty() const {
        std::unique_lock<std::mutex> lock(mutex);
        return queue.empty();
    }
};
struct DispatchConfig {
    std::function<void()> on_finish = nullptr;
};
struct SystemDispatcher {
   private:
    std::vector<const query::FilteredAccessSet*> system_accesses;
    std::deque<size_t> free_indices;
    std::deque<std::tuple<const query::FilteredAccessSet*, DispatchConfig, std::move_only_function<void(size_t)>>>
        pending_systems;
    World* world = nullptr;
    std::recursive_mutex mutex_;
    BS::thread_pool<BS::tp::none> thread_pool;
    system::SystemUnique<system::In<std::function<void(World&)>>> world_scope_system;
    query::FilteredAccessSet world_scope_access;

    size_t get_index() {
        if (!free_indices.empty()) {
            size_t index = free_indices.front();
            free_indices.pop_front();
            return index;
        } else {
            size_t index = system_accesses.size();
            system_accesses.push_back(nullptr);
            return index;
        }
    }
    void tick() {
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
    void finish(size_t index) {
        // collect finished system
        {
            std::lock_guard lock(mutex_);
            const auto& access     = system_accesses[index];
            system_accesses[index] = nullptr;
            free_indices.push_back(index);
        }
        tick();
    }

   public:
    SystemDispatcher(World& world_ref, size_t thread_count = std::clamp(std::thread::hardware_concurrency(), 2u, 8u))
        : world(&world_ref),
          thread_pool(thread_count),
          world_scope_system(system::make_system(
              [](system::In<std::function<void(World&)>> input, World& world) { input.get()(world); })) {
        // initializing this system accesses mut world but does not read or write to it
        world_scope_access = world_scope_system->initialize(world_ref);
    }
    ~SystemDispatcher() {
        // we do not need to explicitly join the thread pool, as it will be joined in its destructor, but we
        // shell be prepared for future changes which might use external thread pools that is not destructed here.

        // since the submit order is preserved, and a World& access is not compatible with any other access.
        // just dispatch a world scope system to wait for all systems to finish.
        world_scope([](World& world) {}).wait();
    }

    std::future<std::expected<void, system::RunSystemError>> world_scope(std::invocable<World&> auto&& func) {
        return dispatch_system(*world_scope_system, std::function(func), world_scope_access);
    }
    // Caller has to guarantee that the system and access pointers are valid during the execution
    template <typename In, typename Out>
    std::future<std::expected<Out, system::RunSystemError>> dispatch_system(system::System<In, Out>& sys,
                                                                            system::SystemInput<In>::Input input,
                                                                            const query::FilteredAccessSet& access,
                                                                            DispatchConfig config = {}) {
        std::packaged_task<std::expected<Out, system::RunSystemError>()> task(
            [this, &sys, input = std::move(input), access]() mutable {
                return sys.run_no_apply(std::move(input), *world);
            });
        auto fut = task.get_future();
        {
            std::lock_guard lock(mutex_);
            pending_systems.emplace_back(
                &access, config,
                [task = std::move(task), on_finished = std::move(config.on_finish), this](size_t index) mutable {
                    task();
                    finish(index);
                    if (on_finished) on_finished();
                });
        }
        tick();
        return fut;
    }
    template <typename In, typename Out>
    std::optional<std::expected<Out, system::RunSystemError>> try_run_system(system::System<In, Out>& sys,
                                                                             system::SystemInput<In>::Input input,
                                                                             const query::FilteredAccessSet& access) {
        std::lock_guard lock(mutex_);
        // check for conflicts
        for (const auto& existing_access : system_accesses) {
            if (existing_access && !access.is_compatible(*existing_access)) {
                return std::nullopt;
            }
        }
        // run on current thread
        return sys.run_no_apply(std::move(input), *world);
    }
    template <typename In, typename Out>
    std::future<std::expected<void, system::RunSystemError>> apply_deferred(system::System<In, Out>& sys) {
        return world_scope([&](World& world) { sys.apply_deferred(world); });
    }
    template <typename T>
    std::future<std::expected<void, system::RunSystemError>> apply_deferred(T&& range)
        requires std::ranges::viewable_range<T> && requires(std::ranges::range_reference_t<T> ref, World& world) {
            { ref.apply_deferred(world) } -> std::same_as<void>;
        }
    {
        return world_scope([&](World& world) {
            for (auto&& sys : range) {
                sys.apply_deferred(world);
            }
        });
    }
};
}  // namespace epix::core::schedule