module;

#include <spdlog/spdlog.h>

export module epix.core:app;

import std;
import epix.traits;

import :label;
import :labels;

export import :app.decl;
export import :app.state;
export import :app.loop;
export import :app.schedules;
export import :app.main_schedule;
export import :app.event;
export import :app.extract;
export import :app.plugin;

export namespace core {
struct WorldNotOwnedError {};
struct ScheduleOrder {
   public:
    /// Iterate over the schedule labels in order
    auto iter() const { return std::views::all(labels); }
    /// Insert at the beginning
    void insert_begin(const ScheduleLabel& label) { labels.insert(labels.begin(), label); }
    /// Insert at the end
    void insert_end(const ScheduleLabel& label) { labels.push_back(label); }
    /// Insert after a specific label, or at the end if not found
    void insert_after(const ScheduleLabel& after, const ScheduleLabel& label) {
        auto it = std::find(labels.begin(), labels.end(), after);
        if (it != labels.end()) it++;
        labels.insert(it, label);
    }
    /// Insert a sequence of labels after a specific label, or at the end if not found. New labels will be ignored if
    /// already present.
    template <typename Rng>
    void insert_range_after(ScheduleLabel after, Rng&& new_labels)
        requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, ScheduleLabel>
    {
        auto existing = labels | std::ranges::to<std::unordered_set<ScheduleLabel>>();
        auto it       = std::find(labels.begin(), labels.end(), after);
        if (it != labels.end()) it++;
        labels.insert_range(it, std::forward<Rng>(new_labels) | std::views::filter([&](const ScheduleLabel& label) {
                                    return !existing.contains(label);
                                }));
    }
    template <typename Rng>
    void insert_range_end(Rng&& new_labels)
        requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, ScheduleLabel>
    {
        auto existing = labels | std::ranges::to<std::unordered_set<ScheduleLabel>>();
        labels.insert_range(labels.end(),
                            std::forward<Rng>(new_labels) | std::views::filter([&](const ScheduleLabel& label) {
                                return !existing.contains(label);
                            }));
    }
    template <typename Rng>
    void insert_range_begin(Rng&& new_labels)
        requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, ScheduleLabel>
    {
        auto existing = labels | std::ranges::to<std::unordered_set<ScheduleLabel>>();
        labels.insert_range(labels.begin(),
                            std::forward<Rng>(new_labels) | std::views::filter([&](const ScheduleLabel& label) {
                                return !existing.contains(label);
                            }));
    }
    /// Remove a label, return true if found and removed
    bool remove(const ScheduleLabel& label) {
        auto it = std::find(labels.begin(), labels.end(), label);
        if (it != labels.end()) {
            labels.erase(it);
            return true;
        }
        return false;
    }

   private:
    std::list<ScheduleLabel> labels;
};
struct AppRunner {
    virtual bool step(App& app) = 0;
    virtual void exit(App& app) = 0;
    virtual ~AppRunner()        = default;
};
struct App {
   public:
    App(const AppLabel& label                            = AppLabel::from_type<App>(),
        std::shared_ptr<TypeRegistry> type_registry      = std::make_shared<TypeRegistry>(),
        std::shared_ptr<std::atomic<uint32_t>> world_ids = std::make_shared<std::atomic<uint32_t>>(0))
        : _label(label),
          _world(std::make_unique<World>(world_ids->fetch_add(1), type_registry)),
          _world_ids(world_ids),
          _world_mutex(std::make_unique<std::recursive_mutex>()) {}
    App(const App&)            = delete;
    App(App&&)                 = default;
    App& operator=(const App&) = delete;
    App& operator=(App&&)      = default;
    ~App() {
        if (auto dispatcher = _dispatcher.lock()) {
            _world = dispatcher->release_world();
        }
    }

    static App create();
    App& then(std::invocable<App&> auto&& func) {
        func(*this);
        return *this;
    }

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
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_resource() const {
        return get_world()
            .transform([](auto&& ref) { return std::optional(ref); })
            .value_or(std::nullopt)
            .and_then([](const World& world) { return world.get_resource<T>(); });
    }
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_resource_mut() {
        return get_world_mut()
            .transform([](auto&& ref) { return std::optional(ref); })
            .value_or(std::nullopt)
            .and_then([](World& world) { return world.get_resource_mut<T>(); });
    }
    template <typename T>
    const T& resource() const {
        return get_resource<T>().value().get();
    }
    template <typename T>
    T& resource_mut() {
        return get_resource_mut<T>().value().get();
    }

    // === Schedule Access ===

