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
    std::unique_ptr<World> world_own =
        nullptr;  // keep ownership of world, so that during dispatch the access is thread-safe
    World* world;
    std::recursive_mutex mutex_;
    size_t running = 0;
    std::condition_variable_any cv_;
    BS::thread_pool<BS::tp::none>* thread_pool;
    system::SystemUnique<system::In<std::move_only_function<void(World&)>>> world_scope_system;
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
    struct ThreadPoolWrapper : public BS::thread_pool<BS::tp::none> {
        using BS::thread_pool<BS::tp::none>::thread_pool;
    };
    BS::thread_pool<BS::tp::none>& get_thread_pool(size_t thread_count) {
        return world->resource_or_emplace<ThreadPoolWrapper>(thread_count);
    }

   public:
    SystemDispatcher(std::unique_ptr<World> world,
                     size_t thread_count = std::clamp(std::thread::hardware_concurrency(), 2u, 8u))
        : world(world.get()),
          world_own(std::move(world)),
          world_scope_system(system::make_system(
              [](system::In<std::move_only_function<void(World&)>> input, World& world) { input.get()(world); })) {
        this->world        = this->world_own.get();
        world_scope_access = world_scope_system->initialize(*this->world);
        thread_pool        = &get_thread_pool(thread_count);
    }
    SystemDispatcher(World& world, size_t thread_count = std::clamp(std::thread::hardware_concurrency(), 2u, 8u))
        : world(&world),
          world_scope_system(system::make_system(
              [](system::In<std::move_only_function<void(World&)>> input, World& world) { input.get()(world); })) {
        world_scope_access = world_scope_system->initialize(*this->world);
        thread_pool        = &get_thread_pool(thread_count);
    }
    ~SystemDispatcher();
    void wait() {
        std::unique_lock lock(mutex_);
        if (world) cv_.wait(lock, [this] { return running == 0 && pending_systems.empty(); });
    }
    /**
     * @brief Release the world if owned. This will make further
     *
     * @return std::unique_ptr<World> The owned world, or nullptr if not owned.
     */
    std::unique_ptr<World> release_world();

    auto world_scope(std::invocable<World&> auto&& func, DispatchConfig config = {})
        -> std::future<
            std::expected<typename system::function_traits<decltype(func)>::return_type, system::RunSystemError>> {
        std::packaged_task<std::expected<typename system::function_traits<decltype(func)>::return_type,
                                         system::RunSystemError>(World&)>
            task([func = std::forward<decltype(func)>(func)](World& world) mutable {
                using return_t = typename system::function_traits<decltype(func)>::return_type;
                if constexpr (std::is_void_v<return_t>) {
                    func(world);
                    return std::expected<return_t, system::RunSystemError>{};
                } else {
                    return std::expected<return_t, system::RunSystemError>{func(world)};
                }
            });
        auto fut = task.get_future();
        dispatch_system(*world_scope_system, std::move(task), world_scope_access, std::move(config));
        return fut;
    }
    // Caller has to guarantee that the system and access pointers are valid during the execution
    template <typename In, typename Out>
    std::future<std::expected<Out, system::RunSystemError>> dispatch_system(system::System<In, Out>& sys,
                                                                            system::SystemInput<In>::Input input,
                                                                            const query::FilteredAccessSet& access,
                                                                            DispatchConfig config = {}) {
        std::packaged_task<std::expected<Out, system::RunSystemError>()> task(
            [this, &sys, input = std::move(input)]() mutable { return sys.run_no_apply(std::move(input), *world); });
        auto fut = task.get_future();
        {
            std::lock_guard lock(mutex_);
            assert_world();
            running++;
            pending_systems.emplace_back(
                &access, config,
                [task = std::move(task), on_finished = std::move(config.on_finish), this](size_t index) mutable {
                    task();
                    if (on_finished) on_finished();
                    finish(index);
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
    std::future<std::expected<void, system::RunSystemError>> apply_deferred(system::System<In, Out>& sys,
                                                                            DispatchConfig config = {}) {
        return world_scope([&](World& world) { sys.apply_deferred(world); }, std::move(config));
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