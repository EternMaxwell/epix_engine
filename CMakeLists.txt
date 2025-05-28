cmake_minimum_required (VERSION 3.21)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  if (MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
  endif()
endif()

project ("epix_engine.cmake" LANGUAGES CXX C)

#sub directories

add_library(BSThreadPool INTERFACE)
target_include_directories(BSThreadPool INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libs/thread-pool/include)

add_subdirectory (libs/entt)
add_subdirectory (libs/glfw)
add_subdirectory (libs/spdlog)
add_subdirectory (libs/glm)
add_subdirectory (libs/freetype)
set (WEBGPU_BACKEND "wgpu")
set (WEBGPU_LINK_TYPE "STATIC")
add_subdirectory (libs/webgpu)
add_subdirectory (libs/vulkan-headers)
add_subdirectory (libs/volk)
add_subdirectory (libs/imgui)
add_subdirectory (libs/box2d)
add_subdirectory (libs/earcut.hpp)
add_subdirectory (libs/tracy)
add_subdirectory (libs/stb)
add_subdirectory (libs/uuid)

add_subdirectory (libs/googletest)

set(CMAKE_CXX_STANDARD 23)

if (MSVC)
  add_compile_options(/bigobj)
  add_compile_options(/wd4819)
else ()
  add_compile_options(-Wa,-mbig-obj)
  add_compile_options(-w)
  add_compile_options(-Wpointer-arith)
  add_compile_options(-fpch-preprocess)
  add_compile_options(-Wno-invalid-token-paste)
endif ()

add_subdirectory (epix_engine)
add_subdirectory (examples)
