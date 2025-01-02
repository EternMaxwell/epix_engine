#pragma once

// ----EPIX API----
#include "app/common.h"
// ----EPIX API----

#include "app/app.h"
#include "app/command.h"
#include "app/runner.h"
#include "app/runner_tools.h"
#include "app/stage_runner.h"
#include "app/subapp.h"
#include "app/substage_runner.h"
#include "app/system.h"
#include "app/tools.h"
#include "app/world.h"

namespace epix::prelude {
using namespace epix;

// ENTITY PART
using App      = app::App;
using Plugin   = app::Plugin;
using Entity   = app::Entity;
using Bundle   = internal_components::Bundle;
using Parent   = internal_components::Parent;
using Children = internal_components::Children;

// STAGES
using namespace app::stages;

// EVENTS
using AppExit = app::AppExit;

// SYSTEM PARA PART
template <typename T>
using Res = app::Res<T>;
template <typename T>
using ResMut  = app::ResMut<T>;
using Command = app::Command;
template <typename T>
using EventReader = app::EventReader<T>;
template <typename T>
using EventWriter = app::EventWriter<T>;
template <typename T>
using State = app::State<T>;
template <typename T>
using NextState = app::NextState<T>;
template <typename... Args>
using Get = app::Get<Args...>;
template <typename... Args>
using With = app::With<Args...>;
template <typename... Args>
using Without = app::Without<Args...>;
template <typename G, typename I = With<>, typename E = Without<>>
using Query = app::Query<G, I, E>;
template <typename G, typename I = With<>, typename E = Without<>>
using Extract = app::Extract<G, I, E>;
template <typename T>
using Local = app::Local<T>;

// OTHER TOOLS
template <typename Resolution = std::chrono::milliseconds>
    requires std::same_as<Resolution, std::chrono::nanoseconds> ||
             std::same_as<Resolution, std::chrono::microseconds> ||
             std::same_as<Resolution, std::chrono::milliseconds> ||
             std::same_as<Resolution, std::chrono::seconds> ||
             std::same_as<Resolution, std::chrono::minutes> ||
             std::same_as<Resolution, std::chrono::hours>
struct time_scope {
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    std::string name;
    time_scope(const std::string& name) : name(name) {
        start = std::chrono::high_resolution_clock::now();
    }
    ~time_scope() {
        auto end = std::chrono::high_resolution_clock::now();
        auto dur = std::chrono::duration_cast<Resolution>(end - start);
        char resolution_char;
        if constexpr (std::same_as<Resolution, std::chrono::nanoseconds>) {
            resolution_char = 'n';
        } else if constexpr (std::same_as<
                                 Resolution, std::chrono::microseconds>) {
            resolution_char = 'u';
        } else if constexpr (std::same_as<
                                 Resolution, std::chrono::milliseconds>) {
            resolution_char = 'm';
        } else if constexpr (std::same_as<Resolution, std::chrono::seconds>) {
            resolution_char = 's';
        } else if constexpr (std::same_as<Resolution, std::chrono::minutes>) {
            resolution_char = 'M';
        } else if constexpr (std::same_as<Resolution, std::chrono::hours>) {
            resolution_char = 'h';
        }
        spdlog::info("{}: {}{}s", name, dur.count(), resolution_char);
    }
};
}  // namespace epix::prelude
namespace epix {
using namespace epix::prelude;
}
