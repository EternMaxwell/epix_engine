# Application Module

`epix.app` builds the engine application lifecycle on top of `epix.ecs`. It
provides `App`, plugins, runners, built-in schedule order, states, sub-apps, and
the extraction bridge used by rendering.

## Core Parts

- [`App`](app.md): owns the main `ecs::World`, plugins, schedule order, sub-apps,
  and runner.
- [Built-in schedules](built-in-schedules.md): startup, frame, state transition,
  and exit schedule labels.
- [State](state.md): `State<T>`, `NextState<T>`, and state-dependent schedules.
- [Extraction](extract.md): frame-scoped access from a sub-app to the source world.

## Quick Guide

```cpp
import epix.ecs;
import epix.app;

using namespace epix::ecs;
using namespace epix::app;

void startup(Commands commands) {
    commands.insert_resource(std::string{"ready"});
}

void update(Res<std::string> status) {
    std::println("{}", *status);
}

int main() {
    App::create()
        .add_systems(Startup, into(startup))
        .add_systems(Update, into(update))
        .run();
}
```

`App::create()` installs `MainSchedulePlugin`, the `AppExit` event, and a
default runner that performs one `update()`. Add `LoopPlugin` when a headless
application needs a continuing loop; window and render integrations may install
their own runner. Plugin
`attach()` hooks run when added, `ready()` hooks run before the runner starts,
and `detach()` hooks run in reverse order after it exits.
