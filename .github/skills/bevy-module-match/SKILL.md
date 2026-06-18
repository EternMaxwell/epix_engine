---
name: bevy-module-match
description: 'Audit and implement Bevy API parity for an epix_engine module. Use when syncing a C++ module with its Bevy counterpart; when checking which Bevy APIs are missing, different, or intentionally skipped; when implementing a Rust-to-C++ API translation. Produces a documentation/tmp/BEVY_MATCH_STATUS.md tracker and implements missing APIs in the correct module files.'
argument-hint: '<module-name> [optional: specific struct/trait to focus on]'
---

# Bevy Module Match

## What This Produces

1. `documentation/tmp/BEVY_MATCH_STATUS.md` — entity-level parity table (see format below)
2. Implemented ❌/⚠️/🚫 items in the C++ module (all tests passing)

The tracker is the source of truth, but can be out-dated. It is created at the start and updated after every entity implementation.

---

## Bevy Source Input

**Strongly prefer the user manually attaching the Bevy source directory** (drag the `bevy_<name>/src/` folder into the chat, or attach individual `.rs` files).  
Only fall back to fetching from the web if no attachment is provided.  
Never guess Bevy API shapes — always read the actual source.

---

## Rust → C++ Mapping Reference

| Rust / Bevy | C++ / epix_engine |
|-------------|-------------------|
| `Arc<T>` | `std::shared_ptr<T>` |
| `Weak<T>` | `std::weak_ptr<T>` |
| `RwLock<T>` | `utils::RwLock<T>` (guard-based: `.read()` / `.write()`) |
| `Mutex<T>` | `utils::Mutex<T>` |
| `TypeId` | `meta::type_index` |
| `async/await`, `spawn` | `utils::IOTaskPool::instance().detach_task(...)` + blocking |
| `impl Trait` return | template or `std::shared_ptr<ErasedXxx>` |
| `Option<T>` | `std::optional<T>` |
| `Result<T, E>` | `std::expected<T, E>` |
| `Box<dyn Trait>` | `std::unique_ptr<XxxDyn>` |
| `tokio`/`flume` channel | `utils::make_channel<T>()` / `utils::make_broadcast_channel<T>()` |
| `derive(Asset)` | inherit `Asset` base or register via `AssetPlugin` |
| `impl SystemParam` | ECS resource (`Res<T>`, `ResMut<T>`) |
| `Event` / `EventWriter` | `Events<T>` / `EventWriter<T>` |
| `Option<&T>` return | `std::optional<std::reference_wrapper<const T>>` or `const T*`, first one preferred |
| Module path `bevy_x::y::Z` | `epix::x::Z` |

---

## Tracking File Format

Create `documentation/tmp/BEVY_{ModuleName}_MATCH_STATUS.md` at the start. Follow this exact structure:

```markdown
# Bevy <Name> Module — C++ Match Status

Reference: `bevy_<name>` <version> vs `epix_engine/<module>` (C++23 MSVC modules)

All items target 1-to-1 entity parity. File locations may differ; entity names and behaviors must match.

## Legend

| Symbol | Meaning |
| ------ | ------- |
| ✅      | Functionally equivalent |
| ⚠️      | Works but signature/behavior diverges (note why) |
| ❌      | Declaration differs significantly — must fix |
| 🚫      | Missing entirely — must implement |
| N/A    | Non-implementable; confirmed with user |

## Cross-Cutting Translation Rules
(copy the Rust→C++ mapping table here, trimmed to what is relevant)

## <Section per logical group, e.g. "1. Core Traits", "2. Handle Types", "3. Asset Server">

| Entity | Bevy signature | C++ signature | Status | Notes |
| ------ | -------------- | ------------- | ------ | ----- |
| `StructName::method` | `fn method(&self, arg: T) -> R` | `R method(T arg) const` | ✅ | |
| `TraitName` | `trait T: Bound` | `concept T` | ⚠️ | Bound X has no C++ equivalent |
```

Group entities by Bevy source file or logical subsystem — not by C++ file. Every public entity in the Bevy crate must appear in the table.

---

## Procedure

This is an **iterative loop**. Execute it one entity at a time; do not batch-classify and then batch-implement.

---

### Phase 0 — Bootstrap

**0a. List Bevy source files using the terminal**, not just read tools (which may miss files):

```powershell
Get-ChildItem -Recurse -Filter "*.rs" <bevy_src_dir> | Select-Object FullName
```

If user attached the directory, use the attached path. Otherwise fetch `lib.rs` from GitHub.

**0b. List C++ module files using the terminal**:

```powershell
Get-ChildItem -Recurse -Filter "*.cppm" epix_engine/<module>/modules | Select-Object FullName
Get-ChildItem -Recurse -Filter "*.cpp"  epix_engine/<module>/src     | Select-Object FullName
```

