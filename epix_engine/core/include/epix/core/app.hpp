#pragma once

#include <spdlog/spdlog.h>

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "app/app_sche.hpp"
#include "app/schedules.hpp"
#include "schedule/schedule.hpp"
#include "world.hpp"

namespace epix::core {
EPIX_MAKE_LABEL(AppLabel)
struct WorldNotOwnedError {};
struct ScheduleOrder {
   public:
    /// Iterate over the schedule labels in order
    auto iter() const { return std::views::all(labels); }
    /// Insert at the beginning
    void insert_begin(const schedule::ScheduleLabel& label) { labels.insert(labels.begin(), label); }
    /// Insert at the end
    void insert_end(const schedule::ScheduleLabel& label) { labels.push_back(label); }
    /// Insert after a specific label, or at the end if not found
    void insert_after(const schedule::ScheduleLabel& after, const schedule::ScheduleLabel& label) {
        auto it = std::find(labels.begin(), labels.end(), after);
        if (it != labels.end()) it++;
        labels.insert(it, label);
    }
    /// Insert a sequence of labels after a specific label, or at the end if not found. New labels will be ignored if
    /// already present.
    template <typename Rng>
    void insert_range_after(schedule::ScheduleLabel after, Rng&& new_labels)
        requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, schedule::ScheduleLabel>
    {
        auto existing = labels | std::ranges::to<std::unordered_set<schedule::ScheduleLabel>>();
        // remove existing labels from new_labels from back to front to avoid invalidating indices
        for (auto it = new_labels.end(); it != new_labels.begin();) {
            --it;
            if (existing.contains(*it)) {
                new_labels.erase(it);
            }
        }
        auto it = std::find(labels.begin(), labels.end(), after);
        if (it != labels.end()) it++;
        labels.insert_range(it, std::forward<Rng>(new_labels));
    }
    template <typename Rng>
    void insert_range_end(Rng&& new_labels)
        requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, schedule::ScheduleLabel>
    {
        auto existing = labels | std::ranges::to<std::unordered_set<schedule::ScheduleLabel>>();
        // remove existing labels from new_labels from back to front to avoid invalidating indices
        for (auto it = new_labels.end(); it != new_labels.begin();) {
            --it;
            if (existing.contains(*it)) {
                new_labels.erase(it);
            }
        }
        labels.insert_range(labels.end(), std::forward<Rng>(new_labels));
    }
    template <typename Rng>
    void insert_range_begin(Rng&& new_labels)
        requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, schedule::ScheduleLabel>
    {
        auto existing = labels | std::ranges::to<std::unordered_set<schedule::ScheduleLabel>>();
        // remove existing labels from new_labels from back to front to avoid invalidating indices
        for (auto it = new_labels.end(); it != new_labels.begin();) {
            --it;
            if (existing.contains(*it)) {
                new_labels.erase(it);
            }
        }
        labels.insert_range(labels.begin(), std::forward<Rng>(new_labels));
    }
    /// Remove a label, return true if found and removed
    bool remove(const schedule::ScheduleLabel& label) {
        auto it = std::find(labels.begin(), labels.end(), label);
        if (it != labels.end()) {
            labels.erase(it);
            return true;
        }
        return false;
    }

   private:
    std::list<schedule::ScheduleLabel> labels;
};
struct App {
   public:
    App(const AppLabel& label                             = AppLabel::from_type<App>(),
        std::shared_ptr<std::atomic<uint32_t>> _world_ids = std::make_shared<std::atomic<uint32_t>>(0))
        : _label(label),
          _world(std::make_unique<World>(_world_ids->fetch_add(1))),
          _world_mutex(std::make_unique<std::recursive_mutex>()) {}
    ~App() { _dispatcher.reset(); }

    // === App Info and sub-apps ===

    /// Get the label of the app.
    AppLabel label() const { return _label; }
    /// Get or create a sub-app with the given label.
    App& sub_app_or_insert(const AppLabel& label) {
        auto&& [it, inserted] = _sub_apps.emplace(label, nullptr);
        if (inserted) {
            it->second = std::make_unique<App>(label, _world_ids);
        }
        return *it->second;
    }
    /// Add a sub-app. If a sub-app with the same label exists, nothing will happen.
    /// Unlike sub_app_or_insert, this will return the parent app.
    App& add_sub_app(const AppLabel& label) {
        auto&& [it, inserted] = _sub_apps.emplace(label, nullptr);
        if (inserted) {
            it->second = std::make_unique<App>(label, _world_ids);
        }
        return *this;
    }
    /// Try get a sub-app with the given label.
    std::optional<std::reference_wrapper<const App>> get_sub_app(const AppLabel& label) const {
        auto it = _sub_apps.find(label);
        if (it != _sub_apps.end()) {
            return *it->second;
        }
        return std::nullopt;
    }
    /// Try get a mutable sub-app with the given label.
    std::optional<std::reference_wrapper<App>> get_sub_app_mut(const AppLabel& label) {
        auto it = _sub_apps.find(label);
        if (it != _sub_apps.end()) {
            return *it->second;
        }
        return std::nullopt;
    }
    /// Get a const reference to a sub-app with the given label. Throws if not found.
    const App& sub_app(const AppLabel& label) const { return get_sub_app(label).value(); }
    /// Get a mutable reference to a sub-app with the given label. Throws if not found.
    App& sub_app_mut(const AppLabel& label) { return get_sub_app_mut(label).value(); }

