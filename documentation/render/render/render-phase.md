# Render Phase, Draw Functions & Render Commands

The render phase system is the ECS-friendly layer above the render graph.
It collects per-entity **phase items** during the `Queue` stage, sorts them
during `PhaseSort`, and executes **draw functions** during `Render`.

All types live in `epix::render::phase`.

---

## Concepts

### PhaseItem

The minimum interface a phase item must satisfy:

```cpp
template <typename T>
concept PhaseItem = requires(const T item) {
    { item.entity()        } -> std::same_as<Entity>;
    { item.sort_key()      } -> std::three_way_comparable;
    { item.draw_function() } -> std::convertible_to<DrawFunctionId>;
};
```

### BatchedPhaseItem

Extends `PhaseItem` with instanced-draw support:

```cpp
concept BatchedPhaseItem = PhaseItem<P> && requires(const P item) {
    { item.batch_size() } -> std::convertible_to<size_t>;
};
```

### CachedRenderPipelinePhaseItem

Extends `PhaseItem` with a cached pipeline reference for `SetItemPipeline`:

```cpp
concept CachedRenderPipelinePhaseItem = PhaseItem<P> && requires(const P item) {
    { item.pipeline() } -> std::convertible_to<CachedPipelineId>;
};
```

---

## DrawFunctionId

```cpp
struct DrawFunctionId : core::int_base<uint32_t> { ... };
```

A strongly-typed uint32 index into the `DrawFunctions<P>` registry.

---

## OpaqueSortKey

```cpp
struct OpaqueSortKey {
    template <std::three_way_comparable<std::strong_ordering> T>
    explicit OpaqueSortKey(T value);

    std::strong_ordering operator<=>(const OpaqueSortKey& other) const;
};
```

A move-only, type-erased sort key.  Different key types are ordered by
`std::type_info::before()` to give a stable total order across heterogeneous
phase items (e.g. opaque 2D batches from different subsystems).

---

## DrawFunction<P>

```cpp
template <PhaseItem P>
struct DrawFunction {
    virtual void prepare(const World& world) {}
    virtual std::expected<void, DrawError> draw(
        const World&, const wgpu::RenderPassEncoder&, Entity view, const P& item) = 0;
};
```

Abstract base for type-erased draw functions.  `EmptyDrawFunction<P>` is a
no-op placeholder.

### DrawError

```cpp
struct DrawError {
    enum class ErrorType {
        Skip,
        RenderCommandFailure,
        InvalidViewQuery,
        InvalidEntityQuery,
        ViewEntityMissing,
    } type;
    std::string message;

    static DrawError skip(std::string = {}) noexcept;
    static DrawError render_command_failure(std::string = {}) noexcept;
    static DrawError invalid_view_query(std::string = {}) noexcept;
    static DrawError invalid_entity_query(std::string = {}) noexcept;
    static DrawError view_entity_missing(std::string = {}) noexcept;
};
```

`Skip` errors are silently ignored; all other types are logged via spdlog.

---

## DrawFunctions<P>

```cpp
template <PhaseItem P>
struct DrawFunctions {
    // Register a draw function (idempotent by type)
    template <Draw<P> T, typename... Args>
    DrawFunctionId add(Args&&... args) const;

    // Lookup by type
    template <Draw<P> T>
    std::optional<DrawFunctionId> get_id() const;

    // Lookup by index
    std::optional<std::reference_wrapper<DrawFunction<P>>>
        get(DrawFunctionId id) const;

    // Prepare all draw functions (called before the draw loop)
    void prepare(const World& world) const;
};
```

Thread-safe (shared_mutex + shared_ptr).  All copies share the same backing
store.  Registered as a world resource in the render world.

---

## RenderPhase<P>

```cpp
template <PhaseItem T>
struct RenderPhase {
    std::vector<T> items;

    void add(const T& item);
    void sort();                           // calls T::sort(items) or std::sort by sort_key
    auto iter_entities() const;
    void render(const wgpu::RenderPassEncoder&, const World&, Entity view) const;
    void render_range(const wgpu::RenderPassEncoder&, const World&, Entity view,
                      std::size_t start = 0, std::size_t end = max) const;
};
```

`RenderPhase<P>` is attached as a component to camera view entities.  During
the `Render` stage, nodes iterate views, obtain `RenderPhase<P>`, call `sort()`
and then `render()`.

### sort_phase_items<P>

```cpp
template <PhaseItem P>
void sort_phase_items(Query<Item<RenderPhase<P>&>> phases);
```

A system template that sorts all `RenderPhase<P>` components in the world.
Register it in `RenderSet::PhaseSort`:

```cpp
render_app.add_systems(render::Render,
    into(render::phase::sort_phase_items<MyPhaseItem>)
        .in_set(render::RenderSet::PhaseSort));
```

---

## RenderCommand Concept

```cpp
template <template <typename> typename R, typename P>
concept RenderCommand = requires {
    requires PhaseItem<P>;
    // R<P>::render(item, view_query_data, optional<entity_query_data>, system_param, encoder)
    // returns std::expected<void, RenderCommandError>
    ...
};
```

A render command is a struct template `R<P>` with:
- `void prepare(const World& world)`
- `std::expected<void, RenderCommandError> render(const P& item, ViewData, optional<EntityData>, Param, const wgpu::RenderPassEncoder&)`

`ViewData` and `EntityData` are `Query` item types resolved automatically by
`RenderCommandState`.

### RenderCommandError

```cpp
struct RenderCommandError {
    enum class Type { Skip, Failure } type;
    std::string message;
};
```

---

## SetItemPipeline<P>

```cpp
template <CachedRenderPipelinePhaseItem P>
struct SetItemPipeline {
    std::expected<void, RenderCommandError>
        render(const P& item, Item<>, optional<Item<>>,
               ParamSet<Res<PipelineServer>>,
               const wgpu::RenderPassEncoder& encoder);
};
```

Built-in render command that looks up the compiled pipeline from `PipelineServer`
by `item.pipeline()` and calls `encoder.setPipeline(...)`.  If the pipeline is
not yet ready it returns `RenderCommandError::Skip`; on permanent errors it
returns `RenderCommandError::Failure`.

---

## app_add_render_commands

```cpp
template <PhaseItem P, template <typename> typename... R>
    requires (RenderCommand<R, P> && ...)
DrawFunctionId app_add_render_commands(core::App& app);
```

Registers a chain of render commands `R0 → R1 → R2 → …` as a single draw
function for phase `P`.  Returns the `DrawFunctionId` of the registered
sequence.

```cpp
// In plugin build():
render::phase::app_add_render_commands<
    Transparent2D,
    render::view::BindViewUniform<0>::Command,  // bind view UBO at slot 0
    render::phase::SetItemPipeline,             // bind pipeline
    MySpriteDrawCommand                         // issue draw call
>(app);
```

Source: `epix_engine/render/render/modules/render_phase.cppm`

---

## Draw Concept

```cpp
template <typename FuncT, typename P>
concept Draw = PhaseItem<P> &&
    requires(FuncT func, ...) {
        { func.prepare(world) };
        { func.draw(world, ctx, view, item) } -> std::same_as<std::expected<void, DrawError>>;
    };
```

Use `Draw` when implementing a full draw function (not via render commands).
