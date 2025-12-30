module;

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>


export module epix.core:app.main_schedule;

import :app.decl;
import :app.state;
import :ticks;
import :schedule;

namespace core {
export struct ScheduleInfo : public ScheduleLabel {
    using ScheduleLabel::ScheduleLabel;

    std::vector<std::function<void(SetConfig&)>> transforms;
};
export inline struct PreStartupT {
} PreStartup;
export inline struct StartupT {
} Startup;
export inline struct PostStartupT {
} PostStartup;
export inline struct FirstT {
} First;
export inline struct PreUpdateT {
} PreUpdate;
export inline struct UpdateT {
} Update;
export inline struct PostUpdateT {
} PostUpdate;
export inline struct LastT {
} Last;
export inline struct PreExitT {
} PreExit;
export inline struct ExitT {
} Exit;
export inline struct PostExitT {
} PostExit;
export inline struct StateTransitionT {
} StateTransition;
enum StateTransitionSet {
    Transit,
    Callback,
};
export inline struct OnEnterT {
    template <typename T>
        requires std::is_enum_v<T>
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
export inline struct OnExitT {
    template <typename T>
        requires std::is_enum_v<T>
    ScheduleInfo operator()(T state) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([state](SetConfig& info) {
            info.run_if([state](Res<State<T>> cur) { return (*cur != state) && !cur.is_added() && cur.is_modified(); });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnExit;
export inline struct OnChangeT {
    template <typename T>
    ScheduleInfo operator()(T state = T{}) {
        ScheduleInfo info(StateTransition);
        info.transforms.emplace_back([](SetConfig& info) {
            info.run_if([](Res<State<T>> cur) { return cur.is_modified(); });
            info.in_set(StateTransitionSet::Callback);
        });
        return info;
    }
} OnChange;

export struct MainSchedulePlugin {
    void build(App& app);
};
}  // namespace core