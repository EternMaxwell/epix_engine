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