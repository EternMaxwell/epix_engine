#pragma once

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/traits.hpp>
#include <expected>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif

#include <epix/app/extract.hpp>
#include <epix/app/loop.hpp>
#include <epix/app/main_schedule.hpp>
#include <epix/app/plugin.hpp>
#include <epix/app/state.hpp>
#include <epix/app/task_pool_plugin.hpp>

#ifndef EPIX_MAKE_LABEL
#define EPIX_MAKE_LABEL(type)                                                         \
    struct type : public ::epix::ecs::Label {                                         \
       public:                                                                        \
        type() noexcept = default;                                                    \
        template <typename T>                                                         \
        type(T t) noexcept                                                            \
            requires(!std::is_same_v<std::decay_t<T>, type> && std::is_object_v<T> && \
                     std::constructible_from<Label, T>)                               \
            : Label(t) {}                                                             \
    };
#endif

EPIX_EXPORT namespace epix::app {
    EPIX_MAKE_LABEL(AppLabel)
    /** @brief Error returned when the app's world has been moved to a system dispatcher. */
    struct WorldNotOwnedError {};
    /** @brief Ordered list of schedule labels controlling the execution order of schedules. */
    struct ScheduleOrder {
       public:
        /** @brief Iterate over the schedule labels in order. */
        auto iter() const noexcept { return std::views::all(labels); }
        /** @brief Insert a schedule label at the beginning. */
        void insert_begin(const ecs::ScheduleLabel& label) { labels.insert(labels.begin(), label); }
        /** @brief Insert a schedule label at the end. */
        void insert_end(const ecs::ScheduleLabel& label) { labels.push_back(label); }
        /** @brief Insert a label after a specific label, or at the end if not found. */
        void insert_after(const ecs::ScheduleLabel& after, const ecs::ScheduleLabel& label) {
            auto it = std::find(labels.begin(), labels.end(), after);
            if (it != labels.end()) it++;
            labels.insert(it, label);
        }
        /** @brief Insert a sequence of labels after a specific label, or at the end if not found.
         *  Labels already present are skipped. */
        template <typename Rng>
        void insert_range_after(ecs::ScheduleLabel after, Rng&& new_labels)
            requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, ecs::ScheduleLabel>
        {
            auto existing = std::ranges::to<std::unordered_set<ecs::ScheduleLabel>>(labels);
            auto it       = std::find(labels.begin(), labels.end(), after);
            if (it != labels.end()) it++;
            labels.insert_range(
                it, std::views::filter(std::forward<Rng>(new_labels),
                                       [&](const ecs::ScheduleLabel& label) { return !existing.contains(label); }));
        }
        /** @brief Insert a range of labels at the end, skipping duplicates. */
        template <typename Rng>
        void insert_range_end(Rng&& new_labels)
            requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, ecs::ScheduleLabel>
        {
            auto existing = std::ranges::to<std::unordered_set<ecs::ScheduleLabel>>(labels);
            labels.insert_range(labels.end(),
                                std::views::filter(std::forward<Rng>(new_labels), [&](const ecs::ScheduleLabel& label) {
                                    return !existing.contains(label);
                                }));
        }
        /** @brief Insert a range of labels at the beginning, skipping duplicates. */
        template <typename Rng>
        void insert_range_begin(Rng&& new_labels)
            requires std::ranges::range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, ecs::ScheduleLabel>
        {
            auto existing = std::ranges::to<std::unordered_set<ecs::ScheduleLabel>>(labels);
            labels.insert_range(labels.begin(),
                                std::views::filter(std::forward<Rng>(new_labels), [&](const ecs::ScheduleLabel& label) {
                                    return !existing.contains(label);
                                }));
        }
        /** @brief Remove a label, return true if found and removed. */
        bool remove(const ecs::ScheduleLabel& label) noexcept {
            auto it = std::find(labels.begin(), labels.end(), label);
            if (it != labels.end()) {
                labels.erase(it);
                return true;
            }
            return false;
        }

       private:
        std::list<ecs::ScheduleLabel> labels;
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
        App(const AppLabel& label                                 = AppLabel::from_type<App>(),
            std::shared_ptr<ecs::TypeRegistry> type_registry      = std::make_shared<ecs::TypeRegistry>(),
            std::shared_ptr<std::atomic<std::uint32_t>> world_ids = std::make_shared<std::atomic<std::uint32_t>>(0))
            : _label(label), _world(world_ids->fetch_add(1), type_registry), _world_ids(world_ids) {}
        App(const App&)            = delete;
        App(App&&)                 = default;
        App& operator=(const App&) = delete;
        App& operator=(App&&)      = default;
        ~App()                     = default;

        /** @brief Create a default App with all core plugins. */
        static App create();
        /** @brief Chain a function call on this app, returning `*this`. */
        App& then(std::invocable<App&> auto&& func) {
            func(*this);
            return *this;
        }

        // === App Info and sub-apps ===

        /** @brief Get the label of the app. */
        AppLabel label() const noexcept { return _label; }
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

        /** @brief Get a const reference to the world. */
        const ecs::World& world() const noexcept { return _world; }
        /** @brief Get a mutable reference to the world. */
        ecs::World& world_mut() noexcept { return _world; }
        /** @brief Execute a function with access to the world. */
        App& world_scope(std::invocable<ecs::World&> auto&& func) {
            func(_world);
            return *this;
        }
        /** @brief Execute a function with access to the world's resources. */
        template <typename F>
        App& resource_scope(F&& func) {
            (void)_world.resource_scope(std::forward<F>(func));
            return *this;
        }
        /** @brief Try get a const resource from the world. */
        template <typename T>
        std::optional<std::reference_wrapper<const T>> get_resource() const {
            return _world.get_resource<T>();
        }
        /** @brief Try get a mutable resource from the world. */
        template <typename T>
        std::optional<std::reference_wrapper<T>> get_resource_mut() {
            return _world.get_resource_mut<T>();
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
        App& add_schedule(ecs::Schedule&& schedule);
        /** @brief Add or replace systems in a schedule. Creates the schedule if it does not exist. */
        App& add_systems(ScheduleInfo schedule, ecs::SetConfig&& config);
        /** @brief Add pre-systems that run before all scheduled systems. Creates the schedule if needed. */
        App& add_pre_systems(ScheduleInfo schedule, ecs::SetConfig&& config);
        /** @brief Add pre-systems to every schedule in the schedule order. */
        App& add_pre_systems(ecs::SetConfig&& config);
        /** @brief Add post-systems that run after all scheduled systems. Creates the schedule if needed. */
        App& add_post_systems(ScheduleInfo schedule, ecs::SetConfig&& config);
        /** @brief Add post-systems to every schedule in the schedule order. */
        App& add_post_systems(ecs::SetConfig&& config);
        /** @brief Configure sets in a schedule. Creates the schedule if needed. Replaces existing set configs. */
        App& configure_sets(ScheduleInfo schedule, ecs::SetConfig&& config);
        /** @brief Configure sets across all schedules. Replaces existing set configs in each. */
        App& configure_sets(ecs::SetConfig&& config);
        /** @brief Get the Schedules resource. Throws if world not owned. */
        ecs::Schedules& schedules();
        /** @brief Try get the Schedules resource. */
        std::optional<std::reference_wrapper<ecs::Schedules>> get_schedules();
        /** @brief Get the ScheduleOrder resource. Throws if world not owned. */
        ScheduleOrder& schedule_order();
        /** @brief Try get the ScheduleOrder resource (const). */
        std::optional<std::reference_wrapper<const ScheduleOrder>> get_schedule_order() const;
        /** @brief Execute a function with exclusive access to a schedule.
         *  @param label The schedule label.
         *  @param func The function to execute.
         *  @param insert_if_missing If true, creates the schedule if it does not exist. */
        App& schedule_scope(const ecs::ScheduleLabel& label,
                            const std::function<void(ecs::Schedule&, ecs::World&)>& func,
                            bool insert_if_missing = false);

        // === Plugin Management ===

        /** @brief Add a plugin to the app. attach() is called immediately;
         *  ready() before running, detach() after running.
         *  @tparam T Plugin type satisfying is_plugin. */
        template <typename T, typename... Args>
        App& add_plugin(Args&&... args)
            requires std::constructible_from<T, Args...> && internal::is_plugin<T>
        {
            resource_scope(
                [&](internal::Plugins& plugins) { plugins.add_plugin<T>(*this, std::forward<Args>(args)...); });
            return *this;
        }
        /** @brief Add multiple plugins to the app. */
        template <typename... Ts>
        App& add_plugins(Ts&&... ts)
            requires((std::constructible_from<std::decay_t<Ts>, Ts> && internal::is_plugin<std::decay_t<Ts>>) && ...)
        {
            resource_scope([&](internal::Plugins& plugins) { (plugins.add_plugin(*this, std::forward<Ts>(ts)), ...); });
            return *this;
        }
        /** @brief Try get a mutable reference to a plugin of type T. */
        template <typename T>
        std::optional<std::reference_wrapper<T>> get_plugin_mut() {
            return get_resource_mut<internal::Plugins>().and_then(
                [](internal::Plugins& plugins) { return plugins.get_plugin_mut<T>(); });
        }
        /** @brief Try get a const reference to a plugin of type T. */
        template <typename T>
        std::optional<std::reference_wrapper<const T>> get_plugin() const {
            return get_resource<const internal::Plugins>().and_then(
                [](const internal::Plugins& plugins) { return plugins.get_plugin<T>(); });
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
            using arg_tuple = typename traits::function_traits<std::decay_t<F>>::args_tuple;
            resource_scope([&](internal::Plugins& plugins) {
                auto plugin_refs = [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return std::make_tuple(
                        plugins.get_plugin_mut<std::decay_t<std::tuple_element_t<I, arg_tuple>>>()...);
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

        /** @brief Register an event type T in the app.
         *  Creates the Events<T> resource and sets up automatic update in the Last schedule. */
        template <typename T>
        App& add_event() {
            ecs::EventRegistry::register_event<T>(world_mut());
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
                std::vector<std::unique_ptr<ecs::System<std::tuple<>, void>>> update_system;
            };
            std::unordered_set<meta::type_index> registered_states;
            void attach(App& app) { app.world_mut().init_resource<Updates>(); }
            void ready(App& app) {
                app.add_systems(StateTransition, ecs::into([](ecs::ParamSet<ecs::World&, ecs::ResMut<Updates>> params) {
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
            resource_scope([&](internal::Plugins& plugins) {
                plugins.get_plugin_mut<StateUpdater>()
                    .or_else([&]() {
                        plugins.add_plugin(*this, StateUpdater{});
                        return plugins.get_plugin_mut<StateUpdater>();
                    })
                    .transform([this](StateUpdater& updater) -> bool {
                        auto type_idx = meta::type_id<T>();
                        if (updater.registered_states.contains(type_idx)) return false;
                        updater.registered_states.insert(type_idx);
                        auto update_system =
                            make_system_unique([](ecs::Res<NextState<T>> next_state, ecs::ResMut<State<T>> state) {
                                if (state.get() == (T)next_state.get()) return;
                                state.get_mut() = (T)next_state.get();
                            });
                        update_system->initialize(world_mut());
                        update_system->set_name("state updater for " + std::string(meta::type_id<T>().short_name()));
                        world_mut().resource_mut<StateUpdater::Updates>().update_system.push_back(
                            std::move(update_system));
                        return true;
                    });
            });
            world_scope([&](ecs::World& world) {
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

        // === Schedule Execution ===

        /** @brief Try run a schedule by label.
         *  @return true if the schedule was found and run. */
        bool run_schedule(const ecs::ScheduleLabel& label);
        /** @brief Run a sequence of schedules by label. Logs a warning for each not found. */
        template <typename... Labels>
        void run_schedules(Labels&&... labels) {
            (void)((run_schedule(ecs::ScheduleLabel(std::forward<Labels>(labels))) ||
                    (spdlog::warn("Failed to run schedule, schedule not found. Skip."), true)),
                   ...);
        }
        /** @brief Update the app by running schedules in schedule-order (synchronous). */
        void update();

        // === Sub-app Extraction ===

        /** @brief Take ownership of a sub-app, removing it from this app. Returns nullptr if not found. */
        std::unique_ptr<App> take_sub_app(const AppLabel& label);
        /** @brief Re-insert a sub-app (e.g. after running it on another thread). */
        void insert_sub_app(const AppLabel& label, std::unique_ptr<App> app);

        /** @brief Check if the app has an extract function set. */
        bool has_extract() const noexcept { return static_cast<bool>(extract_fn); }
        /** @brief Extract data from another app into this app using the extract function. */
        void extract(App& other);
        /** @brief Set the extract function. First argument is this app, second is the source world. */
        App& set_extract_fn(std::move_only_function<void(App&, ecs::World&)> fn) {
            extract_fn = std::move(fn);
            return *this;
        }
        /** @brief Check if the app has a runner set. */
        bool has_runner() const noexcept { return static_cast<bool>(runner); }
        /** @brief Set the runner for the app. Called when run() is invoked. */
        void set_runner(std::unique_ptr<AppRunner> fn) noexcept { runner = std::move(fn); }
        /** @brief Pop and return the current runner (leaves runner as nullptr). */
        std::unique_ptr<AppRunner> pop_runner() noexcept {
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
                typename traits::function_traits<F>::return_type;
                typename traits::function_traits<F>::args_tuple;
                requires(std::tuple_size_v<typename traits::function_traits<F>::args_tuple> == 1);
                requires std::derived_from<
                    std::decay_t<std::tuple_element_t<0, typename traits::function_traits<F>::args_tuple>>, AppRunner>;
                requires std::invocable<
                    F, std::decay_t<std::tuple_element_t<0, typename traits::function_traits<F>::args_tuple>>&>;
            }
        std::expected<typename traits::function_traits<F>::return_type, RunnerError> runner_scope(F&& func) {
            if (!runner) return std::unexpected(RunnerError::RunnerNotSet);
            using RunnerType = std::decay_t<std::tuple_element_t<0, typename traits::function_traits<F>::args_tuple>>;
            auto* runner     = dynamic_cast<RunnerType*>(this->runner.get());
            if (!runner) return std::unexpected(RunnerError::RunnerMismatch);
            if constexpr (std::is_void_v<typename traits::function_traits<F>::return_type>) {
                func(*runner);
                return {};
            } else {
                return func(*runner);
            }
        }
        /** @brief Run the app using the configured runner. Throws if no runner is set. */
        void run();

       private:
        struct DefaultCreateTag {};

        AppLabel _label;

        std::unordered_map<AppLabel, std::unique_ptr<App>> _sub_apps;

        std::shared_ptr<std::atomic<std::uint32_t>> _world_ids;
        ecs::World _world;

        std::move_only_function<void(App&, ecs::World&)> extract_fn;
        std::unique_ptr<AppRunner> runner;

        explicit App(
            DefaultCreateTag tag,
            const AppLabel& label                                 = AppLabel::from_type<App>(),
            std::shared_ptr<ecs::TypeRegistry> type_registry      = std::make_shared<ecs::TypeRegistry>(),
            std::shared_ptr<std::atomic<std::uint32_t>> world_ids = std::make_shared<std::atomic<std::uint32_t>>(0));
    };
    static_assert(std::movable<App>);
}  // namespace epix::app