    // === World Access ===

    /// Try to get a const reference to the world. Return error if the world is not owned.
    std::expected<std::reference_wrapper<const World>, WorldNotOwnedError> get_world() const {
        auto lock = lock_world();
        if (_world) {
            return *_world;
        }
        return std::unexpected(WorldNotOwnedError{});
    }
    /// Try to get a mutable reference to the world. Return error if the world is not owned.
    std::expected<std::reference_wrapper<World>, WorldNotOwnedError> get_world_mut() {
        auto lock = lock_world();
        if (_world) {
            return *_world;
        }
        return std::unexpected(WorldNotOwnedError{});
    }
    /// Get a const reference to the world. Throws if the world is not owned.
    const World& world() const { return get_world().value(); }
    /// Get a mutable reference to the world. Throws if the world is not owned.
    World& world_mut() { return get_world_mut().value(); }
    /// Execute a function with exclusive access to the world. Throw if the world is not owned.
    App& world_scope(std::invocable<World&> auto&& func) {
        auto lock = lock_world();
        func(world_mut());
        return *this;
    }
    /// Execute a function with exclusive access to the world's resources. Throw if the world is not owned.
    template <typename F>
    App& resource_scope(F&& func) {
        auto lock = lock_world();
        world_mut().resource_scope(std::forward<F>(func));
        return *this;
    }

    // === Schedule Access ===

    /// Add a schedule to the app. If a schedule with the same label exists, it will be replaced.
    App& add_schedule(schedule::Schedule&& schedule) {
        auto lock                 = lock_world();
        app::Schedules& schedules = world_mut().resource_or_init<app::Schedules>();
        // add or replace existing schedule
        schedules.add_schedule(std::move(schedule));
        return *this;
    }
    /// Add or replace systems in a schedule in the app. If the schedule does not exist, it will be created.
    App& add_systems(app::ScheduleInfo schedule, schedule::SetConfig&& config) {
        std::ranges::for_each(schedule.transforms, [&](auto&& transform) { transform(config); });
        resource_scope([&](app::Schedules& schedules, World& world) mutable {
            schedules.schedule_or_insert((schedule::ScheduleLabel&)schedule).add_systems(std::move(config));
        });
        return *this;
    }
    /// Configure sets in a schedule in the app. If the schedule does not exist, it will be created. If the sets exist,
    /// they will be replaced with the new configuration.
    App& configure_sets(app::ScheduleInfo schedule, schedule::SetConfig&& config) {
        std::ranges::for_each(schedule.transforms, [&](auto&& transform) { transform(config); });
        resource_scope([&](app::Schedules& schedules, World& world) mutable {
            schedules.schedule_or_insert((schedule::ScheduleLabel&)schedule).configure_sets(std::move(config));
        });
        return *this;
    }
    app::Schedules& schedules() {
        auto lock = lock_world();
        return world_mut().resource_or_init<app::Schedules>();
    }
    std::optional<std::reference_wrapper<app::Schedules>> get_schedules() {
        auto lock = lock_world();
        return get_world_mut()
            .transform([](auto&& w) { return std::make_optional(w); })
            .value_or(std::nullopt)
            .and_then([](World& world) { return world.get_resource_mut<app::Schedules>(); });
    }
    ScheduleOrder& schedule_order() {
        auto lock = lock_world();
        return world_mut().resource_or_init<ScheduleOrder>();
    }
    std::optional<std::reference_wrapper<const ScheduleOrder>> get_schedule_order() const {
        auto lock = lock_world();
        return get_world()
            .transform([](auto&& w) { return std::make_optional(w); })
            .value_or(std::nullopt)
            .and_then([](const World& world) { return world.get_resource<const ScheduleOrder>(); });
    }
    /// Execute a function with exclusive access to a schedule in the world's resources. Throw if the world is not
    /// owned. If insert_if_missing is true, the schedule will be created if it does not exist.
    App& schedule_scope(const schedule::ScheduleLabel& label,
                        const std::function<void(schedule::Schedule&, World&)>& func,
                        bool insert_if_missing = false) {
        resource_scope([&](app::Schedules& schedules, World& world) {
            if (insert_if_missing) {
                func(schedules.schedule_or_insert(label), world);
            } else {
                schedules.get_schedule_mut(label).transform([&](schedule::Schedule& schedule) {
                    func(schedule, world);
                    return true;
                });
            }
        });
        return *this;
    }

