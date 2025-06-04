add_executable(
    epix_assets_collection
    assets.cpp
)
add_executable(
    epix_assets_loading
    loading.cpp
)

target_link_libraries(
    epix_assets_collection
    PRIVATE epix
)
target_link_libraries(
    epix_assets_loading
    PRIVATE epix
)