# Test ownership

Place unit tests under the module that owns the API being tested, not under a
downstream renderer merely because the behavior was found during a render audit.
Use the existing `epix_add_tests` helper and link the owning module.

| Behavior under test | Directory | Target group |
| --- | --- | --- |
| Camera, projection, visibility, render-target values | `epix_engine/camera/tests` | `camera` |
| Window references | `epix_engine/window/window/tests` | `window` |
| Image data, samplers, ImagePlugin | `epix_engine/image/tests` | `image` |
| App extraction access and scheduling | `epix_engine/app/tests` | `app` |
| Core2D, fullscreen materials, blit and upscaling | `epix_engine/render/core_graph/tests` | `core_graph` |
| Mesh assets and allocation | `epix_engine/mesh/tests` | `mesh` |
| Render graph, phases, buffers, GPU assets and render-world extraction | `epix_engine/render/render/tests` | `render` |

Integration tests belong to the module implementing the integration. For example,
render-side camera extraction and window surface configuration stay in render;
standalone camera projection and window-reference tests do not. Split mixed tests
at that boundary without discarding assertions. A render unit-test target must
not link core_graph just to accommodate misplaced core-pipeline tests.

The current verification build is MSVC without C++ modules. Header test files
produce `tests_header_<group>_<filename>` targets. After adding or moving tests,
reconfigure CMake, build all affected test targets with `--parallel 10`, and run
them through CTest with `--output-on-failure`. Keep existing module-mode tests in
the corresponding owner directory; their relocation alone is not module-build
verification.

For rendering changes, also rebuild affected examples and inspect several frames
from each actual rendering window. Keep the example's original purpose; create a
dedicated example when new verification would change that purpose. Screenshot
stability or a uniform success color alone does not prove rendering correctness.
