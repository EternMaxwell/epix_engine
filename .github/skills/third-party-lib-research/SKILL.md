---
name: third-party-lib-research
description: 'Learn and use a third-party C++ library by reading its public interface and documentation. Use when integrating, using, or debugging a third-party dependency; when looking up API usage of a library; when a new library needs to be explored before writing code that depends on it. Do NOT read library implementation (.cpp) files unless a compilation error explicitly requires it.'
argument-hint: '<library-name> [specific feature or type to look up]'
---

# Third-Party Library Research

## Goal

Learn how to use a third-party library **without reading its implementation code**. Derive all knowledge from public-facing resources in the following priority order.

## Source Priority (Highest → Lowest)

1. **README / docs inside the repo** — `README.md`, `docs/`, `CHANGELOG.md`, any `.md` files at repo root
2. **Public headers / module interfaces** — `.h`, `.hpp`, `.ixx`, `.cppm`, `.slang` files in `include/` or `src/` (declarations only, never `.cpp`)
3. **Example and test files** — `examples/`, `tests/`, `test/`, `samples/` folders
4. **Official website** — use `fetch_webpage` only if the above are insufficient

> **Never open `.cpp`, `.c`, or private implementation files** to learn API usage. If you find yourself reading implementation, stop and look for a header or example instead.

## Procedure

### Step 1 — Locate the library root

The library is typically found in one of:
- `libs/<name>/` (vendored)
- `build/_deps/<name>-src/` (CMake FetchContent)
- A git submodule path listed in CMakeLists.txt

Use `file_search` or `grep_search` on CMakeLists.txt to confirm the path.

### Step 2 — Read README and in-repo docs

```
file_search: libs/<name>/README*
file_search: libs/<name>/docs/**/*.md
read_file: <path-to-README>
```

Extract: purpose, version, key types/classes, primary API surface, any breaking changes in CHANGELOG relevant to the version in use.

### Step 3 — Survey public headers

```
file_search: libs/<name>/include/**/*.{h,hpp,ixx,cppm}
```

Read the **top-level or facade headers first** (e.g., `<lib>.h`, `<lib>.hpp`, `all.h`). For module interfaces (`.ixx`/`.cppm`) read the `export` declarations. Do **not** follow `#include` chains into `detail/` or `impl/` subdirectories unless the type you need lives there.

### Step 4 — Check examples and tests

```
file_search: libs/<name>/examples/**
file_search: libs/<name>/tests/**
```

Prefer short, self-contained examples over large integration tests. Look for usage patterns of the specific API you intend to call.

### Step 5 — Fetch official docs (if needed)

Only if Steps 1–4 left genuine gaps, use `fetch_webpage` with the library's official documentation URL. Do not guess URLs; find them in the README.

### Step 6 — Write the code

Use only the API surface discovered through Steps 1–5. Prefer patterns seen in examples directly.

---

## On Compilation Errors

When code using the library fails to compile:

### Decision tree

```
Compilation error
│
├─ Is the error in YOUR code (wrong type, wrong call site)?
│     → Fix the call site using the public header as reference.
│
├─ Is the error inside the library's own headers / generated code?
│     │
│     ├─ Library comes from CMake FetchContent (_deps/<name>-src/)?
│     │     → Bump the version tag or commit hash in the relevant
│     │       cmake/*.cmake or CMakeLists.txt FetchContent_Declare call,
│     │       re-run cmake, rebuild. Try the latest stable release first.
│     │
│     ├─ Library is a git submodule (libs/<name>/)?
│     │     → Run: git -C libs/<name> fetch && git -C libs/<name> checkout <newer-tag>
│     │       then rebuild.
│     │
│     └─ Cannot update (pinned, no newer version fixes it)?
│           → Report the error clearly: paste the error message, the
│             offending header line, and the library version in use.
│             Then propose a workaround:
│             - Use an alternative API call that avoids the broken path
│             - Wrap the broken type/call behind a compatibility shim
│             - Conditionally compile around the issue with a version guard
│
└─ Is the error a linker error (undefined symbol)?
      → Check CMakeLists.txt for the correct target_link_libraries name.
        Look in the library's own CMakeLists.txt for exported target names.
        Do not read implementation .cpp files.
```

### Version update recipe (FetchContent)

```cmake
# In cmake/Fetch<Name>.cmake or CMakeLists.txt
FetchContent_Declare(
    <name>
    GIT_REPOSITORY https://...
    GIT_TAG        <new-tag-or-commit>   # ← bump here
)
```

After editing, run cmake configure + build the failing target.

### Version update recipe (git submodule)

```powershell
git -C libs/<name> fetch origin
git -C libs/<name> checkout <new-tag>
git add libs/<name>
# rebuild
```

---

## Checklist Before Writing Code

- [ ] Read README / in-repo docs
- [ ] Identified the primary include / module interface file(s)
- [ ] Confirmed the version currently in use (tag or commit)
- [ ] Found at least one usage example for the API to be called
- [ ] Did **not** open any `.cpp` implementation files
