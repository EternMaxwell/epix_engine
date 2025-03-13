project(
    wgpu-native-cmake
)

include(ExternalProject)
ExternalProject_Add(
    wgpu-git
    GIT_REPOSITORY https://github.com/gfx-rs/wgpu-native.git
    GIT_TAG v24.0.0.2
    GIT_SUBMODULES_RECURSE ON

    CONFIGURE_COMMAND ""

    BUILD_IN_SOURCE ON
    BUILD_COMMAND cargo build && cargo build --release
    BUILD_BYPRODUCTS
        wgpu-git-prefix/src/wgpu/target/debug/wgpu_native.lib
        wgpu-git-prefix/src/wgpu/target/release/wgpu_native.lib

    UPDATE_COMMAND ""
    INSTALL_COMMAND ""
)

ExternalProject_Get_Property(wgpu-git SOURCE_DIR)
add_library(wgpu STATIC IMPORTED)
add_dependencies(wgpu wgpu-git)
set_target_properties(
    wgpu
    PROPERTIES 
        IMPORTED_LOCATION ${SOURCE_DIR}/target/release/wgpu_native.lib
        IMPORTED_LOCATION_DEBUG ${SOURCE_DIR}/target/debug/wgpu_native.lib
        IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
)
target_include_directories(
    wgpu
    INTERFACE
        ${SOURCE_DIR}/ffi
        ${SOURCE_DIR}/ffi/webgpu-headers
)
target_link_libraries(
    wgpu
    INTERFACE
        d3dcompiler
        ws2_32
        userenv
        bcrypt
        ntdll
        opengl32
)