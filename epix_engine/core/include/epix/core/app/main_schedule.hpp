#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <functional>
#include <type_traits>
#include <vector>
#endif

#include <epix/core/app/decl.hpp>
#include <epix/core/app/state.hpp>
#include <epix/core/schedule.hpp>
#include <epix/core/ticks.hpp>

namespace epix::core {
/** @brief A schedule label with optional system-set transforms.
 *  Extends ScheduleLabel to support run-condition transforms for
 *  state-dependent scheduling. */
EPIX_EXPORT struct ScheduleInfo : public ScheduleLabel {
    using ScheduleLabel::ScheduleLabel;

    /** @brief Transforms applied to system sets added under this schedule label. */
    std::vector<std::function<void(SetConfig&)>> transforms;
};
/** @brief Runs once before Startup. */
EPIX_EXPORT inline struct PreStartupT {
} PreStartup;
/** @brief Runs once at application start. */
EPIX_EXPORT inline struct StartupT {
} Startup;
/** @brief Runs once after Startup. */
EPIX_EXPORT inline struct PostStartupT {
} PostStartup;
/** @brief Runs at the beginning of every frame. */
EPIX_EXPORT inline struct FirstT {
} First;
/** @brief Runs before the main Update schedule. */
EPIX_EXPORT inline struct PreUpdateT {
} PreUpdate;
/** @brief Main per-frame update schedule. */
EPIX_EXPORT inline struct UpdateT {
} Update;
/** @brief Runs after the main Update schedule. */
EPIX_EXPORT inline struct PostUpdateT {
} PostUpdate;
/** @brief Runs at the end of every frame. */
EPIX_EXPORT inline struct LastT {
} Last;
/** @brief Runs before Exit during shutdown. */
EPIX_EXPORT inline struct PreExitT {
} PreExit;
/** @brief Runs during shutdown. */
EPIX_EXPORT inline struct ExitT {
} Exit;
/** @brief Runs after Exit during shutdown. */
EPIX_EXPORT inline struct PostExitT {
} PostExit;
/** @brief Schedule for state transition callbacks. */
EPIX_EXPORT inline struct StateTransitionT {
} StateTransition;
enum StateTransitionSet {
    Transit,
    Callback,
};
/** @brief Functor that creates a schedule running when a given state is entered.
 *  Usage: `OnEnter(MyState::Playing)` */
EPIX_EXPORT inline struct OnEnterT {
    template <typename T>
        requires std::is_enum_v<T>
    /** @brief Create a schedule triggered on entering the given state. */
    ScheduleInfo operator()(T state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SetConfig& info) {
            info.run_if(
                [state](Res<State<T>> cur) { return (*cur == state) && (cur.is_added() || cur.is_modified()); });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnEnter;
/** @brief Functor that creates a schedule running when a given state is exited.
 *  Usage: `OnExit(MyState::Playing)` */
EPIX_EXPORT inline struct OnExitT {
    template <typename T>
        requires std::is_enum_v<T>
    /** @brief Create a schedule triggered on exiting the given state. */
    ScheduleInfo operator()(T state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SetConfig& info) {
            info.run_if([state](Res<State<T>> cur) { return (*cur != state) && !cur.is_added() && cur.is_modified(); });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnExit;
/** @brief Functor that creates a schedule running whenever a state resource is modified.
 *  Usage: `OnChange(MyState{})` */
EPIX_EXPORT inline struct OnChangeT {
    template <typename T>
    /** @brief Create a schedule triggered on any state change. */
    ScheduleInfo operator()(T state = T{}) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([](SetConfig& info) {
            info.run_if([](Res<State<T>> cur) { return cur.is_modified(); });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnChange;

/** @brief Plugin that registers all built-in schedules (Startup, Update, etc.). */
EPIX_EXPORT struct MainSchedulePlugin {
    /** @brief Register all built-in schedules with the app. */
    void attach(App& app);
};
}  // namespace epix::core