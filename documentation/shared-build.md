# Shared library build

The engine is built as **one** library target, `epix_engine`, either shared
(the default) or static. Logical modules no longer own targets: a module
contributes its sources and its public include directory to that single target,
so what a translation unit may use is decided in the source files (imports /
includes) instead of by CMake link lines between module libraries.

## Layout

```
CMakeLists.txt                 project options, third-party libraries, subdirs
epix_engine/CMakeLists.txt     the epix_engine target + the flat module list
epix_engine/<module>/          module sources, headers (include/), module units (modules/)
tests/<group>/{header,module}/ test executables, one per source file
examples/<group>/{header,module}/ example executables, one per source file
```

* `tests/` and `examples/` live at the repository root; every executable links
  only `epix_engine` (plus GTest for tests).
* The per-module `CMakeLists.txt` files are gone. Adding a module means adding
  one `epix_add_module(<dir>)` line to `epix_engine/CMakeLists.txt`; the helper
  globs `<dir>/src`, `<dir>/include`, `<dir>/modules` and appends them to the
  single target.
* Test / example executables keep their historical names
  (`tests_header_<group>_<file>`, `examples_module_<group>_<file>`), so existing
  capture and verification scripts keep working.

## Options

| Option | Default | Meaning |
| ------ | ------- | ------- |
| `EPIX_ENABLE_SHARED` | `ON` | Build `epix_engine` as a shared library. `OFF` builds a static library. |
| `EPIX_ENABLE_TEST` | `ON` | Build the tests in `tests/`. |
| `EPIX_ENABLE_EXAMPLE` | `ON` | Build the examples in `examples/`. |
| `EPIX_CXX_MODULE` | `ON` | Build the `modules/*.cppm` interface units into the same target. |
| `EPIX_IMPORT_STD` | `ON` | Use `import std` in module units. |

`EPIX_EXAMPLE_USE_MIMALLOC` is ignored while `EPIX_ENABLE_SHARED=ON`: a static
mimalloc linked into an example would own a different heap than the engine DLL,
so an allocation made on one side and freed on the other would go to the wrong
allocator.

## `EPIX_API`

Two macros with different jobs:

* `EPIX_EXPORT` — module visibility (`export` when the header is pulled into a
  module unit, empty otherwise). Unchanged.
* `EPIX_API` — the DLL interface: `__declspec(dllexport)` while building the
  engine, `__declspec(dllimport)` for consumers, empty for a static build.

Placement matters on MSVC, and getting it wrong is a silent no-op:

| Declaration | Form |
| ----------- | ---- |
| class / struct definition | `EPIX_EXPORT struct EPIX_API Foo {` (`class EPIX_API` alike) |
| forward declaration | `EPIX_EXPORT struct EPIX_API Foo;` |
| free function | `EPIX_EXPORT EPIX_API ret name(...)` |
| variable that cannot be imported | left as `EPIX_EXPORT` only |

`__declspec` placed *before* the class keyword is ignored for a declaration with
no declarator (`EPIX_API struct Foo` produces C4091 and exports nothing), while
for a free function the attribute belongs at the start of the declaration. Do
not put `EPIX_API` on

* a `thread_local` declaration — MSVC rejects thread-local data with a DLL
  interface (C2492);
* a variable defined with an initializer, including `inline constexpr` and the
  `inline struct Tag {} value;` label idiom — a definition of imported data is
  C2491. Constants stay duplicated, which is harmless because they are const;
* a `friend` declaration — it declares no type and the attribute is inert;
* a class or function template; explicit specializations may carry it.

Two more rules the compiler enforces, both C2375 (`redefinition; different linkage`):

* every declaration of an exported entity must agree. A forward declaration
  (`struct EPIX_API Foo;`) and a `friend` declaration of an exported *function*
  inside a class both have to carry the attribute as well, otherwise the later
  annotated declaration conflicts with them.
* annotate a type only when it actually has an out-of-line definition. A
  class-level attribute makes MSVC instantiate every member of the class,
  including defaulted comparisons, so an unexportable member turns into a hard
  error rather than a harmless extra export: `struct EPIX_API X : std::variant<…>`
  or a class holding a `std::unordered_map` of a non-comparable / move-only type
  fails with C2679/C3889, and a class with an explicit object parameter
  (`void f(this X& self)`) fails with C2340. Header-only types need no attribute —
  they are compiled into each image anyway.

Entities that are entirely header-defined and have no out-of-line definition do
not get `EPIX_API`: nobody imports their symbols. They are compiled into each
image, so a type's RTTI/vtable identity still differs across the boundary.

Three types are deliberately left unexported even though they do have out-of-line
members, because annotating them does not compile:

| Type | Why it cannot carry the attribute |
| ---- | --------------------------------- |
| `assets::AssetInfos` | holds a `std::unordered_map` whose mapped type is move-only; the attribute forces instantiation of the map's copy operations (C2679) |

`ecs::Table` and `ecs::Tables` were in the same position for a different reason:
a member with an explicit object parameter (`void f(this Table& self)`) can never
carry a DLL interface — MSVC rejects the attribute on the declaration and on the
out-of-line definition alike (C2340). Their three out-of-line members
(`Table::swap_remove`, `Table::move_to`, `Tables::get_id_or_insert`) are therefore
ordinary member functions now; call sites are unchanged (`table.move_to(row, next)`).
Inline explicit-object members are unaffected and stay as they are.

