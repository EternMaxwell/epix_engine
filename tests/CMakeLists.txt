# Test executables — one per test source file, all linking the single
# epix_engine library. Each group directory holds a header/ (and, with
# EPIX_CXX_MODULE=ON, a module/) variant.

epix_add_tests(utils)
epix_add_tests(task)
epix_add_tests(ecs)
epix_add_tests(app)
epix_add_tests(assets)
epix_add_tests(time)
epix_add_tests(image)
epix_add_tests(window)
epix_add_tests(transform)
epix_add_tests(shader)
epix_add_tests(mesh)
epix_add_tests(camera)
epix_add_tests(sprite)
epix_add_tests(render)
epix_add_tests(core_graph)
epix_add_tests(sprite_render)
epix_add_tests(pbr)
# The text tests exercise private headers of the text module directly.
epix_add_tests(text INCLUDE_DIRS "${EPIX_ENGINE_SOURCE_DIR}/epix_engine/text/src")
epix_add_tests(grid)
epix_add_tests(grid_gpu)
