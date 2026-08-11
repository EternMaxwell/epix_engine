# Text module

`epix.text` provides font assets, shaping/layout, glyph atlases, generated text meshes, and Core2D
text rendering.

```cpp
import epix.text;
using namespace epix::text;
```

## Plugin stack

- `font::FontPlugin` adds `ImagePlugin`, registers `Font`/`FontLoader`, owns FreeType and atlas
  resources, and registers the embedded `fonts/default.ttf` during `ready()`.
- `TextPlugin` adds `FontPlugin` and `MeshPlugin`, shapes changed text in `PostUpdate`, applies
  pending atlas changes, and regenerates text meshes in `Last`.
- `TextRenderPlugin` adds Core2D rendering and installs extraction, queueing, batching, and draw
  systems. It requires the render sub-app to exist before its `ready()` phase.

For rendered text, add `TextRenderPlugin`; its dependencies install the lower layers.

## Text components and layout

```cpp
commands.spawn(TextBundle{
    .text = Text::with_str("Hello, Epix"),
    .font = TextFont{.font = font_handle, .size = 32.0f},
    .layout = TextLayout{.justify = Justify::Center, .wrap_mode = TextWrap::WordWrap},
    .bounds = TextBounds{.width = 400.0f},
});
```

`TextFont` contains the font handle, pixel size, line height, relative/absolute line-height flag,
and anti-aliasing flag. `TextLayout` combines `Justify::{Left,Center,Right,Justified}` with
`TextWrap::{WordWrap,CharWrap,WordOrCharWrap,NoWrap}`. `TextBounds` has optional width and height.
`TextColor` is RGBA and `TextMeasure` stores computed width/height.

`Text` registers its required layout components. `shape_text()` is the direct low-level API: it
returns `ShapedText`, whose glyph span, bounding edges, ascent/descent, width, height, and line
height are read-only. Each `GlyphInfo` records glyph/cluster ids, offsets, and advances.

## Fonts and atlases

`font::Font` owns loaded bytes. `FontLoader` registers the supported font extensions.
`FontAtlasKey {size, anti_aliased}` selects an atlas. `FontAtlas` exposes glyph lookup/rasterization,
atlas rectangle/UV lookup, pending-image application, and its atlas image handle. `FontAtlasSet`
stores atlases for one face; `FontAtlasSets` maps font asset ids to sets and exposes `get`,
`get_mut`, `contains`, `add`, `erase`, and iteration. `FontSystems` labels creation and pending
atlas updates for schedule ordering.

## Rendering 2D text

`Text2d` marks rendered text and supplies a pixel offset. `Text2dBundle` expands to `Text2d`,
`Transform`, and `TextColor`; its required-component hook supplies default `Transform` and
`TextColor` when absent. The runtime produces `TextMesh` (generated mesh plus metrics) and
`TextImage` (atlas image id).

```cpp
commands.spawn(
    Text::with_str("Score: 0"),
    TextFont{.font = font_handle, .size = 24.0f},
    Text2d{.offset = {8, 4}},
    transform::Transform::from_xyz(0, 0, 0),
    TextColor{1, 1, 0.8f, 1});
```

`TextMesh::from_shaped_text()` is available for custom pipelines and exposes the generated mesh
handle and all bounding/baseline metrics.