`AssetInfos`' own members that a consumer actually needs are annotated
individually, which avoids the class-level instantiation. If another member is
ever needed, it gets the same treatment.

## Header-defined state (the audit)

An `inline` variable or a function-local `static` in a header is emitted into
every image that includes it. Under a static build that is one image; under a
DLL it is two, with two different addresses. Everything that is *state* rather
than a constant has been moved into a translation unit and is reached through
the exported symbol:

| Site | Problem | Fix |
| ---- | ------- | --- |
| `task/include/epix/task/usages.hpp` | `ComputeTaskPool` / `AsyncComputeTaskPool` / `IoTaskPool` kept their singleton pointer and `once_flag` in function-local statics: each image would allocate its own pool, so `IoTaskPool::get()` disagreed across the boundary | `global_ptr()` and `get_or_init(TaskPool)` declared in the header, defined in `task/src/usages.cpp` |
| `task/include/epix/task/task_pool.hpp` | `get_thread_executor()`'s `static thread_local ThreadExecutor`, and the `t_local_ctx` / `t_local_work` thread-local members | declared in the header as accessors, defined in `task/src/task_pool.cpp` |
| `render/render/include/epix/render/pipeline.hpp` | `make_atomic_id` kept `counter` as an inline static, so engine and caller would hand out colliding pipeline ids | static data member defined in `render/render/src/pipeline.cpp` |

A `const` constant is *not* in that category and stays in its header as
`inline const`, which is where it belongs: both images then hold an identical
copy, which is harmless because nothing depends on the object's address, and no
image has to import it. That is why `Entity::PLACEHOLDER` and the six `Dir3` axis
constants are `inline const` definitions in `entity.hpp` / `direction.hpp` rather
than objects in a translation unit. (Moving them out would also have required
annotating their classes, and a class-level attribute makes MSVC reject an
in-header definition of imported data with C2491.)

Deliberately left alone:

* `inline constexpr` / `const` constants and `inline struct Tag {} value;`
  schedule labels — const, no identity requirement, and `dllimport` on them does
  not compile.
* `static CommandMeta meta` inside the `CommandQueue::push<T>` function template
  (`ecs/include/epix/ecs/world/commands.hpp`) — one copy per image, but the
  copies are equivalent value-only metadata and nothing compares their address.
* Template-local statics in general: one instance per instantiation per image is
  the only thing C++ can express there.

## What actually gets `EPIX_API`

Only an entity that has an **out-of-line definition** in an engine translation
unit: a member function, a static data member, a free function, an explicit
specialization or an explicit instantiation. Those are the only ones that emit a
symbol a consumer must import. A type whose members are all defined in the header
exports nothing, so annotating it is noise at best — and at worst it forces MSVC
to instantiate members that do not instantiate (see the table above). Data
members matter too: a `static const` member that is *declared* in the class and
*defined* in a `.cpp` does need the attribute, because data requires
`dllimport`, while an `inline const` definition in the header does not.

A class that keeps such a definition **in a header** therefore cannot take the
class-level attribute at all (C2491, and it fires in the consumer), and rewriting
the members as in-class `static constexpr` is not a general escape on MSVC: the
initializer is not a complete-class context, so it may neither call a member of
its own class (C2131, which rules out every bitflags key built through
`from_bits_retain`) nor name a member declared later (C3861/C2065). Constant
tables like `MeshPipelineKey`, `BaseMeshPipelineKey`, `Exposure`,
`GlobalAmbientLight` and `PhaseItemExtraIndex` stay out-of-class for that reason;
when one of them also has an out-of-line member, that member is annotated
individually instead, as `MeshPipelineViewLayoutKey::label()` is.

## Third-party libraries in a shared build

A shared engine and its caller are two images in one process. Anything whose
*state* has to be the same object for both sides is therefore configured to be
one object — either its own DLL or part of the engine:

| Library | State at risk | How it is handled |
| ------- | ------------- | ----------------- |
| spdlog | the logger registry and default logger. A test that captures engine log output installs a sink in its own image; with a static spdlog it would never see what the engine logs (this failed `Cluster.ClusterDiagnosticsMatchBevy`) | built shared when `EPIX_ENABLE_SHARED` (`SPDLOG_BUILD_SHARED`), deployed next to the binaries |
| wgpu-native | instance/adapter/device registries; callers create their own instances through the same API | `EPIX_WGPU_LINK_TYPE` follows the engine (`SHARED` by default when the engine is shared) |
| ImGui | the current-context pointer is a global inside the ImGui objects. With a static ImGui the engine's plugin sets the context in its own copy and a caller calling `ImGui::` directly dereferences an unset one — seven examples crashed with 0xC0000005 | its sources are compiled into `epix_engine` with `IMGUI_API` mapped onto the engine's DLL interface, so `ImGui::` in a caller reaches the engine's context |
| mimalloc | process-wide allocator override; a static copy in the executable would own a different heap than the engine | not linked into examples while the engine is shared |
| SDL3 | the window registry and the property sets that describe each window (the native Win32/X11/Cocoa handles live there). With a static SDL3 linked into both the engine and an executable, the executable's copy cannot see — or even look up a property on — a window the engine created | linked into `epix_engine` alone (`PRIVATE`), and no public header includes an SDL header, so a consumer never links SDL at all |
| freetype / harfbuzz | the text tests build `FT_Library`/`FT_Face` themselves | still static; the text tests pass (FreeType's per-object state keeps the two copies apart) |
