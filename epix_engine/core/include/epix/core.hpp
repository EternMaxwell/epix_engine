#pragma once

/**
 * @file core.hpp
 * @brief Combined header for epix core module.
 */
#include "api/macros.hpp"
#include "core/app.hpp"
#include "core/app/app_sche.hpp"
#include "core/app/loop.hpp"
#include "core/app/state.hpp"
#include "core/archetype.hpp"
#include "core/change_detection.hpp"
#include "core/entities.hpp"
#include "core/event/events.hpp"
#include "core/event/reader.hpp"
#include "core/event/writer.hpp"
#include "core/fwd.hpp"
#include "core/hierarchy.hpp"
#include "core/meta/typeid.hpp"
#include "core/meta/typeindex.hpp"
#include "core/query/fetch.hpp"
#include "core/query/filter.hpp"
#include "core/query/fwd.hpp"
#include "core/query/query.hpp"
#include "core/schedule/schedule.hpp"
#include "core/schedule/system_set.hpp"
#include "core/system/commands.hpp"
#include "core/system/from_param.hpp"
#include "core/system/param.hpp"
#include "core/system/system.hpp"
#include "core/tick.hpp"
#include "core/type_system/type_registry.hpp"
#include "core/world.hpp"
#include "core/world/command_queue.hpp"
#include "core/world/entity_ref.hpp"

namespace epix::core {
namespace prelude {
using query::Query;
using query::QueryIter;
using query::QueryState;
using query::Single;

using query::Added;
using query::Has;
using query::Item;
using query::Modified;
using query::Opt;
using query::Or;
using query::With;
using query::Without;

using meta::type_id;
using meta::type_index;

using type_system::TypeId;
using type_system::TypeInfo;
using type_system::TypeRegistry;

using system::Commands;
using system::Deferred;
using system::EntityCommands;
using system::Local;
using system::ParamSet;

using system::RunSystemError;
using system::System;
using system::SystemException;
using system::SystemMeta;
using system::SystemParam;
using system::ValidateParamError;

using archetype::Archetype;
using archetype::Archetypes;

using schedule::ExecuteConfig;
using schedule::Schedule;
using schedule::SetConfig;
using schedule::SystemSetLabel;

using event::EventReader;
using event::Events;
using event::EventWriter;

using app::NextState;
using app::State;

using core::CommandQueue;
using core::DeferredWorld;
using core::World;

using core::Entities;
using core::Entity;
using core::EntityRef;
using core::EntityRefMut;
using core::EntityWorldMut;

using core::hierarchy::Children;
using core::hierarchy::Parent;

using core::ArchetypeId;
using core::ArchetypeRow;
using core::TableId;
using core::TableRow;

using core::Tick;
using core::TickRefs;
using core::Ticks;
using core::TicksMut;

using core::Mut;
using core::Ref;
using core::Res;
using core::ResMut;

using core::App;
using core::Extract;

using app::Exit;
using app::First;
using app::Last;
using app::OnChange;
using app::OnEnter;
using app::OnExit;
using app::PostExit;
using app::PostStartup;
using app::PostUpdate;
using app::PreExit;
using app::PreStartup;
using app::PreUpdate;
using app::Startup;
using app::StateTransition;
using app::Update;

using app::AppExit;
using app::LoopPlugin;

using core::into;
using core::sets;

using core::system::make_system;
using core::system::make_system_shared;
using core::system::make_system_unique;
}  // namespace prelude

using namespace prelude;
using wrapper::int_base;
}  // namespace epix::core

namespace epix::prelude {
using namespace core::prelude;
}
namespace epix {
using namespace core::prelude;
}