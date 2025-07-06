#pragma once

#include "schedule.h"
#include "state.h"

namespace epix::app {
struct ScheduleInfo : public ScheduleLabel {
    using ScheduleLabel::ScheduleLabel;
    std::vector<std::function<void(SystemSetConfig&)>> transforms;
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
inline struct PrepareT {
} Prepare;
inline struct PreRenderT {
} PreRender;
inline struct RenderT {
} Render;
inline struct PostRenderT {
} PostRender;
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
inline struct ExtractScheduleT {
} ExtractSchedule;
inline struct OnEnterT {
    template <typename T>
    ScheduleInfo operator()(T&& state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SystemSetConfig& info) {
            info.run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
                return ((cur == state) && cur->is_just_created()) ||
                       ((cur != state) && (next == state));
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnEnter;
inline struct OnExitT {
    template <typename T>
    ScheduleInfo operator()(T&& state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SystemSetConfig& info) {
            info.run_if([state](Res<State<T>> cur, Res<NextState<T>> next) {
                return (cur == state) && (next != state);
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnExit;
inline struct OnChangeT {
    template <typename T>
    ScheduleInfo operator()(T&& state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SystemSetConfig& info) {
            info.run_if([](Res<State<T>> cur, Res<NextState<T>> next) {
                return cur != next;
            });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnChange;
}  // namespace epix::app