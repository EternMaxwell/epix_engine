epix_add_module(epix_image)
target_link_libraries(epix_image PUBLIC epix_app)
target_link_libraries(epix_image PUBLIC epix_assets)
target_link_libraries(epix_image PUBLIC glm::glm)
target_link_libraries(epix_image PRIVATE stbimage)

epix_add_tests(image LINK_LIBS epix_image)
