# EPIX ENGINE MODULE NAMING CONVENTION

This document outlines the naming conventions for cxx modules within the Epix Engine project. Adhering to these conventions ensures consistency, clarity, and ease of maintenance across the codebase.

## Overview

Cxx modules in the Epix Engine are named using a structured format that reflects their functionality and scope. The naming convention follows the pattern:

```
epix.<category>:[<partition>].[...<sub-partition>].[<scope>];
```

Where:
- `epix`: The root namespace indicating that the module is part of the Epix Engine
- `<category>`: A broad classification of the module's functionality (e.g., `core`, `assets`, `render`, etc.)
- `<partition>`: Optional, for subdivising categories into more specific areas, following the cpp language feature of partitions
  - If partition is not for functionality subdivision, and is for separating interface and implementation, use the following name:
    - `decl` for foward declaration and minimal interface(e.g., some concepts that might be needed by other partition but does not require full interface)
    - `interface` for full interface that can be used by other partitions
    - `spec` for template specialization implementations, if needed, otherwise provided in `interface`
    - `impl` for implementation details that should not be exposed outside the module
  - If any `sub-partition` or `scope` is present, the `partition` should import and optionally export them as needed.
- `<sub-partition>`: Optional, for further subdividing partitions into more specific areas of functionality. Naming of sub-partitions should be descriptive of their purpose, e.g., `math`, `io`, `graphics`, etc.
- `<scope>`: A specific identifier for the partition. Naming of scope is similar to non functionality subdivision partition naming, e.g.:
  - `decl` for foward declaration and minimal interface(e.g., some concepts that might be needed by other partition but does not require full interface)
  - `interface` for full interface that can be used by other partitions
  - `spec` for template specialization implementations, if needed, otherwise provided in `interface`
  - `impl` for implementation details that should not be exposed outside the module

For file names associated with these modules and partitions, the following conventions apply:

```
[epix\<category>][\<partition>][\<sub-partition>][-<scope>].cppm
```

*If the category is not in the same target with other categories, `[epix\<category>]` folder can be omitted for brevity.*

Where the `<>` brackets indicate optional components based on the module's structure.

---

## Member Function Naming

### Accessor pairs

Prefer overloaded functions by `const`-qualification over separate names when the read and write forms differ only in mutability.

When a separate name is needed for the mutable accessor (e.g. on a virtual interface or when the return type itself differs), use the **`_mut` suffix** on the mutable overload — never the `mutable_` prefix:

```cpp
// ✅ correct
virtual const ProcessedInfo* processed_info() const = 0;
virtual std::optional<ProcessedInfo>& processed_info_mut() = 0;

Components&       components_mut();
const Components& components() const;

// ❌ incorrect — do not use mutable_ prefix
virtual std::optional<ProcessedInfo>& mutable_processed_info() = 0;
```

This mirrors the engine's existing naming style used throughout `epix_core` (`world_mut()`, `components_mut()`, `storage_mut()`, `entities_mut()`, `archetypes_mut()`, `bundles_mut()`, `get_mut()`, `access_mut()`, `required_mut()`, …) and the Bevy convention (`processed_info_mut`, `world_mut`, …) that the C++ code tracks.

### General rules

| Pattern                 | Style        | Example                                 |
| ----------------------- | ------------ | --------------------------------------- |
| Type names              | `PascalCase` | `AssetMetaDyn`, `ProcessedInfo`         |
| Variable / field names  | `snake_case` | `meta_format_version`, `processed_info` |
| Free / member functions | `snake_case` | `get_asset_hash()`, `serialize_bytes()` |
| Mutable accessor suffix | `_mut`       | `processed_info_mut()`, `world_mut()`   |
| Template type params    | `PascalCase` | `LoaderSettings`, `ProcessSettings`     |
| Concepts                | `PascalCase` | `Asset`, `AssetLoader`, `Process`       |

---

## Optional References

Prefer `std::optional<std::reference_wrapper<T>>` (or `std::optional<std::reference_wrapper<const T>>`) over raw pointer `T*` / `const T*` when the value is optional (may or may not exist). This applies primarily to return types of getter functions and API boundaries.

```cpp
// ✅ correct — optionality is explicit, no ownership ambiguity
std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_labeled(const std::string& label) const;
std::optional<std::reference_wrapper<const AssetSource>>       get_source(const AssetSourceId& id) const;

// ❌ incorrect — raw pointer is ambiguous (owned? nullable? error-sentinel?)
const ErasedLoadedAsset* get_labeled(const std::string& label) const;
const AssetSource*       get_source(const AssetSourceId& id) const;
```

Use `std::ref(value)` to construct `optional<reference_wrapper<T>>` and `std::cref(value)` for the const form. Callers access the wrapped value via `.get()` or implicit conversion.

**Exception:** a raw non-owning `T*` observer parameter (not a return value) remains acceptable in hot-path code where the overhead of `optional`-wrapping is measurable and established by profiling.