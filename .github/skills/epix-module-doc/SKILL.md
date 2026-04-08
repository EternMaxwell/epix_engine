---
name: epix-module-doc
description: 'Write or update documentation for an epix_engine module. Use when adding docs for a new or existing module; when auditing a module for missing docs; when documenting unimplemented or partially implemented features. Produces a documentation/<module>/ folder with separate files: quick.md, one .md per feature, and todo.md.'
argument-hint: '<module-name> [specific feature or area to document]'
---

# Epix Module Documentation

## What This Produces

A `documentation/<module>/` folder containing:

```
documentation/<module>/
├── quick.md          # Quick guide — get productive immediately
├── <feature>.md      # One file per major feature, type, or capability
├── ...               # Additional feature files as needed
└── todo.md           # Unimplemented / partially implemented features
```

Every module always gets a folder, even if it currently has only one feature file.

---

## Before Writing

Read the module's public interface first using the `epix-module-research` skill. Do not invent behavior — derive every statement from the actual exported API. Document only what is exported and publicly usable.

---

## File Layout

```
documentation/
├── <module>/
│   ├── quick.md          # Required — quick guide
│   ├── <feature>.md      # One per major exported type or capability
│   └── todo.md           # Required — planned/partial/stub features
├── module_naming_convention.md
└── todo.md               # Project-wide todo (link from module todo.md)
```

**File naming:** use lowercase kebab-case for feature files (`pipeline-server.md`, `asset-handle.md`).  
**Module folder name:** matches the CMake target without the `epix_` prefix (e.g., `epix_assets` → `documentation/assets/`).  
Cross-cutting convention docs (e.g., `module_naming_convention.md`) stay at `documentation/` root.

---

## File Contents

### `quick.md`

```markdown
# EPIX ENGINE <MODULE> MODULE

One-sentence description of what the module does and why it exists.

## Core Parts

Bullet list of all major exported types/systems with a one-line role description.
Link each item to its feature file.
- **[`TypeName`](./<feature>.md)**: what it is and what it owns/manages
- **[`SystemName`](./<feature>.md)**: when it runs and what it does

## Quick Guide

Minimal end-to-end example that gets a user productive immediately.
Cover: how to add the plugin (if any), create/load the primary type, and do the core operation.
Code examples preferred over prose.
```

### `<feature>.md`  *(one file per major type or capability)*

```markdown
# <Feature Name>

One-sentence description of this type or capability.

## Overview

What it is, what it owns, and its contract.

## Usage

**Code example is mandatory.** Every usage section must contain at least one compilable
code snippet that shows the PRIMARY INTENDED usage pattern — not just a trivial
construction. Use the pattern observed in tests/examples (see Step 4), not the pattern
inferred from the type definition alone.

If the type has multiple distinct operations, show each with its own snippet.

## Extending / Custom Implementations *(include if applicable)*

Include this section when the type is a **customization point**: a template struct
that users are expected to specialize, a concept that user-defined types must satisfy,
or any type that can be registered/plugged-in to the engine.

Show the minimal correct specialization or implementation with a code example.
State clearly which methods/fields are required vs optional.

## Constraints / Gotchas

Non-obvious preconditions, ordering requirements, or known limitations.
```

### `todo.md`

```markdown
# TODO — <MODULE>

Features that are planned, partially implemented, or have an API stub only.
See also: [project-wide todo](../todo.md)

- [ ] **Feature name** — brief status note
- [~] **Feature name** — what's done, what's missing
- [i] **Feature name** — API exported but not implemented
```

---

## TODO Status Markers

Use these consistently across all module docs and in `documentation/todo.md`:

| Marker | Meaning |
|--------|---------|
| `[ ]` | Not started — no interface, no implementation |
| `[~]` | In progress — may have partial interface or partial implementation |
| `[i]` | Interface only — API is exported and callable but not yet implemented (stubs/panics) |
| `[x]` | Done — completed; remove from TODO after a release cycle |

**Document TODOs at the feature level, not the task level.** Write what the feature *is* and what state it is in, not implementation subtasks (those belong in `documentation/todo.md` or issue tracker).

```markdown
# TODO — assets

- [ ] **Hot reload** — file watching via efsw is wired up but reload callbacks not implemented.
      `AssetServer::reload()` exists only for manually triggered reloads.
- [i] **Untyped load** — `load_untyped(path)` is part of the API but the loader dispatch for untyped
      handles is not yet implemented.
- [~] **Asset sources** — multiple asset sources partially supported; `source://` prefix recognized
      but fallback ordering between sources is incomplete.
