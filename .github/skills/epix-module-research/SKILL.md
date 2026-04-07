---
name: epix-module-research
description: 'Explore and understand engine-internal epix_engine modules before writing code that depends on them. Use when looking up the API of an engine module (epix_core, epix_render, epix_assets, epix_shader, etc.); when unsure how to use an engine system; when finding the right exported types or functions. Follows public-interface-first order: docs → module interfaces → examples → implementation.'
argument-hint: '<module-name> [type or system to look up]'
---

# Epix Engine Module Research

## Goal

Understand an engine module's **public API** before writing code that uses it. Always work from the outermost public surface inward — never start from implementation files.

---

## Source Priority (Outermost → Innermost)

1. **In-repo documentation** — `documentation/<module>.md` or any `.md` inside `epix_engine/<module>/`
2. **Module interface files** — `epix_engine/<module>/modules/**/*.cppm` (exported declarations only)
3. **Legacy public headers** — `epix_engine/<module>/include/**/*.hpp` (compatibility layer)
4. **Example files** — `epix_engine/<module>/examples/**/*.cpp`
5. **Test files** — `epix_engine/<module>/tests/**/*.cpp`
6. **Implementation** — `epix_engine/<module>/src/**/*.cpp` and `*-impl.cppm` partitions — **last resort only**

> Read implementation only when you cannot determine behavior from the above, or when diagnosing a bug. State explicitly why you needed to go that deep.

---

## Module Layout

```
epix_engine/<module>/
├── modules/           ← public API — read these first
│   ├── mod.cppm       ← top-level re-export (start here)
│   ├── <part>.cppm         ← partition interface
│   ├── <part>-decl.cppm    ← forward declarations
│   ├── <part>-spec.cppm    ← template specializations
│   └── <part>-impl.cppm    ← heavy inline impl (read last)
├── include/           ← legacy headers (read if no .cppm equivalent)
├── src/               ← implementation .cpp (read last)
├── examples/          ← usage patterns
└── tests/             ← usage patterns + expected behavior
```

**Module naming:** `epix.<category>:[<partition>]` — e.g., `epix.render:pipeline`, `epix.core:schedule`

---

## Procedure

### Step 1 — Check documentation

```
file_search: documentation/<module>*.md
file_search: epix_engine/<module>/**/*.md
```

If a doc file exists, read it first — it is the fastest path to usage intent and key types. Documentation may be absent (WIP); if so, proceed to Step 2.

### Step 2 — Find and read the primary module interface

```
file_search: epix_engine/<module>/modules/mod.cppm
```

If `mod.cppm` exists, read it — it typically re-exports all public partitions and gives the full API surface in one file. If not, list `modules/` and read each `*.cppm` that is not `-impl.cppm`.

Focus on:
- `export` declarations (types, functions, concepts)
- `export import` lines (which partitions are re-exported)
- Template parameters and constraints

### Step 3 — Read relevant partition interfaces

If the target type or system is in a specific partition (e.g., `pipeline.cppm`, `schedule.cppm`), read that file. Skip `-impl.cppm` and `-spec.cppm` unless the exported declaration alone is not enough to understand usage.

### Step 4 — Check examples and tests

```
file_search: epix_engine/<module>/examples/**
file_search: epix_engine/<module>/tests/**
```

Examples show the intended usage pattern. Tests show edge cases and confirm expected behavior. Prefer examples for "how do I use this?" and tests for "what happens when X?".

### Step 5 — Read implementation (only if needed)

Open `src/*.cpp` or `-impl.cppm` only to answer a specific question that the public interface could not answer (e.g., "what does `cache.sync()` actually do with the event queue?"). Note what you looked for.

---

## Module → Path Reference

| Module | Path | Primary interface |
|--------|------|------------------|
| `epix_core` | `epix_engine/core/` | `modules/mod.cppm` |
| `epix_assets` | `epix_engine/assets/` | `modules/mod.cppm` |
| `epix_shader` | `epix_engine/shader/` | `modules/shader.cppm` |
| `epix_render` | `epix_engine/render/render/` | `modules/render.cppm` |
| `epix_window` | `epix_engine/window/` | `modules/mod.cppm` |
| `epix_input` | `epix_engine/input/` | `modules/mod.cppm` |
| `epix_transform` | `epix_engine/transform/` | `modules/mod.cppm` |
| `epix_text` | `epix_engine/text/` | `modules/mod.cppm` |
| `epix_image` | `epix_engine/image/` | `modules/mod.cppm` |
| `epix_mesh` | `epix_engine/mesh/` | `modules/mod.cppm` |
| `epix_glfw_render` | `epix_engine/render/glfw/` | `modules/mod.cppm` |
| `epix_imgui` | `epix_engine/render/imgui/` | `modules/mod.cppm` |

---

## Reading `.cppm` Files Efficiently

- **Start at `export` lines** — non-exported code is internal and irrelevant.
- **`export import :partition;`** — means the partition's full API is part of this module's public surface; follow it.
- **`export module epix.x:y;`** at top — confirms this is partition `y` of module `epix.x`.
- **Ignore `module :private;` sections** — private fragment, not public API.
- **Template implementations in `-impl.cppm`** — only skim if you need to understand a template's constraints.

---

## Checklist

- [ ] Checked `documentation/` for a module doc first
- [ ] Read `mod.cppm` or the primary façade interface
- [ ] Identified the exported types/functions needed
- [ ] Found at least one example or test showing usage
- [ ] Did **not** read `src/*.cpp` unless necessary — and stated why if you did
