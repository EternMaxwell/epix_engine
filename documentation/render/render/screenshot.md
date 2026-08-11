# Screenshots

`epix.render.screenshot` captures a window swapchain or supplied WebGPU texture into an
`image::Image` asset.

```cpp
import epix.render.screenshot;
using namespace epix::render::screenshot;

app.add_plugins(ScreenshotPlugin{}); // add after RenderPlugin
```

`ScreenshotPlugin` registers `ScreenCapture` and `ScreenCaptureResult`. Its defaults save PNGs to
`screenshots/` and request the primary window when F12 is just pressed. Configure
`save_path = nullopt` to disable automatic saving or `capture_key = nullopt` to disable the hotkey.
The hotkey path requires `InputPlugin` so `ButtonInput<KeyCode>` exists.

## Event-driven capture

```cpp
void request(EventWriter<ScreenCapture> out) {
    out.write(ScreenCapture{}); // primary window
}

void receive(EventReader<ScreenCaptureResult> results) {
    for (const auto& result : results.read()) {
        assets::Handle<image::Image> image = result.handle;
        // Retain the strong handle if the image is needed after this system.
    }
}
```

Set `ScreenCapture::target` with the same `camera::RenderTarget` constructors used by cameras to
capture a particular window or a `wgpu::Texture`. Requests are extracted to the render world,
copied during `RenderSet::Cleanup`, and delivered to the main world on the following extract pass.

Supported source formats are RGBA8/BGRA8 (including sRGB), R8, RG8, RGBA16 integer/float, and
RGBA32Float. BGRA is converted to RGBA and row padding is stripped. The resulting image has
`ImageUsage::Main`.

If auto-save is enabled, saving uses `IoTaskPool` when initialized and otherwise falls back to a
synchronous save. `ScreenshotHotkey {key}` is the mutable resource controlling the active hotkey.

