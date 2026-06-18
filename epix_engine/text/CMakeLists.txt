epix_add_module(epix_text)
target_link_libraries(epix_text PUBLIC epix_core)
target_link_libraries(epix_text PUBLIC epix_assets)
target_link_libraries(epix_text PUBLIC epix_image)
target_link_libraries(epix_text PUBLIC epix_transform)
target_link_libraries(epix_text PUBLIC epix_mesh)
target_link_libraries(epix_text PRIVATE freetype)
target_link_libraries(epix_text PRIVATE harfbuzz)
target_link_libraries(epix_text PRIVATE webgpu)

epix_add_tests(text LINK_LIBS epix_text epix_sprite epix_mesh freetype harfbuzz)

epix_add_examples(text LINK_LIBS epix_text epix_sprite epix_mesh freetype harfbuzz epix_window epix_input epix_glfw epix_glfw_render epix_core_graph)