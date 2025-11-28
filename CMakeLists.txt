cmake_minimum_required (VERSION 3.21)

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
set (EARCUT_BUILD_TESTS OFF)
set (EARCUT_BUILD_BENCH OFF)
set (EARCUT_BUILD_VIZ OFF)
add_subdirectory (libs/earcut)
add_subdirectory (libs/tracy)
add_subdirectory (libs/stb)
add_subdirectory (libs/uuid)
set (NVRHI_WITH_DX12 OFF)
set (NVRHI_WITH_DX11 OFF)
set (NVRHI_WITH_VULKAN ON)
add_subdirectory (libs/nvrhi)
add_subdirectory (libs/spirv-cross)
add_subdirectory (libs/utfcpp)
set (HARFBUZZ_HAVE_FREETYPE ON)
add_subdirectory (libs/harfbuzz)

add_subdirectory (libs/googletest)

set(CMAKE_CXX_STANDARD 23)

if (MSVC)
  add_compile_options(/bigobj)
else ()
  add_compile_options(-Wa,-mbig-obj)
  add_compile_options(-Wpointer-arith)
  add_compile_options(-fpch-preprocess)
endif ()

add_subdirectory (epix_engine)
# add_subdirectory (examples)
