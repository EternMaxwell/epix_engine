cmake_minimum_required (VERSION 3.21)

# Enable C++20 modules support (must be before project())
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.30)
    set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "aa1f7df0-828a-4fcd-9afc-2dc80491aca7")
  endif()
  # Disable C++ std modules for now as Clang 18 doesn't fully support them
  # set(CMAKE_CXX_MODULE_STD 1)
endif()

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  if (MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
  endif()
endif()

project ("epix_engine.cmake" LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#sub directories

add_library(BSThreadPool INTERFACE)
target_include_directories(BSThreadPool INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libs/thread-pool/include)

add_subdirectory (libs/entt)
add_subdirectory (libs/glfw)
set (SPDLOG_USE_STD_FORMAT ON)
add_subdirectory (libs/spdlog)
add_subdirectory (libs/glm)
add_subdirectory (libs/freetype)
add_subdirectory (libs/vulkan-headers)
add_subdirectory (libs/volk)
add_subdirectory (libs/imgui)
add_subdirectory (libs/box2d)
set (EARCUT_BUILD_TESTS OFF CACHE BOOL "Build earcut tests" FORCE)
set (EARCUT_BUILD_BENCH OFF CACHE BOOL "Build earcut benchmarks" FORCE)
set (EARCUT_BUILD_VIZ OFF CACHE BOOL "Build earcut visualizer" FORCE)
add_subdirectory (libs/earcut)
add_subdirectory (libs/tracy)
add_subdirectory (libs/stb)
add_subdirectory (libs/uuid)
set (NVRHI_WITH_DX12 OFF)
set (NVRHI_WITH_DX11 OFF)
set (NVRHI_WITH_VULKAN ON)
add_subdirectory (libs/nvrhi)
add_subdirectory (libs/spirv-cross)

add_subdirectory (libs/googletest)

# Workaround for earcut's old GLFW submodule defining GLFW_USE_WAYLAND
unset(GLFW_USE_WAYLAND CACHE)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  add_compile_options(/bigobj)
elseif(MINGW)
  add_compile_options(-Wa,-mbig-obj)
endif()

if (NOT MSVC)
  add_compile_options(-Wpointer-arith)
  add_compile_options(-fpch-preprocess)
endif ()

add_subdirectory (epix_engine)
add_subdirectory (examples)
