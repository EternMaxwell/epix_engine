# Flat module registration for the single epix_engine target.
#
# Every logical module — formerly its own static library with its own
# CMakeLists.txt — now only contributes its sources and its public include
# directory to epix_engine. Cross-module access is therefore controlled by the
# source files themselves (imports / includes), not by CMake link lines.

if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED COMPONENTS Xrandr)
endif()

# The one engine target lives here, in the same directory scope as its
# generated sources.
epix_create_engine_target()

# ── Modules ───────────────────────────────────────────────────────────────
set(EPIX_ENGINE_MODULE_FILES "")
set(EPIX_ENGINE_INCLUDE_DIRS "")

epix_add_module(common)
epix_add_module(utils)
epix_add_module(task)
epix_add_module(ecs)
epix_add_module(app)
epix_add_module(assets)
epix_add_module(time)
epix_add_module(input)
epix_add_module(image)
epix_add_module(window/window)
epix_add_module(window/sdl3)
epix_add_module(transform)
epix_add_module(shader)
epix_add_module(mesh)
epix_add_module(camera)
epix_add_module(sprite)
epix_add_module(render/render)
epix_add_module(render/sdl3_render)
epix_add_module(render/core_graph)
epix_add_module(render/imgui)
epix_add_module(sprite_render)
epix_add_module(pbr)
epix_add_module(text)
epix_add_module(extension/grid)
epix_add_module(extension/grid_gpu)
epix_add_module(extension/fallingsand)
epix_add_module(experimental/pixelbody)

epix_add_module_filesets()

# ── Generated sources ─────────────────────────────────────────────────────
set(_tonemapping_lut_dir "${CMAKE_CURRENT_SOURCE_DIR}/render/core_graph/assets/luts")
set(_tonemapping_lut_source "${CMAKE_CURRENT_BINARY_DIR}/generated/tonemapping_luts.cpp")
add_custom_command(
  OUTPUT "${_tonemapping_lut_source}"
  COMMAND "${CMAKE_COMMAND}"
    -DOUTPUT_FILE=${_tonemapping_lut_source}
    -DBLENDER_FILE=${_tonemapping_lut_dir}/Blender_-11_12.ktx2
    -DAGX_FILE=${_tonemapping_lut_dir}/AgX-default_contrast.ktx2
    -DTONY_FILE=${_tonemapping_lut_dir}/tony_mc_mapface.ktx2
    -P "${PROJECT_SOURCE_DIR}/cmake/generate_tonemapping_luts.cmake"
  DEPENDS
    "${_tonemapping_lut_dir}/Blender_-11_12.ktx2"
    "${_tonemapping_lut_dir}/AgX-default_contrast.ktx2"
    "${_tonemapping_lut_dir}/tony_mc_mapface.ktx2"
    "${PROJECT_SOURCE_DIR}/cmake/generate_tonemapping_luts.cmake"
  VERBATIM
)
target_sources(${EPIX_ENGINE_TARGET} PRIVATE "${_tonemapping_lut_source}")

# ── Third-party libraries ─────────────────────────────────────────────────
target_link_libraries(${EPIX_ENGINE_TARGET} PUBLIC
  BSThreadPool
  spdlog::spdlog
  stduuid
  asio
  efsw
  zpp_bits
  STDEXEC::stdexec
  STDEXEC::asioexec
  webgpu
  glm::glm
  slang::slang
  box2d
  earcut_hpp
)
# imgui's current-context pointer is process-wide state. In a shared build its
# sources are compiled into the engine and IMGUI_API becomes the engine's DLL
# interface, so the engine's ImGuiPlugin and a caller drawing UI share one
# context; in a static build the plain static target is linked as before.
if(EPIX_ENABLE_SHARED AND IMGUI_SOURCES)
  # imgui's own headers expand IMGUI_API, so the value has to be a literal
  # attribute (epix/common.hpp's EPIX_API_EXPORT is not in their include graph).
  if(MSVC)
    set(_imgui_api_export "__declspec(dllexport)")
    set(_imgui_api_import "__declspec(dllimport)")
  else()
    set(_imgui_api_export "__attribute__((visibility(\"default\")))")
    set(_imgui_api_import "")
  endif()
  target_sources(${EPIX_ENGINE_TARGET} PRIVATE ${IMGUI_SOURCES})
  target_include_directories(${EPIX_ENGINE_TARGET} PUBLIC "${IMGUI_INCLUDES}" "${IMGUI_INCLUDES}/backends")
  target_compile_definitions(${EPIX_ENGINE_TARGET} PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_WGPU "IMGUI_API=${_imgui_api_export}")
  target_compile_definitions(${EPIX_ENGINE_TARGET} INTERFACE "IMGUI_API=${_imgui_api_import}")
else()
  target_link_libraries(${EPIX_ENGINE_TARGET} PUBLIC imgui)
endif()
target_link_libraries(${EPIX_ENGINE_TARGET} PRIVATE
  Taskflow
  stbimage
  libzstd_static
  # SDL3 keeps process-wide state (its window registry and its property sets),
  # so a second copy linked into an executable would not see the windows the
  # engine created. It therefore stays inside the engine, and no public header
  # exposes SDL.
  SDL3::SDL3-static
)
# freetype/harfbuzz are private implementation details, but consumers (the text
# tests) instantiate FreeType objects directly through the public headers.
target_link_libraries(${EPIX_ENGINE_TARGET} PUBLIC
  freetype
  harfbuzz
)
if (EPIX_ENABLE_TRACY)
  target_link_libraries(${EPIX_ENGINE_TARGET} PRIVATE TracyClient)
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
  target_link_libraries(${EPIX_ENGINE_TARGET} PUBLIC stdc++exp)
endif()
if (UNIX AND NOT APPLE)
  target_link_libraries(${EPIX_ENGINE_TARGET} PRIVATE X11::X11 X11::Xrandr)
endif()

# ── Install rules ─────────────────────────────────────────────────────────
if(EPIX_ENABLE_INSTALL)
  install(TARGETS ${EPIX_ENGINE_TARGET}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    FILE_SET cxx_modules DESTINATION lib/modules/${EPIX_ENGINE_TARGET}
  )
  foreach(_include_dir IN LISTS EPIX_ENGINE_INCLUDE_DIRS)
    install(DIRECTORY "${_include_dir}/" DESTINATION include)
  endforeach()
endif()
