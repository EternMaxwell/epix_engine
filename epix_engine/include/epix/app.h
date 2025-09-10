#pragma once

#include "app/app.h"
#include "app/commands.h"
#include "app/entity.h"
#include "app/hash_tool.h"
#include "app/label.h"
#include "app/profiler.h"
#include "app/query.h"
#include "app/res.h"
#include "app/schedule.h"
#include "app/system.h"
#include "app/systemparam.h"
#include "app/world.h"
#include "app/world_data.h"

namespace epix {
using app::Label;

using app::World;

using app::Main;

using app::Schedule;
using app::Schedules;

using app::IntoSystem;
using app::RunState;
using app::RunSystemError;
using RunSystemConfig = app::RunState::RunSystemConfig;

using app::App;
using app::AppCreateInfo;
using app::AppProfiler;
using AppConfig = app::AppCreateInfo;
using app::AppExit;
using app::AppRunner;

using app::into;
using app::sets;

using app::Children;
using app::Entity;
using app::Parent;

using app::Bundle;

using app::ParamSet;

using app::ExecutorType;

using app::Get;
using app::Has;
using app::Mut;
using app::Opt;

using app::Filter;
using app::Or;
using app::With;
using app::Without;

using app::Query;

using app::Commands;
using app::EntityCommands;

using app::Res;
using app::ResMut;

using app::NextState;
using app::State;

using app::EventReader;
using app::EventWriter;

using app::Extract;
using app::Local;

using app::LoopPlugin;

using app::Exit;
using app::PostExit;
using app::PreExit;

using app::First;
using app::Last;
using app::OnChange;
using app::OnEnter;
using app::OnExit;
using app::PostStartup;
using app::PostUpdate;
using app::PreStartup;
using app::PreUpdate;
using app::Startup;
using app::Update;

using thread_pool = BS::thread_pool<BS::tp::priority>;
}  // namespace epix