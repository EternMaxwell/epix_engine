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
/** @brief Error returned when the app's world has been moved to a system dispatcher. */
struct WorldNotOwnedError {};
/** @brief Ordered list of schedule labels controlling the execution order of schedules. */
struct ScheduleOrder {
   public:
    /** @brief Iterate over the schedule labels in order. */
    auto iter() const { return std::views::all(labels); }
    /** @brief Insert a schedule label at the beginning. */
    void insert_begin(const ScheduleLabel& label) { labels.insert(labels.begin(), label); }
    /** @brief Insert a schedule label at the end. */
    void insert_end(const ScheduleLabel& label) { labels.push_back(label); }
    /** @brief Insert a label after a specific label, or at the end if not found. */
    void insert_after(const ScheduleLabel& after, const ScheduleLabel& label) {
        auto it = std::find(labels.begin(), labels.end(), after);
        if (it != labels.end()) it++;
        labels.insert(it, label);
    }
    /** @brief Insert a sequence of labels after a specific label, or at the end if not found.
     *  Labels already present are skipped. */
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
    /** @brief Insert a range of labels at the end, skipping duplicates. */
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
    /** @brief Insert a range of labels at the beginning, skipping duplicates. */
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
    /** @brief Remove a label, return true if found and removed. */
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
/** @brief Abstract base class for app runner implementations.
 *  Subclass and override step() and exit() to control the main loop. */
struct AppRunner {
    /** @brief Execute one iteration of the app loop. Return false to stop. */
    virtual bool step(App& app) = 0;
    /** @brief Called when the app is exiting. */
    virtual void exit(App& app) = 0;
    virtual ~AppRunner()        = default;
};
/** @brief The main application. Owns a World, schedules, sub-apps, plugins, and a runner.
 *  Provides a fluent API for configuration and execution. */
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

    /** @brief Create a default App with all core plugins. */
    static App create();
    /** @brief Chain a function call on this app, returning `*this`. */
    App& then(std::invocable<App&> auto&& func) {
        func(*this);
        return *this;
    }

    // === App Info and sub-apps ===

    /** @brief Get the label of the app. */
    AppLabel label() const { return _label; }
    /** @brief Get or create a sub-app with the given label. */
    App& sub_app_or_insert(const AppLabel& label);
    /** @brief Add a sub-app. If a sub-app with the same label exists, nothing happens.
     *  Unlike sub_app_or_insert, returns the parent app. */
    App& add_sub_app(const AppLabel& label);
    /** @brief Try get a const sub-app with the given label. */
    std::optional<std::reference_wrapper<const App>> get_sub_app(const AppLabel& label) const;
    /** @brief Try get a mutable sub-app with the given label. */
    std::optional<std::reference_wrapper<App>> get_sub_app_mut(const AppLabel& label);
    /** @brief Get a const reference to a sub-app. Throws if not found. */
    const App& sub_app(const AppLabel& label) const { return get_sub_app(label).value(); }
    /** @brief Get a mutable reference to a sub-app. Throws if not found. */
    App& sub_app_mut(const AppLabel& label) { return get_sub_app_mut(label).value(); }

    // === World Access ===

    /** @brief Try to get a const reference to the world. Returns error if the world is dispatched. */
    std::expected<std::reference_wrapper<const World>, WorldNotOwnedError> get_world() const;
    /** @brief Try to get a mutable reference to the world. Returns error if the world is dispatched. */
    std::expected<std::reference_wrapper<World>, WorldNotOwnedError> get_world_mut();
    /** @brief Get a const reference to the world. Throws if the world is not owned. */
    const World& world() const { return get_world().value(); }
    /** @brief Get a mutable reference to the world. Throws if the world is not owned. */
    World& world_mut() { return get_world_mut().value(); }
    /** @brief Execute a function with exclusive access to the world. */
    App& world_scope(std::invocable<World&> auto&& func) {
        auto lock = lock_world();
        func(world_mut());
        return *this;
    }
    /** @brief Execute a function with exclusive access to the world's resources. */
    template <typename F>
    App& resource_scope(F&& func) {
        auto lock = lock_world();
        world_mut().resource_scope(std::forward<F>(func));
        return *this;
    }
    /** @brief Try get a const resource from the world. */
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_resource() const {
        return get_world()
            .transform([](auto&& ref) { return std::optional(ref); })
            .value_or(std::nullopt)
            .and_then([](const World& world) { return world.get_resource<T>(); });
    }
    /** @brief Try get a mutable resource from the world. */
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_resource_mut() {
        return get_world_mut()
            .transform([](auto&& ref) { return std::optional(ref); })
            .value_or(std::nullopt)
            .and_then([](World& world) { return world.get_resource_mut<T>(); });
    }
    /** @brief Get a const resource. Throws if not present. */
    template <typename T>
    const T& resource() const {
        return get_resource<T>().value().get();
    }
    /** @brief Get a mutable resource. Throws if not present. */
    template <typename T>
    T& resource_mut() {
        return get_resource_mut<T>().value().get();
    }