```

---

## Procedure

### Step 1 — Research the module

Use `epix-module-research` to read:
- `epix_engine/<module>/modules/mod.cppm` and all partition `.cppm` files
- `documentation/<module>/` (if it already exists — update files rather than replace)
- Any examples and tests

### Step 2 — Build an export inventory

Before writing any doc, produce a flat list of **every `export` declaration** found across all `.cppm` files. Group by file. Include:
- Every `export struct`, `export class`, `export template`, `export concept`, `export using`, `export function`
- For templates that act as customization points (e.g. `template<typename T> struct Foo;`), note that they are a **specialization target**, not just a type
- For each item, note: is it a concept? a struct? a template trait? a convenience wrapper built on top of another mechanism?

This inventory is the ground truth for Step 4. **Do not skip or abbreviate it.**

> Example: if the module exports both a concept `system_param<T>` AND a template struct `SystemParam<T>`, these are two distinct API surfaces and must both appear in the inventory and in the docs.

### Step 3 — Create the folder and write `quick.md` first

Create `documentation/<module>/` if it does not exist. Write `quick.md` first — the Core Parts list (derived from the export inventory) and Quick Guide example. If you cannot write a correct working example from the public API alone, the research phase is incomplete — go back to Step 1.

### Step 4 — Write one feature file per major exported group

Using the export inventory from Step 2, group related exports into feature files.

**Before writing the Usage section for any type, verify the intended usage pattern:**

1. Search for the type name in `tests/`, `examples/`, and `src/` caller code, or other modules that depend on it (they may use this as well, can be referenced as an example).
2. Read at least one concrete usage site. The code found there is the **authoritative usage pattern**.
3. If the pattern seen in tests/examples differs from what the interface alone would suggest, the test/example pattern takes precedence. Document what users should do, not what the type technically allows.
4. If no test or example exists for a type, note this explicitly and derive usage only from the interface — but flag the section with a `> ⚠ No usage example found in tests/examples` callout.

**Additional rules per type:**
- Every documented type must have a code example in its Usage section. A section with only prose is incomplete.
- If a type is a **customization point** (template struct to specialize, concept to satisfy, registry to register into), include an "Extending" subsection showing a minimal correct user implementation with a code example.
- If a type has **two complementary mechanisms** (e.g. a trait struct for specialization AND a concept/wrapper for convenience), document both under separate headings in the same file, showing clearly how they relate.

Link each feature file from `quick.md`'s Core Parts list.

### Step 5 — Build the TODO list with evidence

A TODO entry requires concrete evidence in source code. For each candidate:

1. **Find the candidate**: scan all `modules/**/*.cppm` and `src/**/*.cpp` for:
   - `// TODO`, `// FIXME`, `// NOT IMPLEMENTED`, `// planned`, `// future`
   - Commented-out declarations or concept bodies
   - Empty function bodies `{}` or `{ return {}; }` on non-trivial functions
   - `assert(false)`, `std::unreachable()`, or `throw` on functions that should work

2. **Verify before adding**: for every candidate TODO, open the corresponding `src/*.cpp` implementation file and check whether a real body exists. **Do not assume a feature is unimplemented just because the interface looks incomplete**.
   - If `src/` has a real implementation → feature is NOT a TODO
   - If `src/` body is empty or missing → mark `[i]` (interface only) or `[~]` (partial)
   - If there is no interface and no implementation → mark `[ ]`

3. **Do not add**: performance concerns, code style issues, compiler workarounds that are already resolved, or items from a project-wide `todo.md` that are not specifically about this module's exported API.

### Step 6 — Cross-link

- `todo.md`: add a link to `../todo.md` for project-wide tracking
- Feature files: link to other module docs when a dependency exists
- Update `documentation/todo.md` if new TODO items were found

---

## Style Rules

- **Imperative headers**: `## Loading Assets`, not `## Asset Loading Process`
- **Code before prose**: a correct 5-line snippet beats two paragraphs
- **No duplication**: if the naming convention is documented in `module_naming_convention.md`, link to it rather than repeating it
- **Present tense for what exists**, future/conditional for what doesn't: "Hot reload is not yet implemented" not "Hot reload will be implemented"
- **Public only**: do not document internal types, `detail::` namespaces, or `-impl.cppm` internals

---

## Checklist

- [ ] Export inventory was produced (Step 2) before writing any doc file
- [ ] Every item in the export inventory is covered in at least one feature file
- [ ] Template customization-point structs and concepts are documented with an "Extending" subsection showing a user-defined specialization/implementation
- [ ] Every Usage section has at least one code example (no prose-only sections)
- [ ] Every Usage section's pattern was verified against tests/examples/src callers — not inferred from the interface alone
- [ ] Any type whose intended usage differs from what its fields/constructors suggest shows the correct pattern (e.g. prefer API methods over direct struct construction when that is the intent)
- [ ] `quick.md` exists with Core Parts list and a working Quick Guide example
- [ ] One `<feature>.md` file per major exported group; all linked from `quick.md`
- [ ] Every TODO entry in `todo.md` has a cited piece of evidence (file + reason: commented-out code / empty body / `assert(false)` / explicit TODO comment)
- [ ] Each TODO entry implementation state was verified in `src/` before being added
- [ ] Interface-only features (`[i]`) are clearly distinguished from not-started (`[ ]`)
- [ ] No prose describes internal implementation details
- [ ] Cross-references use relative links
- [ ] `documentation/todo.md` updated if new TODO items were found
