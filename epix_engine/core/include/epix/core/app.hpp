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
    App& sub_app_or_insert(const AppLabel& label);
    /// Add a sub-app. If a sub-app with the same label exists, nothing will happen.
    /// Unlike sub_app_or_insert, this will return the parent app.
    App& add_sub_app(const AppLabel& label);
    /// Try get a sub-app with the given label.
    std::optional<std::reference_wrapper<const App>> get_sub_app(const AppLabel& label) const;
    /// Try get a mutable sub-app with the given label.
    std::optional<std::reference_wrapper<App>> get_sub_app_mut(const AppLabel& label);
    /// Get a const reference to a sub-app with the given label. Throws if not found.
    const App& sub_app(const AppLabel& label) const { return get_sub_app(label).value(); }
    /// Get a mutable reference to a sub-app with the given label. Throws if not found.
    App& sub_app_mut(const AppLabel& label) { return get_sub_app_mut(label).value(); }

    // === World Access ===

    /// Try to get a const reference to the world. Return error if the world is not owned.
    std::expected<std::reference_wrapper<const World>, WorldNotOwnedError> get_world() const;
    /// Try to get a mutable reference to the world. Return error if the world is not owned.
    std::expected<std::reference_wrapper<World>, WorldNotOwnedError> get_world_mut();
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
    App& add_schedule(schedule::Schedule&& schedule);
    /// Add or replace systems in a schedule in the app. If the schedule does not exist, it will be created.
    App& add_systems(app::ScheduleInfo schedule, schedule::SetConfig&& config);
    /// Configure sets in a schedule in the app. If the schedule does not exist, it will be created. If the sets exist,
    /// they will be replaced with the new configuration.
    App& configure_sets(app::ScheduleInfo schedule, schedule::SetConfig&& config);
    app::Schedules& schedules();
    std::optional<std::reference_wrapper<app::Schedules>> get_schedules();
    ScheduleOrder& schedule_order();
    std::optional<std::reference_wrapper<const ScheduleOrder>> get_schedule_order() const;
    /// Execute a function with exclusive access to a schedule in the world's resources. Throw if the world is not
    /// owned. If insert_if_missing is true, the schedule will be created if it does not exist.
    App& schedule_scope(const schedule::ScheduleLabel& label,
                        const std::function<void(schedule::Schedule&, World&)>& func,
                        bool insert_if_missing = false);

    // === System Dispatcher ===

    /// Get or create the system dispatcher owning the world. Return error if no dispatcher exists and the app does not
    /// own a world. Though theoretically no error can happen here.
    std::expected<std::shared_ptr<schedule::SystemDispatcher>, WorldNotOwnedError> get_system_dispatcher();
    /// Get the system dispatcher owning the world. Throws if failed.
    std::shared_ptr<schedule::SystemDispatcher> system_dispatcher() { return get_system_dispatcher().value(); }
    /// Try run a schedule of label. Return true if the schedule was found and run, false otherwise.
    bool run_schedule(const schedule::ScheduleLabel& label);
    /// Update the app, e.g. run schedules according to schedule order with the provided dispatcher. Return false if no
    /// ScheduleOrder resource found.
    bool update_local(std::shared_ptr<schedule::SystemDispatcher> dispatcher);
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