    // === Schedule Access ===

    /** @brief Add a schedule to the app. Replaces any existing schedule with the same label. */
    App& add_schedule(Schedule&& schedule);
    /** @brief Add or replace systems in a schedule. Creates the schedule if it does not exist. */
    App& add_systems(ScheduleInfo schedule, SetConfig&& config);
    /** @brief Add pre-systems that run before all scheduled systems. Creates the schedule if needed. */
    App& add_pre_systems(ScheduleInfo schedule, SetConfig&& config);
    /** @brief Add pre-systems to every schedule in the schedule order. */
    App& add_pre_systems(SetConfig&& config);
    /** @brief Add post-systems that run after all scheduled systems. Creates the schedule if needed. */
    App& add_post_systems(ScheduleInfo schedule, SetConfig&& config);
    /** @brief Add post-systems to every schedule in the schedule order. */
    App& add_post_systems(SetConfig&& config);
    /** @brief Configure sets in a schedule. Creates the schedule if needed. Replaces existing set configs. */
    App& configure_sets(ScheduleInfo schedule, SetConfig&& config);
    /** @brief Configure sets across all schedules. Replaces existing set configs in each. */
    App& configure_sets(SetConfig&& config);
    /** @brief Get the Schedules resource. Throws if world not owned. */
    Schedules& schedules();
    /** @brief Try get the Schedules resource. */
    std::optional<std::reference_wrapper<Schedules>> get_schedules();
    /** @brief Get the ScheduleOrder resource. Throws if world not owned. */
    ScheduleOrder& schedule_order();
    /** @brief Try get the ScheduleOrder resource (const). */
    std::optional<std::reference_wrapper<const ScheduleOrder>> get_schedule_order() const;
    /** @brief Execute a function with exclusive access to a schedule.
     *  @param label The schedule label.
     *  @param func The function to execute.
     *  @param insert_if_missing If true, creates the schedule if it does not exist. */
    App& schedule_scope(const ScheduleLabel& label,
                        const std::function<void(Schedule&, World&)>& func,
                        bool insert_if_missing = false);

    // === Plugin Management ===

    /** @brief Add a plugin to the app. build() is called immediately;
     *  finish() before running, finalize() after running.
     *  @tparam T Plugin type satisfying is_plugin. */
    template <typename T, typename... Args>
    App& add_plugin(Args&&... args)
        requires std::constructible_from<T, Args...> && is_plugin<T>
    {
        resource_scope([&](Plugins& plugins) { plugins.add_plugin<T>(*this, std::forward<Args>(args)...); });
        return *this;
    }
    /** @brief Add multiple plugins to the app. */
    template <typename... Ts>
    App& add_plugins(Ts&&... ts)
        requires((std::constructible_from<std::decay_t<Ts>, Ts> && is_plugin<std::decay_t<Ts>>) && ...)
    {
        resource_scope([&](Plugins& plugins) { (plugins.add_plugin(*this, std::forward<Ts>(ts)), ...); });
        return *this;
    }
    /** @brief Try get a mutable reference to a plugin of type T. */
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_plugin_mut() {
        return get_resource_mut<Plugins>().and_then([](Plugins& plugins) { return plugins.get_plugin_mut<T>(); });
    }
    /** @brief Try get a const reference to a plugin of type T. */
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_plugin() const {
        return get_resource<const Plugins>().and_then([](const Plugins& plugins) { return plugins.get_plugin<T>(); });
    }
    /** @brief Get a const reference to a plugin. Throws if not found.
     *  @tparam T Plugin type. */
    template <typename T>
    const T& plugin() const {
        return get_plugin<T>().value().get();
    }
    /** @brief Get a mutable reference to a plugin. Throws if not found.
     *  @tparam T Plugin type. */
    template <typename T>
    T& plugin_mut() {
        return get_plugin_mut<T>().value().get();
    }
    /** @brief Execute a function with exclusive access to one or more plugins.
     *  Logs an error if any requested plugin is not found. */
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

    /** @brief Register an event type T in the app.
     *  Creates the Events<T> resource and sets up automatic update in the Last schedule. */
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
    /** @brief Register multiple event types in the app. */
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

    /** @brief Insert a state with an initial value and register its updater. */
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
    /** @brief Insert a default-constructed state. */
    template <typename T>
    App& init_state()
        requires(std::is_enum_v<T>)
    {
        insert_state(T{});
        return *this;
    }

