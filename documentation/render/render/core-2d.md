# Core 2D render graph

`epix.core_graph` supplies the implemented camera-driven 2D graph used by meshes, sprites, and
text. It extends the current `epix.render` API.

```cpp
import epix.core_graph;
using namespace epix::core_graph::core_2d;
```

`CoreGraphPlugin` is a convenience plugin that adds `Core2dPlugin`. Add it only after
`render::RenderPlugin` has created the render sub-app; feature plugins such as `SpritePlugin` also
add Core2D themselves.

## Camera and graph

```cpp
app.add_plugins(core_graph::CoreGraphPlugin{});
commands.spawn(Camera2DBundle{});
```

`Camera2DBundle` contains `Camera`, `Projection`, `CameraRenderGraph{Core2d}`, `Transform`,
`VisibleEntities`, the `Camera2D` marker, and `RenderLayer::all()`. Inserting only `Camera2D`
supplies required default `Camera` and `CameraRenderGraph` components, but the full bundle is the
normal setup.

`Core2d.add_to(graph)` installs a named subgraph with this order:

`StartMainPass -> MainOpaquePass -> MainTransparentPass -> EndMainPass -> ScreenUIPass`

The labels are `Core2dNodes`. `Node2D<P>` obtains the current view's target, depth texture, and
`RenderPhase<P>`, opens a load/store render pass, renders the phase, and flushes the encoder.

## Built-in phases

- `Opaque2D` sorts by `OpaqueSortKey` for front-to-back rendering and batching.
- `Transparent2D` returns `-depth` as its sort key for back-to-front rendering.
- `UI2DItem` sorts by integer `order`.

All three store an entity, cached pipeline, draw-function id, and batch count. `Core2dPlugin`
creates their `DrawFunctions` resources, inserts the three phases for extracted Core2D views in
`RenderSet::ManageViews`, and sorts them in `RenderSet::PhaseSort`.

