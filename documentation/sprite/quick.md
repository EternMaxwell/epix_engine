# Sprite module

`epix.sprite` is the built-in textured-quad 2D renderer.

```cpp
import epix.sprite;
using namespace epix::sprite;
```

## Setup and spawning

Add the normal render/window plugin stack, then `SpritePlugin`. It installs the Core2D graph,
image/render-asset support, extraction, batching, pipelines, and draw commands.

```cpp
commands.spawn(SpriteBundle{
    .sprite = Sprite{.color = {1, 1, 1, 1}},
    .transform = transform::Transform::from_xyz(0, 0, 0),
    .texture = texture_handle,
});
```

`SpriteBundle` expands to `Sprite`, `Transform`, and `Handle<Image>`. `Sprite` exposes:

- `color`: multiplicative RGBA tint;
- `flip_x` / `flip_y`;
- optional `uv_rect` as pixel-space `(x, y, width, height)`;
- optional world-space `size` (native image size when absent); and
- `anchor`, an offset from the sprite center.

The image handle must remain valid. Add `render::camera::RenderLayer` when the entity should not use
the default layer 0.

## Render-world extension surface

During extraction each visible sprite becomes `ExtractedSprite` with its source entity, copied
properties, world model matrix, depth, texture id, image size, and layer. Compatible sprites are
grouped into `SpriteBatch`; `SpriteInstanceData` holds model, UV, tint, and quad transform data.
`SpriteGeometryBuffers` owns the shared quad, while `SpriteInstanceBuffer` owns per-frame instance
data and its bind group.

Custom phase integration can reuse `BindSpriteInstances<Slot>`, `BindSpriteTexture<Slot>`, and
`DrawSpriteBatch<PhaseItem>`. Ordinary applications do not need to create these render-world
resources manually.

