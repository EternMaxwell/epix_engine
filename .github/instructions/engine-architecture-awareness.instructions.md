---
applyTo: "**"
description: "Engine architecture awareness: ECS structure, multithreaded system executor, render/main app concurrency. Always active when working in this repo. Covers module authoring, test structure, and example authoring conventions."
---
# Engine Architecture Awareness

## Core Architecture Overview

This engine is built on `epix.core` — always be aware of it before writing any code.  
Use the **epix-module-research** skill (`epix-module-research`) to investigate `epix_engine/core` if you need a deeper understanding of any specific subsystem.

### ECS

- Entities, components, queries, and resources live in `epix.core` (see `epix_engine/core/modules/core/` and `epix_engine/core/modules/query/`).
- Systems are plain functions (or lambdas) whose parameters declare access — the scheduler infers data dependencies automatically.

### Multithreading — Two Levels

**Level 1 — System Schedule Executor** (within a world):
- Schedules dispatch systems in parallel using a `ScheduleExecutor` (`MultithreadClassicExecutor`, `MultithreadFlatExecutor`, `TaskflowExecutor` — see `epix_engine/core/modules/schedule/executors.cppm`).
- Executors derive data-access conflicts from `SystemMeta` / `FilteredAccessSet` and insert dependency edges so systems that share mutable data are never run concurrently.
- Standard schedules: `PreStartup → Startup → PostStartup → First → PreUpdate → Update → PostUpdate → Last → PreExit → Exit → PostExit` (see `epix_engine/core/modules/app/main_schedule.cppm`).

**Level 2 — Runner-level App Concurrency** (between worlds):
- The application runs a **main world** (simulation) and a **render sub-world** concurrently at the runner level.
- Data is propagated from main → render via `Extract<T>` (see `epix_engine/core/modules/app/extract.cppm`): render systems wrap their main-world parameters in `Extract<T>` to access the extracted snapshot without touching the live world.
- Both worlds have independent `Schedules` and `World` instances; they share data only through the extraction mechanism.

### Plugin System

- Features are added via plugins (`build(App&)` / `finish(App&)` / `finalize(App&)` lifecycle). See `epix_engine/core/modules/app/plugin.cppm`.

---

## Use Existing Engine Modules First

**Before implementing any new system, resource, or utility, always check whether the engine already provides it.**  
Only write a custom solution when the user explicitly asks for it, or after confirming no existing module covers the need.

Key modules and systems to be aware of:

| Area | Module / Feature | Location |
|---|---|---|
| Rendering pipeline management | `PipelineServer`, render graph | `epix_engine/render/` |
| Asset loading / management | Asset module (`epix.assets`) | `epix_engine/assets/` |
| Shader compilation / reflection | Shader module (`epix.shader`) | `epix_engine/shader/` |
| Windowing / input | Window & input modules | `epix_engine/window/`, `epix_engine/input/` |
| 2D sprites | Sprite module | `epix_engine/sprite/` |
| Text rendering | Text module | `epix_engine/text/` |
| Transforms / hierarchy | Transform module | `epix_engine/transform/` |
| Timing | Time module | `epix_engine/time/` |
| Mesh data | Mesh module | `epix_engine/mesh/` |

Use the **epix-module-research** skill to look up the public API of any module before using or extending it.  
If you are unsure whether a feature exists, search `epix_engine/` before assuming it needs to be written.

---

## Authoring Conventions by Context

### Writing or Modifying a Module

- Reference **non-extension** sibling modules (e.g., `epix_engine/render`, `epix_engine/assets`, `epix_engine/shader`, `epix_engine/window`) to understand how they register resources, add plugins, define schedules, and integrate with the core ECS before writing your own.
- Extension modules (`epix_engine/extension/`) are wrappers and do not set architecture patterns — do not use them as references for new modules.

### Writing Tests

- Mirror the structure of existing tests (e.g., `epix_engine/core/tests/`, `epix_engine/assets/tests/`).
- Reference nearby tests in the same module or in `epix_engine/core/tests/` for setup patterns (App construction, schedule execution, world inspection).

### Writing Examples

- **Do not reference other examples as templates.** Examples vary greatly by use case and the patterns they demonstrate. Start from first principles and the module's public API instead.
