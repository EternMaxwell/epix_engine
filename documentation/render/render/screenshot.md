# Screenshots

`ScreenshotPlugin` is installed by `WindowRenderPlugin` as part of
`RenderPlugin`. It follows Bevy's component-based screenshot lifecycle: spawn
a `Screenshot` request entity, then observe `ScreenshotCaptured`.

```cpp
#include <epix/render/screenshot.hpp>

using namespace epix::render::screenshot;

void request(ecs::World& world) {
    world.spawn(Screenshot::primary_window());
}

void receive(EventReader<ScreenshotCaptured> captures) {
    for (const auto& capture : captures.read()) {
        // capture.entity identifies the request entity.
        const image::Image& image = capture.image;
    }
}
```

The plugin adds `Capturing` while the GPU readback is in flight, then emits
`ScreenshotCaptured`, adds `Captured`, and despawns the request entity on the
following frame. Duplicate requests for the same render target are coalesced.

`Screenshot::primary_window()`, `Screenshot::window(entity)`,
`Screenshot::image(texture)`, and `Screenshot::texture_view(handle)` select a
render target. Captures use the final graph output; source formats supported by
the readback path are RGBA8/BGRA8 (including sRGB), R8, RG8, RGBA16
integer/float, and RGBA32Float. BGRA is converted to RGBA and row padding is
stripped.

Use `save_to_disk(path)` in a `ScreenshotCaptured` observer if disk output is
desired. There is no built-in hotkey or automatic file-writing policy.