    /// Add a schedule to the app. If a schedule with the same label exists, it will be replaced.
    App& add_schedule(Schedule&& schedule);
    /// Add or replace systems in a schedule in the app. If the schedule does not exist, it will be created.
    App& add_systems(ScheduleInfo schedule, SetConfig&& config);
    /// Configure sets in a schedule in the app. If the schedule does not exist, it will be created. If the sets exist,
    /// they will be replaced with the new configuration.
    App& configure_sets(ScheduleInfo schedule, SetConfig&& config);
    /// Configure sets in all schedules in the app. If the sets exist in a schedule, they will be replaced with the new
    /// configuration.
    App& configure_sets(SetConfig&& config);
    Schedules& schedules();
    std::optional<std::reference_wrapper<Schedules>> get_schedules();
    ScheduleOrder& schedule_order();
    std::optional<std::reference_wrapper<const ScheduleOrder>> get_schedule_order() const;
    /// Execute a function with exclusive access to a schedule in the world's resources. Throw if the world is not
    /// owned. If insert_if_missing is true, the schedule will be created if it does not exist.
    App& schedule_scope(const ScheduleLabel& label,
                        const std::function<void(Schedule&, World&)>& func,
                        bool insert_if_missing = false);

    // === Plugin Management ===

    /// Add a plugin to the app. The build function for the plugin will be called during addition. finish function of
    /// the plugin will be called before running, and finalize function will be called after running.
    template <typename T, typename... Args>
    App& add_plugin(Args&&... args)
        requires std::constructible_from<T, Args...> && is_plugin<T>
    {
        resource_scope([&](Plugins& plugins) { plugins.add_plugin<T>(*this, std::forward<Args>(args)...); });
        return *this;
    }
    /// Add multiple plugins to the app. The build function for each plugin will be called during addition. The finish
    /// function of each plugin will be called before running, and the finalize function will be called after running.
    template <typename... Ts>
    App& add_plugins(Ts&&... ts)
        requires((std::constructible_from<std::decay_t<Ts>, Ts> && is_plugin<std::decay_t<Ts>>) && ...)
    {
        resource_scope([&](Plugins& plugins) { (plugins.add_plugin(*this, std::forward<Ts>(ts)), ...); });
        return *this;
    }
    /// Try get a mutable reference to a plugin of type T.
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_plugin_mut() {
        return get_resource_mut<Plugins>().and_then([](Plugins& plugins) { return plugins.get_plugin_mut<T>(); });
    }
    /// Try get a const reference to a plugin of type T.
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_plugin() const {
        return get_resource<const Plugins>().and_then([](const Plugins& plugins) { return plugins.get_plugin<T>(); });
    }
    /// Get a const reference to a plugin of type T. Throws if not found.
    template <typename T>
    const T& plugin() const {
        return get_plugin<T>().value().get();
    }
    /// Get a mutable reference to a plugin of type T. Throws if not found.
    template <typename T>
    T& plugin_mut() {
        return get_plugin_mut<T>().value().get();
    }
    /// Execute a function with exclusive access to a plugin of type T. Throws if not found.
    template <typename F>
    App& plugin_scope(F&& func) {
        using arg_tuple = typename function_traits<std::decay_t<F>>::args_tuple;
        resource_scope([&](Plugins& plugins) {
            auto plugin_refs = [&]<std::size_t... I>(std::index_sequence<I...>) {
                return std::make_tuple(plugins.get_plugin_mut<std::decay_t<std::tuple_element_t<I, arg_tuple>>>()...);
            }(std::make_index_sequence<std::tuple_size_v<arg_tuple>>());
            bool all_found = [&]<std::size_t... I>(std::index_sequence<I...>) {
                return true && ([&]<std::size_t J>(std::integral_constant<std::size_t, J>) {
                           bool found = std::get<J>(plugin_refs).has_value();
                           if (!found) {
                               spdlog::error(
                                   "Plugin of type '{}' not found in app '{}'",
                                   meta::type_id<std::decay_t<std::tuple_element_t<J, arg_tuple>>>::short_name(),
                                   _label.to_string());
                           }
                           return found;
                       }(std::integral_constant<std::size_t, I>{}) &&
                                ...);
            }(std::make_index_sequence<std::tuple_size_v<arg_tuple>>());
            if (!all_found) return;
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                func(std::get<I>(plugin_refs).value().get()...);
            }(std::make_index_sequence<std::tuple_size_v<arg_tuple>>());
        });
        return *this;
    }

    // === Event Management ===

    struct EventSystem {
        struct Updates {
            std::vector<void (*)(World&)> updates;
        };
        void build(App& app) { app.world_mut().init_resource<Updates>(); }
        void finish(App& app) {
            app.add_systems(Last, into([](World& world) {
                                      for (auto&& update : world.resource_mut<Updates>().updates) {
                                          update(world);
                                      }
                                  }).set_name("update events"));
        }
    };