**0c. Create the tracking file** `documentation/tmp/BEVY_MATCH_STATUS.md` using the format above. Leave all entity rows empty for now — they will be filled in the loop below.

---

### Phase 1 — Per-Entity Iteration Loop

Repeat the following steps for **each public entity** in the Bevy source (structs, enums, traits, free functions, type aliases — anything `pub`). Process one logical group (one Bevy source file section) at a time.

#### Step 1 — Read the Bevy entity directly

Read the actual Bevy `.rs` source for this entity. Identify:
- Full signature (fields, methods, associated types, trait bounds)
- Behavior / semantics from doc comments or tests
- Which other entities it depends on

Do not guess or rely on memory — read the source.

#### Step 2 — Read the C++ counterpart directly

Use the terminal to confirm the file exists, then read it:

```powershell
Get-ChildItem -Recurse epix_engine/<module>/modules | Where-Object { $_.Name -match "<entity>" }
```

Read the relevant `.cppm` declaration. If the entity is not found, it is `🚫 Missing`.

#### Step 3 — Classify and update the tracking file

Assign exactly one status symbol and write a note explaining any difference. Update `documentation/tmp/BEVY_MATCH_STATUS.md` immediately — do not defer tracker updates.

**Skip policy — almost never skip.** The only valid reasons to mark `N/A` are:
- The entity is physically impossible to express in C++ (e.g., wasm `fetch`, Android APK reader, Rust reflection)
- The entity is internal to Bevy's ECS scheduler and has no user-facing surface

**If you think something should be skipped for any other reason, stop and ask the user** before marking it N/A.

#### Step 4 — If status is ❌ or 🚫: implement

1. **Find the right `.cppm` file** using `epix-module-research` (read the module interface first)
2. **Add the declaration** (`export` in the correct partition)
3. **Add the implementation** (`.cpp` or `-impl.cppm`), when implementing, you should always re-read the Bevy source to confirm you are matching the behavior (1v1 match logic if possible), not just the signature
4. Build: `cmake --build build --target epix_<module>`
5. Fix any compile errors before continuing

#### Step 5 — Verify match after implementation

After any implementation change, **re-read both the Bevy source and the C++ file** to confirm:
- Signatures match (accounting for Rust→C++ mapping)
- Behavior semantics match (method names, return types, error types)
- Logic matches, function body matches
- No other entity in the same file was accidentally broken

Update the tracking file status to ✅ or ⚠️ with notes.

#### Step 6 — If new tests are needed

Add a test in `epix_engine/<module>/tests/` named `<StructName>.<MethodName>_<Scenario>`.  
Build and run: `cmake --build build --target test_<module>_*`  
Do not proceed to the next entity if tests fail.

---

### Phase 2 — Final Pass

After all entities are classified:

1. Run the terminal list commands again to confirm no new `.rs` or `.cppm` files were added during the session
2. Re-read the tracking file top to bottom and verify every row has a status
3. Run the full test suite: `cmake --build build --target test_<module>_*`
4. Confirm no entity is silently left unaddressed

---

## Common Pitfalls

- **File structure differs — that is fine.** Bevy may split across `asset.rs`, `server/mod.rs`, `loader.rs`. The C++ may consolidate into one partition. Match by **entity identity**, not file location.
- **Never copy async Bevy patterns verbatim.** `async fn`, `select!`, `spawn`, use `libs/stdexec`(or `std::execution` if using cpp26) and coroutines. Unless I **explicitly** told to use thread pool and blocking. Always confirm the intended async behavior and map to the correct C++ async pattern.
- **Bevy `#[reflect]` traits** → only skip after confirming with user. Do not assume.
- **Module partition import order matters.** Adding a new exported type used by an earlier partition may require a `-decl.cppm` forward declaration. See `epix-module-research`.
- **Do not batch updates.** Classify → implement → verify → update tracker, one entity at a time.
- **Always re-read sources after editing** — do not rely on memory of what you just wrote.

---

## Completion Check

Before finishing, confirm all of the following:

- [ ] Every `pub` entity in the Bevy crate has a row in `documentation/tmp/BEVY_MATCH_STATUS.md`
- [ ] No row has a blank status
- [ ] All ❌ and 🚫 rows are resolved (or have a user-confirmed N/A reason)
- [ ] `cmake --build build --target epix_<module>` succeeds with 0 errors
- [ ] `cmake --build build --target test_<module>_*` succeeds and all tests pass
- [ ] Terminal file-list was re-run at the end to catch any missed files
- [ ] `/memories/repo/<module>_bevy_parity.md` is updated
- [ ] Acceptable differences are noted with rationale (not silently ignored)
