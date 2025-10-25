#pragma once

#include "../schedule/schedule.hpp"
#include "state.hpp"

namespace epix::core::app {
struct ScheduleInfo : public schedule::ScheduleLabel {
    using ScheduleLabel::ScheduleLabel;

    std::vector<std::function<void(schedule::SetConfig&)>> transforms;
};
inline struct PreStartupT {
} PreStartup;
inline struct StartupT {
} Startup;
inline struct PostStartupT {
} PostStartup;
inline struct FirstT {
} First;
inline struct PreUpdateT {
} PreUpdate;
inline struct UpdateT {
} Update;
inline struct PostUpdateT {
} PostUpdate;
inline struct LastT {
} Last;
inline struct PreExitT {
} PreExit;
inline struct ExitT {
} Exit;
inline struct PostExitT {
} PostExit;
inline struct StateTransitionT {
} StateTransition;
enum StateTransitionSet {
    Transit,
    Callback,
};
inline struct OnEnterT {
    template <typename T>
        requires std::is_enum_v<T>
    ScheduleInfo operator()(T state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](schedule::SetConfig& info) {
            info.run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
                return ((*cur == state) && cur.is_added()) || ((*cur != state) && (*next == state));
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnEnter;
inline struct OnExitT {
    template <typename T>
        requires std::is_enum_v<T>
    ScheduleInfo operator()(T state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](schedule::SetConfig& info) {
            info.run_if(
                [state](Res<State<T>> cur, Res<NextState<T>> next) { return (*cur == state) && (*next != state); });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnExit;
inline struct OnChangeT {
    template <typename T>
    ScheduleInfo operator()(T state = T{}) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([](schedule::SetConfig& info) {
            info.run_if([](Res<State<T>> cur, Res<NextState<T>> next) { return *cur != *next; });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnChange;

struct MainSchedulePlugin {
    void build(App& app);
};
}  // namespace epix::core::app