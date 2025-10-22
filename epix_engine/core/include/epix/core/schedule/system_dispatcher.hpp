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
    smallvec();
    smallvec(const smallvec& other);
    smallvec(smallvec&& other) noexcept;
    smallvec& operator=(const smallvec& other) noexcept;
    smallvec& operator=(smallvec&& other) noexcept;
    ~smallvec();

    void push_back(size_t value);
    size_t pop_back();

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
    void push(size_t index);
    smallvec pop();
    smallvec try_pop();
    size_t size() const;
    bool empty() const;
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
    std::unique_ptr<World> world =
        nullptr;  // keep ownership of world, so that during dispatch the access is thread-safe
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
    void tick();
    void finish(size_t index);

    void assert_world() {
        if (!world) {
            throw std::runtime_error("SystemDispatcher: world is null.");
        }
    }

   public:
    SystemDispatcher(std::unique_ptr<World> world,
                     size_t thread_count = std::clamp(std::thread::hardware_concurrency(), 2u, 8u))
        : world(std::move(world)),
          thread_pool(thread_count),
          world_scope_system(system::make_system(
              [](system::In<std::function<void(World&)>> input, World& world) { input.get()(world); })) {
        world_scope_access = world_scope_system->initialize(*this->world);
    }
    ~SystemDispatcher();
    void wait() {
        auto res = world_scope([](World& world) {}).get();
    }
    std::unique_ptr<World> release_world() {
        std::lock_guard lock(mutex_);
        return std::move(world);
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
            assert_world();
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
        assert_world();
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