    /// Register an event type T in the app.
    template <typename T>
    App& add_event() {
        if (world().get_resource<Events<T>>()) return *this;
        resource_scope([&](Plugins& plugins) {
            plugins.get_plugin_mut<EventSystem>().or_else([&]() {
                plugins.add_plugin(*this, EventSystem{});
                return plugins.get_plugin_mut<EventSystem>();
            });
        });
        resource_scope([](EventSystem::Updates& updates) {
            updates.updates.emplace_back([](World& world) {
                world.get_resource_mut<Events<T>>().transform([](Events<T>& events) {
                    events.update();
                    return true;
                });
            });
        });
        world_scope([&](World& world) { world.init_resource<Events<T>>(); });
        return *this;
    }
    /// Register multiple event types in the app.
    template <typename... Ts>
    App& add_events() {
        (add_event<Ts>(), ...);
        return *this;
    }

    // === State Management ===

    struct StateUpdater {
        struct Updates {
            Updates()               = default;
            Updates(const Updates&) = delete;
            Updates(Updates&&)      = default;
            std::vector<std::unique_ptr<System<std::tuple<>, void>>> update_system;
        };
        std::unordered_set<meta::type_index> registered_states;
        void build(App& app) { app.world_mut().init_resource<Updates>(); }
        void finish(App& app) {
            app.add_systems(StateTransition, into([](ParamSet<World&, ResMut<Updates>> params) {
                                                 auto&& [world, updates] = params.get();
                                                 for (auto&& sys : updates->update_system) {
                                                     auto res = sys->run({}, world);
                                                 }
                                             })
                                                 .set_name("update states")
                                                 .in_set(StateTransitionSet::Transit));
        }
    };

    template <typename T>
    App& insert_state(const T& state)
        requires(std::is_enum_v<T>)
    {
        resource_scope([&](Plugins& plugins) {
            plugins.get_plugin_mut<StateUpdater>()
                .or_else([&]() {
                    plugins.add_plugin(*this, StateUpdater{});
                    return plugins.get_plugin_mut<StateUpdater>();
                })
                .transform([this](StateUpdater& updater) -> bool {
                    auto type_idx = meta::type_id<T>();
                    if (updater.registered_states.contains(type_idx)) return false;
                    updater.registered_states.insert(type_idx);
                    auto update_system = make_system_unique([](Res<NextState<T>> next_state, ResMut<State<T>> state) {
                        if (state.get() == (T)next_state.get()) return;
                        state.get_mut() = (T)next_state.get();
                    });
                    update_system->initialize(world_mut());
                    update_system->set_name("state updater for " + std::string(meta::type_id<T>().short_name()));
                    world_mut().resource_mut<StateUpdater::Updates>().update_system.push_back(std::move(update_system));
                    return true;
                });
        });
        world_scope([&](World& world) {
            world.insert_resource(State<T>(state));
            world.insert_resource(NextState<T>(state));
        });
        return *this;
    }
    template <typename T>
    App& init_state()
        requires(std::is_enum_v<T>)
    {
        insert_state(T{});
        return *this;
    }

    // === System Dispatcher ===

