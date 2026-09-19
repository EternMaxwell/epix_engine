epix_add_module(epix_sprite_render)
target_link_libraries(epix_sprite_render PUBLIC epix_ecs)
target_link_libraries(epix_sprite_render PUBLIC epix_app)
target_link_libraries(epix_sprite_render PUBLIC epix_assets)
target_link_libraries(epix_sprite_render PUBLIC epix_image)
target_link_libraries(epix_sprite_render PUBLIC epix_mesh)
target_link_libraries(epix_sprite_render PUBLIC epix_camera)
target_link_libraries(epix_sprite_render PUBLIC epix_transform)
target_link_libraries(epix_sprite_render PUBLIC epix_render)
target_link_libraries(epix_sprite_render PUBLIC epix_core_graph)
target_link_libraries(epix_sprite_render PUBLIC epix_sprite)
target_link_libraries(epix_sprite_render PUBLIC webgpu)

epix_add_tests(sprite_render LINK_LIBS epix_sprite_render epix_task)

epix_add_examples(sprite_render LINK_LIBS epix_sprite_render epix_glfw epix_glfw_render epix_input)

# Every 2D example renders through `epix_sprite_render`, so this module owns the
# example files; the group and its directory name the API the example
# demonstrates (`examples/sprite` for `epix_sprite`, `examples/mesh` for
# `epix_mesh`). They are registered here rather than in their own module because
# `epix_engine/CMakeLists.txt` adds `sprite` and `mesh` before this module, so
# their CMakeLists cannot link `epix_sprite_render` yet; `examples/text` is
# registered by the text module, which is added after this one.
epix_add_examples(sprite EXAMPLES_DIR examples/sprite
  LINK_LIBS epix_sprite_render epix_glfw epix_glfw_render epix_input)
epix_add_examples(mesh EXAMPLES_DIR examples/mesh
  LINK_LIBS epix_sprite_render epix_glfw epix_glfw_render epix_input)
epix_add_examples(text EXAMPLES_DIR examples/text
  LINK_LIBS epix_text epix_sprite epix_sprite_render epix_mesh freetype harfbuzz epix_window epix_input epix_glfw epix_glfw_render epix_core_graph)
