---
applyTo: "**"
description: "Naming conventions for the Epix Engine: C++ module names, member function casing, accessor _mut suffix, optional references. Always apply when writing or modifying any engine code."
---
# Epix Engine Naming Conventions

Full reference: `documentation/module_naming_convention.md`. The rules below are the mandatory summary.

## C++ Module Names

```
epix.<category>:[<partition>].[<sub-partition>].[<scope>]
```

- Root is always `epix`.
- `<category>`: broad area — `core`, `assets`, `render`, `shader`, `window`, etc.
- `<partition>` / `<sub-partition>`: functional subdivisions (descriptive names, e.g., `math`, `io`).
- Non-functional partitions use reserved names:
  - `decl` — forward declarations / minimal interface required by other partitions.
  - `interface` — full public interface.
  - `spec` — template specializations (if separate from `interface`).
  - `impl` — implementation details; never exposed outside the module.

**File names** mirror the module layout:
```
[epix\<category>][\<partition>][\<sub-partition>][-<scope>].cppm
```
The `epix\<category>` prefix may be omitted when all files belong to a single dedicated build target.

## Type and Symbol Naming

| Entity | Style | Example |
|---|---|---|
| Types / classes / concepts | `PascalCase` | `AssetMetaDyn`, `ScheduleLabel` |
| Variables / fields | `snake_case` | `meta_format_version`, `world_ptr` |
| Free & member functions | `snake_case` | `get_asset_hash()`, `run_schedule()` |
| Template type parameters | `PascalCase` | `LoaderSettings`, `T` |
| Mutable accessor suffix | `_mut` | `world_mut()`, `components_mut()` |

## Accessor Pairs — `_mut` Suffix

Prefer `const`-overloading when read and write forms differ only in mutability.  
When a separate name is needed, append `_mut` to the mutable overload — **never** use a `mutable_` prefix:

```cpp
// ✅ correct
Components&       components_mut();
const Components& components() const;

// ❌ incorrect
Components& mutable_components();
```

## Optional References

Prefer `std::optional<std::reference_wrapper<T>>` over raw `T*` for return types that express optionality:

```cpp
// ✅ correct
std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_labeled(const std::string& label) const;

// ❌ incorrect
const ErasedLoadedAsset* get_labeled(const std::string& label) const;
```

Raw non-owning `T*` observer *parameters* (not return values) remain acceptable in hot-path code where overhead is measurable.
