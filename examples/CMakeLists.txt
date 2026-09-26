# Example executables — one per example source file, all linking the single
# epix_engine library. Each group directory holds a header/ (and, with
# EPIX_CXX_MODULE=ON, a module/) variant.

epix_add_examples(task)
epix_add_examples(ext)
epix_add_examples(grid)
epix_add_examples(grid_gpu)
epix_add_examples(fallingsand)

# render examples
epix_add_examples(render SOURCE_DIR render)
# SFML render examples
epix_add_examples(render_sfml)

epix_add_examples(sprite_render)
epix_add_examples(sprite SOURCE_DIR sprite_render/sprite)
epix_add_examples(mesh SOURCE_DIR sprite_render/mesh)
epix_add_examples(text SOURCE_DIR sprite_render/text)

epix_add_examples(window_glfw SOURCE_DIR window/glfw)
epix_add_examples(window_sfml SOURCE_DIR window/sfml)
