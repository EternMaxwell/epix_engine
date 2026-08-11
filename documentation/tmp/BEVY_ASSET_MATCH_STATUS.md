# Asset parity status

This file is a historical parity tracker, not the public API reference. The old
three-month snapshot described the asset pipeline before its asynchronous
conversion and is no longer applicable.

## Current implementation baseline

- Asset I/O uses asynchronous `Reader` and `Writer` abstractions.
- Loader, saver, transformer, and processor work returns `STDEXEC::task`.
- Asset work is scheduled through `epix::task`, including `IoTaskPool`.
- `AssetServer` returns handles immediately for ordinary loads and processes
  internal load events through the ECS schedules installed by `AssetPlugin`.
- Blocking readiness remains available through `wait_for_asset()` and
  `load_acquire()`, with the documented thread restrictions.
- Processing metadata, dependency tracking, source registration, embedded and
  filesystem readers, hot reload, and typed lifecycle events are implemented.

For maintained interfaces and examples, use the pages in
[`documentation/assets`](../assets/quick.md). Any future parity work should be
derived from current headers under `epix_engine/assets/include/epix/assets`, not
from the removed synchronous design.