    // === System Dispatcher ===

    /// Get or create the system dispatcher owning the world. Return error if no dispatcher exists and the app does not
    /// own a world. Though theoretically no error can happen here.
    std::expected<std::shared_ptr<schedule::SystemDispatcher>, WorldNotOwnedError> get_system_dispatcher() {
        auto lock = lock_world();
        if (auto dispatcher = _dispatcher.lock()) {
            return dispatcher;
        } else if (_world) {
            auto dispatcher = std::shared_ptr<schedule::SystemDispatcher>(
                new schedule::SystemDispatcher(std::move(_world)), [this](schedule::SystemDispatcher* dispatcher) {
                    // retrieve world back since only the shared ptr have the ability to modify the ptr, it is safe not
                    // having extra synchronization here
                    this->_world = dispatcher->release_world();
                    delete dispatcher;
                });
            _dispatcher = dispatcher;
            return dispatcher;
        }
        return std::unexpected(WorldNotOwnedError{});
    }
    /// Get the system dispatcher owning the world. Throws if failed.
    std::shared_ptr<schedule::SystemDispatcher> system_dispatcher() { return get_system_dispatcher().value(); }
    /// Try run a schedule of label. Return true if the schedule was found and run, false otherwise.
    bool run_schedule(const schedule::ScheduleLabel& label) {
        return get_system_dispatcher()
            .transform([&](std::shared_ptr<schedule::SystemDispatcher> dispatcher) {
                std::optional<schedule::Schedule> schedule;
                dispatcher
                    ->world_scope([&](World& world) {
                        schedule = world.get_resource_mut<app::Schedules>().and_then(
                            [&](app::Schedules& schedules) { return schedules.remove_schedule(label); });
                    })
                    .wait();
                if (schedule) {
                    schedule->execute(*dispatcher);
                    // push back the schedule
                    dispatcher
                        ->world_scope([&](World& world) {
                            auto& schedules = world.resource_mut<app::Schedules>();
                            if (schedules.get_schedule(label)) {
                                spdlog::warn(
                                    "Schedule '{}' was re-added while existing one running, old one will be "
                                    "overwritten!",
                                    label.to_string());
                            }
                            schedules.add_schedule(std::move(*schedule));
                        })
                        .wait();
                }
                return schedule.has_value();
            })
            .value_or(false);
    }
    /// Update the app, e.g. run schedules according to schedule order with the provided dispatcher. Return false if no
    /// ScheduleOrder resource found.
    bool update_local(std::shared_ptr<schedule::SystemDispatcher> dispatcher) {
        return dispatcher->world_scope([&](World& world) { return world.take_resource<ScheduleOrder>(); })
            .get()
            .value_or(std::nullopt)
            .transform([this, dispatcher](ScheduleOrder&& order) {
                std::ranges::for_each(order.iter(), [&](const schedule::ScheduleLabel& label) {
                    if (!run_schedule(label)) {
                        spdlog::warn("Failed to run schedule '{}', schedule not found.", label.to_string());
                    }
                });
                // push back the order
                dispatcher->world_scope([&](World& world) { world.insert_resource(std::move(order)); }).wait();
                return true;
            })
            .value_or(false);
    }
    /// Update the app, e.g. run schedules according to schedule order with the internal dispatcher.
    /// Return a future that will hold false if no ScheduleOrder resource found or no dispatcher available.
    /// Default launch policy is async.
    std::future<bool> update(std::launch launch = std::launch::async) {
        return get_system_dispatcher()
            .transform([this, launch](std::shared_ptr<schedule::SystemDispatcher> dispatcher) mutable {
                return std::async(launch, [this, dispatcher]() { return update_local(dispatcher); });
            })
            .value_or([]() { return std::async(std::launch::deferred, []() { return false; }); }());
    }

   private:
    std::unique_lock<std::recursive_mutex> lock_world() const {
        return std::unique_lock<std::recursive_mutex>(*_world_mutex);
    }

    AppLabel _label;

    std::unordered_map<AppLabel, std::unique_ptr<App>> _sub_apps;

    std::shared_ptr<std::atomic<uint32_t>> _world_ids;
    std::unique_ptr<std::recursive_mutex> _world_mutex;
    std::unique_ptr<World> _world;

    std::weak_ptr<schedule::SystemDispatcher> _dispatcher;
};
}  // namespace epix::core