    // === System Dispatcher ===

    /** @brief Get or create the system dispatcher owning the world.
     *  Returns error if no dispatcher exists and the app does not own a world. */
    std::expected<std::shared_ptr<SystemDispatcher>, WorldNotOwnedError> get_system_dispatcher();
    /** @brief Configure hooks for this app's dispatcher.
     *
     * If the dispatcher already exists, hooks are applied immediately.
     * Otherwise hooks are stored and applied when the dispatcher is created.
     */
    App& set_dispatch_system_hooks(DispatchSystemHooks hooks) {
        auto lock               = lock_world();
        _pending_dispatch_hooks = std::move(hooks);
        if (auto dispatcher = _dispatcher.lock()) {
            dispatcher->set_dispatch_system_hooks(*_pending_dispatch_hooks);
        }
        return *this;
    }
    /** @brief Get the system dispatcher owning the world. Throws if failed. */
    std::shared_ptr<SystemDispatcher> system_dispatcher() { return get_system_dispatcher().value(); }
    /** @brief Try run a schedule with the provided dispatcher.
     *  @return true if the schedule was found and run. */
    bool run_schedule(const ScheduleLabel& label, std::shared_ptr<SystemDispatcher> dispatcher);
    /** @brief Try run a schedule with the internal dispatcher.
     *  @return true if the schedule was found and run. */
    bool run_schedule(const ScheduleLabel& label) {
        return get_system_dispatcher()
            .transform([this, &label](std::shared_ptr<SystemDispatcher> dispatcher) {
                return run_schedule(label, dispatcher);
            })
            .value_or(false);
    }
    /** @brief Run schedules from a range of labels using the provided dispatcher.
     *  Logs a warning for each schedule not found. */
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
    /** @brief Update the app by running schedules in schedule-order with the provided dispatcher.
     *  @return false if no ScheduleOrder resource found. */
    bool update_local(std::shared_ptr<SystemDispatcher> dispatcher);
    /** @brief Update the app asynchronously with the internal dispatcher.
     *  @return A future holding false if no ScheduleOrder or dispatcher available. */
    std::future<bool> update(std::launch launch = std::launch::async);

    /** @brief Check if the app has an extract function set. */
    bool has_extract() const { return static_cast<bool>(extract_fn); }
    /** @brief Extract data from another app into this app using the extract function. */
    void extract(App& other);
    /** @brief Set the extract function. First argument is this app, second is the source world. */
    App& set_extract_fn(std::move_only_function<void(App&, World&)> fn) {
        extract_fn = std::move(fn);
        return *this;
    }
    /** @brief Check if the app has a runner set. */
    bool has_runner() const { return static_cast<bool>(runner); }
    /** @brief Set the runner for the app. Called when run() is invoked. */
    void set_runner(std::unique_ptr<AppRunner> fn) { runner = std::move(fn); }
    /** @brief Pop and return the current runner (leaves runner as nullptr). */
    std::unique_ptr<AppRunner> pop_runner() {
        return std::move(runner);  // runner should be nullptr after move
    }
    /** @brief Error codes for runner scope access. */
    enum class RunnerError {
        RunnerNotSet,   /**< No runner has been set. */
        RunnerMismatch, /**< Runner exists but does not match expected type. */
    };
    /** @brief Access the runner as a specific subtype. Returns error if not set or type mismatch. */
    template <typename F>
        requires requires {
            typename function_traits<F>::return_type;
            typename function_traits<F>::args_tuple;
            requires(std::tuple_size_v<typename function_traits<F>::args_tuple> == 1);
            requires std::derived_from<std::decay_t<std::tuple_element_t<0, typename function_traits<F>::args_tuple>>,
                                       AppRunner>;
            requires std::invocable<F, std::decay_t<std::tuple_element_t<0, typename function_traits<F>::args_tuple>>&>;
        }
    std::expected<typename function_traits<F>::return_type, RunnerError> runner_scope(F&& func) {
        if (!runner) return std::unexpected(RunnerError::RunnerNotSet);
        using RunnerType = std::decay_t<std::tuple_element_t<0, typename function_traits<F>::args_tuple>>;
        auto* runner     = dynamic_cast<RunnerType*>(this->runner.get());
        if (!runner) return std::unexpected(RunnerError::RunnerMismatch);
        if constexpr (std::is_void_v<typename function_traits<F>::return_type>) {
            func(*runner);
            return {};
        } else {
            return func(*runner);
        }
    }
    /** @brief Run the app using the configured runner. Throws if no runner is set. */
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
    std::optional<DispatchSystemHooks> _pending_dispatch_hooks;
};
}  // namespace core