    /// Get or create the system dispatcher owning the world. Return error if no dispatcher exists and the app does not
    /// own a world. Though theoretically no error can happen here.
    std::expected<std::shared_ptr<SystemDispatcher>, WorldNotOwnedError> get_system_dispatcher();
    /// Get the system dispatcher owning the world. Throws if failed.
    std::shared_ptr<SystemDispatcher> system_dispatcher() { return get_system_dispatcher().value(); }
    /// Try run schedule with label with the provided dispatcher. Return true if the schedule was found and run, false
    /// otherwise.
    bool run_schedule(const ScheduleLabel& label, std::shared_ptr<SystemDispatcher> dispatcher);
    /// Try run schedule with label with the internal dispatcher. Return true if system dispatcher successfully obtained
    /// and the schedule was found and run, false otherwise.
    bool run_schedule(const ScheduleLabel& label) {
        return get_system_dispatcher()
            .transform([this, &label](std::shared_ptr<SystemDispatcher> dispatcher) {
                return run_schedule(label, dispatcher);
            })
            .value_or(false);
    }
    /// Try run schedules with labels in the provided range and the provided dispatcher. Return true if system
    /// dispatcher successfully obtained. For each schedule, log a warning if not found.
    template <typename Rng>
    void run_schedules_local(Rng&& labels, std::shared_ptr<SystemDispatcher> dispatcher)
        requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, ScheduleLabel>
    {
        std::ranges::for_each(labels, [&](const ScheduleLabel& label) {
            if (!run_schedule(label, dispatcher)) {
                spdlog::warn("Failed to run schedule '{}', schedule not found. Skip.", label.to_string());
            }
        });
    }
    template <typename Rng>
    std::future<void> run_schedules(Rng&& labels, std::launch launch = std::launch::async)
        requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, ScheduleLabel>
    {
        return get_system_dispatcher()
            .transform([this, launch,
                        labels = std::forward<Rng>(labels)](std::shared_ptr<SystemDispatcher> dispatcher) mutable {
                return std::async(launch, [this, dispatcher, labels = std::move(labels)]() mutable {
                    run_schedules_local(std::move(labels), dispatcher);
                });
            })
            .or_else([](auto&&) -> std::expected<std::future<void>, WorldNotOwnedError> {
                return std::async(std::launch::deferred, []() {});
            })
            .value();
    }
    template <typename... Labels>
    std::future<void> run_schedules(Labels&&... labels) {
        constexpr bool explicit_launch =
            std::same_as<std::launch, std::tuple_element_t<sizeof...(Labels) - 1, std::tuple<std::decay_t<Labels>...>>>;
        auto launch = [&] {
            if constexpr (explicit_launch) {
                return std::get<sizeof...(Labels) - 1>(std::forward_as_tuple(std::forward<Labels>(labels)...));
            } else {
                return std::launch::async;
            }
        }();
        auto array = [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::array{ScheduleLabel(std::forward<Labels>(std::get<I>(std::forward_as_tuple(labels...))))...};
        }(std::make_index_sequence<(explicit_launch ? sizeof...(Labels) - 1 : sizeof...(Labels))>());
        return run_schedules(std::move(array), launch);
    }
    /// Update the app, e.g. run schedules according to schedule order with the provided dispatcher. Return false if no
    /// ScheduleOrder resource found.
    bool update_local(std::shared_ptr<SystemDispatcher> dispatcher);
    /// Update the app, e.g. run schedules according to schedule order with the internal dispatcher.
    /// Return a future that will hold false if no ScheduleOrder resource found or no dispatcher available.
    /// Default launch policy is async.
    std::future<bool> update(std::launch launch = std::launch::async);

    /// Check if the app has an extract function set.
    bool has_extract() const { return static_cast<bool>(extract_fn); }
    /// Extract data from another app into this app. will call the extract function set by set_extract_fn internally.
    void extract(App& other);
    /// Set the extract function for the app. First argument is this app, second argument is the world extracted from
    /// the other app.
    App& set_extract_fn(std::move_only_function<void(App&, World&)> fn) {
        extract_fn = std::move(fn);
        return *this;
    }
    /// Check if the app has a runner function set.
    bool has_runner() const { return static_cast<bool>(runner); }
    /// Set the runner function for the app. The function will be called when run() is called.
    void set_runner(std::unique_ptr<AppRunner> fn) { runner = std::move(fn); }
    /// Pop the current runner
    std::unique_ptr<AppRunner> pop_runner() {
        return std::move(runner);  // runner should be nullptr after move
    }
    template <typename F>
        requires requires {
            typename function_traits<F>::return_type;
            typename function_traits<F>::args_tuple;
            requires std::tuple_size_v<typename function_traits<F>::args_tuple> == 1;
            requires std::derived_from<std::decay_t<std::tuple_element_t<0, typename function_traits<F>::args_tuple>>,
                                       AppRunner>;
            requires std::invocable<
                F,
                std::add_reference_t<std::decay_t<std::tuple_element_t<0, typename function_traits<F>::args_tuple>>>>;
        }
    std::optional<function_traits<F>::return_type> runner_scope(F&& func) {
        if (!runner) return std::nullopt;
        using RunnerType = std::decay_t<std::tuple_element_t<0, typename function_traits<F>::args_tuple>>;
        auto* runner     = dynamic_cast<RunnerType*>(this->runner.get());
        if (!runner) return std::nullopt;
        if constexpr (std::is_void_v<typename function_traits<F>::return_type>) {
            func(*runner);
            return {};
        } else {
            return func(*runner);
        }
    }
    /// Run the app. Or throw if no runner function set.
    void run();

   private:
    std::unique_lock<std::recursive_mutex> lock_world() const {
        return std::unique_lock<std::recursive_mutex>(*_world_mutex);
    }

    AppLabel _label;

    std::unordered_map<AppLabel, std::unique_ptr<App>> _sub_apps;

    std::shared_ptr<std::atomic<uint32_t>> _world_ids;
    std::unique_ptr<std::recursive_mutex> _world_mutex;
    std::unique_ptr<World> _world;

    std::move_only_function<void(App&, World&)> extract_fn;
    std::unique_ptr<AppRunner> runner;

    std::weak_ptr<SystemDispatcher> _dispatcher;
};
}  // namespace core