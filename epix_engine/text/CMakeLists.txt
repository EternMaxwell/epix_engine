epix_add_module(epix_text)
target_link_libraries(epix_text PUBLIC epix_common)
target_link_libraries(epix_text PUBLIC epix_ecs)
target_link_libraries(epix_text PUBLIC epix_app)
target_link_libraries(epix_text PUBLIC epix_assets)
target_link_libraries(epix_text PUBLIC epix_image)
target_link_libraries(epix_text PUBLIC epix_transform)
target_link_libraries(epix_text PUBLIC epix_camera)
# Bevy extracts 2D text inside `bevy_sprite_render` (`extract_text2d_sprite`,
# `bevy_sprite_render/src/text2d/mod.rs:25`) and draws it with `SpritePipeline`,
# so the text module depends on the sprite and sprite-render modules.
target_link_libraries(epix_text PUBLIC epix_sprite)
target_link_libraries(epix_text PUBLIC epix_sprite_render)
target_link_libraries(epix_text PUBLIC epix_core_graph)
target_link_libraries(epix_text PRIVATE freetype)
target_link_libraries(epix_text PRIVATE harfbuzz)
target_link_libraries(epix_text PRIVATE webgpu)

epix_add_tests(text LINK_LIBS epix_text epix_sprite epix_sprite_render epix_mesh freetype harfbuzz)

epix_add_examples(text LINK_LIBS epix_text epix_sprite epix_sprite_render epix_mesh freetype harfbuzz epix_window epix_input epix_glfw epix_glfw_render epix_core_graph)
