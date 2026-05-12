# Single entry point for all third-party library setup.
# Each file under cmake/libs/ reproduces the exact setup previously inline
# in the root CMakeLists.txt. Functions from cmake/utils.cmake are already
# available because root includes utils.cmake before this file.

include(${CMAKE_CURRENT_LIST_DIR}/libs/glfw.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/sfml.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/spdlog.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/freetype.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/imgui.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/box2d.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/earcut.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/tracy.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/stb.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/uuid.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/harfbuzz.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/googletest.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/efsw.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/zpp_bits.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/taskflow.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/glm.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/asio.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/webgpu.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/slang.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/stdexec.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libs/bs_thread_pool.cmake)
