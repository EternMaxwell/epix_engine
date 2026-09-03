# ECS TODO

Items that remain incomplete in the current `epix.ecs` implementation.

## Table-level query iteration

The `WorldQuery<T>` extension point still has commented `set_table` hooks in
`include/epix/ecs/query/fetch.hpp`, `filter.hpp`, `refs.hpp`, and related entity
query implementations. Queries currently select an archetype and its table via
`set_archetype`; there is no dense-query fast path selected by an `IS_DENSE`
property.

This is a performance opportunity, not a correctness issue. Implementing it
requires restoring `set_table` for all built-in query data and filters, exposing
density metadata, and teaching query iteration to select the table path only
when every involved component uses dense table storage.

## TaskflowExecutor scheduling bug

`TaskflowExecutor` (in `schedule/taskflow_executor.cpp`) can schedule systems
incompletely or out of order: it intermittently renders the frame incomplete
(white window) and produces large startup frame spikes (53–130 ms, the startup
low-FPS diagnosed on the mesh batching example). It was excluded from
`AutoExecutor`'s candidates as a stop-gap (commit `332e936a`). Root-cause the
precedence/topology logic in `build_taskflow`: ordering edges come only from
`depends` plus access-conflict edges, so systems that are access-compatible with
`render_system` receive no ordering edge and can race it when the set-chain
`depends` is incomplete for that pair. Restore `TaskflowExecutor` to the auto
candidates once fixed.

## Recently completed

- Required components can be declared through a component's static
  `register_required_components(...)` hook or registered dynamically through
  `World::register_required_components*`.
- Resources use entity component storage and participate in ordinary access
  conflict and change detection.
- `RemovedComponents<T>`, `World::removed<T>()`, and runtime-ID removal streams
  track removals and despawns.
