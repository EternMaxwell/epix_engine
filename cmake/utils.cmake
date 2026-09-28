# Detect system architecture
function(detect_system_architecture)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
            set(ARCH "aarch64" PARENT_SCOPE)
        else()
            set(ARCH "x86_64" PARENT_SCOPE)
        endif()
    else()
        set(ARCH "i686" PARENT_SCOPE)
    endif()
endfunction()

# Build library filename based on platform and link type
function(build_lib_filename OUT_VAR LIB_NAME IS_SHARED)
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if (IS_SHARED)
            set(${OUT_VAR} "${LIB_NAME}.dll" PARENT_SCOPE)
        else()
            if (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
                set(${OUT_VAR} "${LIB_NAME}.lib" PARENT_SCOPE)
            else()
                set(${OUT_VAR} "lib${LIB_NAME}.a" PARENT_SCOPE)
            endif()
        endif()
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if (IS_SHARED)
            set(${OUT_VAR} "lib${LIB_NAME}.dylib" PARENT_SCOPE)
        else()
            set(${OUT_VAR} "lib${LIB_NAME}.a" PARENT_SCOPE)
        endif()
    else()
        if (IS_SHARED)
            set(${OUT_VAR} "lib${LIB_NAME}.so" PARENT_SCOPE)
        else()
            set(${OUT_VAR} "lib${LIB_NAME}.a" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# ── Transitive shared-lib collector ──────────────────────────────────────

function(_epix_collect_shared_deps OUT_VAR)
  set(_result "")
  set(_visited "")
  _epix_collect_shared_deps_recurse(_result _visited ${ARGN})
  if(_result)
    list(REMOVE_DUPLICATES _result)
  endif()
  set(${OUT_VAR} "${_result}" PARENT_SCOPE)
endfunction()

function(_epix_collect_shared_deps_recurse RESULT_VAR VISITED_VAR)
  foreach(_LIB IN LISTS ARGN)
    if(NOT TARGET ${_LIB})
      continue()
    endif()
    get_target_property(_alias ${_LIB} ALIASED_TARGET)
    if(_alias)
      set(_LIB ${_alias})
    endif()
    if(${_LIB} IN_LIST _visited)
      continue()
    endif()
    list(APPEND _visited ${_LIB})
    set(_visited "${_visited}" PARENT_SCOPE)

    get_target_property(_type ${_LIB} TYPE)
    if(_type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY")
      list(APPEND _result ${_LIB})
    endif()

    get_target_property(_link_libs ${_LIB} LINK_LIBRARIES)
    if(_link_libs)
      _epix_collect_shared_deps_recurse(_sub_result _visited ${_link_libs})
      if(_sub_result)
        list(APPEND _result ${_sub_result})
      endif()
    endif()
    get_target_property(_iface_libs ${_LIB} INTERFACE_LINK_LIBRARIES)
    if(_iface_libs)
      _epix_collect_shared_deps_recurse(_sub_iface _visited ${_iface_libs})
      if(_sub_iface)
        list(APPEND _result ${_sub_iface})
      endif()
    endif()
  endforeach()
  set(${RESULT_VAR} "${_result}" PARENT_SCOPE)
  set(${VISITED_VAR} "${_visited}" PARENT_SCOPE)
endfunction()

# ── Engine helpers ────────────────────────────────────────────────────────
#
# The engine is built as exactly one library target (EPIX_ENGINE_TARGET). The
# logical modules no longer own targets: epix_add_module() appends a module's
# sources and its public include directory to that one target, so which module
# a translation unit may use is decided at source level (imports/includes)
# rather than by CMake link lines between module libraries.

function(epix_create_engine_target)
  if(EPIX_ENABLE_SHARED)
    add_library(${EPIX_ENGINE_TARGET} SHARED)
    # EPIX_SHARED_LIB reaches consumers (so their headers declare dllimport);
    # EPIX_BUILD_SHARED only the engine's own sources (so they declare dllexport).
    target_compile_definitions(${EPIX_ENGINE_TARGET} PUBLIC EPIX_SHARED_LIB)
    target_compile_definitions(${EPIX_ENGINE_TARGET} PRIVATE EPIX_BUILD_SHARED)
  else()
    add_library(${EPIX_ENGINE_TARGET} STATIC)
  endif()
  epix_parse_options(${EPIX_ENGINE_TARGET})
endfunction()

# Appends one logical module (a directory holding include/, src/ and/or
# modules/) to the single engine target.
function(epix_add_module MODULE_DIR)
  if(NOT TARGET ${EPIX_ENGINE_TARGET})
    message(FATAL_ERROR "epix_add_module(${MODULE_DIR}) called before ${EPIX_ENGINE_TARGET} was created")
  endif()
  set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_DIR}")
  if(NOT IS_DIRECTORY "${_dir}")
    message(FATAL_ERROR "epix_add_module: '${MODULE_DIR}' is not a directory")
  endif()

  file(GLOB_RECURSE _MODULES CONFIGURE_DEPENDS "${_dir}/modules/*.ixx" "${_dir}/modules/*.cppm")
  file(GLOB_RECURSE _SOURCES CONFIGURE_DEPENDS "${_dir}/src/*.c" "${_dir}/src/*.cpp")
  file(GLOB_RECURSE _HEADERS CONFIGURE_DEPENDS "${_dir}/include/**/*.hpp" "${_dir}/include/**/*.h")

  if(IS_DIRECTORY "${_dir}/include")
    target_include_directories(${EPIX_ENGINE_TARGET} PUBLIC "${_dir}/include")
    set(EPIX_ENGINE_INCLUDE_DIRS "${EPIX_ENGINE_INCLUDE_DIRS};${_dir}/include" PARENT_SCOPE)
  endif()
  if(_SOURCES)
    target_sources(${EPIX_ENGINE_TARGET} PRIVATE ${_SOURCES})
  endif()
  if(_HEADERS)
    target_sources(${EPIX_ENGINE_TARGET} PUBLIC ${_HEADERS})
  endif()
  set(EPIX_ENGINE_MODULE_FILES "${EPIX_ENGINE_MODULE_FILES};${_MODULES}" PARENT_SCOPE)
endfunction()

# Registers every collected module interface unit with the single target.
function(epix_add_module_filesets)
  if(NOT EPIX_CXX_MODULE OR NOT EPIX_ENGINE_MODULE_FILES)
    return()
  endif()
  set_source_files_properties(${EPIX_ENGINE_MODULE_FILES} PROPERTIES COMPILE_DEFINITIONS "EPIX_CXX_MODULE")
  target_sources(${EPIX_ENGINE_TARGET}
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES ${EPIX_ENGINE_MODULE_FILES}
  )
endfunction()

function(epix_parse_options TARGET)
  if(EPIX_ENABLE_TRACY)
    target_compile_definitions(${TARGET} PRIVATE EPIX_ENABLE_TRACY)
  endif()
  if(EPIX_USE_VOLK)
    target_compile_definitions(${TARGET} PUBLIC EPIX_USE_VOLK)
  endif()
  if (EPIX_IMPORT_STD AND EPIX_CXX_MODULE)
    target_compile_definitions(${TARGET} PUBLIC EPIX_IMPORT_STD)
  endif()
  if(EPIX_ENABLE_TEST)
    target_compile_definitions(${TARGET} PRIVATE EPIX_ENABLE_TEST)
  endif()
endfunction()

# Shared body of epix_add_tests()/epix_add_examples(): one executable per
# source file of a group, next to the group's header/ and module/ variants.
function(_epix_add_group_executables GROUP_NAME)
  cmake_parse_arguments(PARSE_ARGV 1 _G "TEST;SERIAL" "SOURCE_DIR;EXCLUDE_REGEX;GLOB_TYPE" "LINK_LIBS;INCLUDE_DIRS;SHARED_DEPS")
  if(NOT _G_SOURCE_DIR)
    set(_G_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${GROUP_NAME}")
  elseif(NOT IS_ABSOLUTE "${_G_SOURCE_DIR}")
    set(_G_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${_G_SOURCE_DIR}")
  endif()
  if(NOT _G_GLOB_TYPE)
    set(_G_GLOB_TYPE "GLOB_RECURSE")
  endif()
  if(NOT IS_DIRECTORY "${_G_SOURCE_DIR}")
    return()
  endif()

  # Header dir always built, module dir only when EPIX_CXX_MODULE=ON. The two
  # variants are kept in separate variables: a "dir:variant" string cannot be
  # split on ':' because an absolute Windows path already contains one.
  set(_variants "header")
  if(EPIX_CXX_MODULE)
    list(APPEND _variants "module")
  endif()

  foreach(_variant IN LISTS _variants)
    set(_dir "${_G_SOURCE_DIR}/${_variant}")
    if(NOT IS_DIRECTORY "${_dir}")
      continue()
    endif()
    file(${_G_GLOB_TYPE} _SRCS CONFIGURE_DEPENDS "${_dir}/*.cpp")
    if(_G_EXCLUDE_REGEX)
      list(FILTER _SRCS EXCLUDE REGEX "${_G_EXCLUDE_REGEX}")
    endif()
    foreach(_SRC IN LISTS _SRCS)
      file(RELATIVE_PATH _REL "${_dir}" "${_SRC}")
      if(_G_TEST)
        set(_NAME "tests_${_variant}_${GROUP_NAME}_${_REL}")
      else()
        set(_NAME "examples_${_variant}_${GROUP_NAME}_${_REL}")
      endif()
      string(REPLACE "/" "_" _NAME "${_NAME}")
      string(REPLACE "\\" "_" _NAME "${_NAME}")
      string(REPLACE ".cpp" "" _NAME "${_NAME}")
      add_executable(${_NAME} "${_SRC}")
      target_link_libraries(${_NAME} PRIVATE ${_G_LINK_LIBS})
      if(_G_INCLUDE_DIRS)
        target_include_directories(${_NAME} PRIVATE ${_G_INCLUDE_DIRS})
      endif()
      if(_G_TEST)
        add_test(NAME ${_NAME} COMMAND ${_NAME}
          WORKING_DIRECTORY "${EPIX_ENGINE_SOURCE_DIR}")
        # A group whose tests wait on asset loads while pumping schedules from helper
        # threads cannot share a machine with other GPU tests: under `ctest -j` the
        # shader group stalled the suite at 110/111 six times while passing 11/11 alone.
        # `RUN_SERIAL` keeps other tests off the machine while these run.
        if(_G_SERIAL)
          set_tests_properties(${_NAME} PROPERTIES RUN_SERIAL TRUE)
        endif()
      endif()
      # collect shared libs
      set(_shared_deps ${_G_SHARED_DEPS})
      _epix_collect_shared_deps(_auto_deps ${_G_LINK_LIBS})
      if(_auto_deps)
        list(APPEND _shared_deps ${_auto_deps})
      endif()
      if(_shared_deps)
        list(REMOVE_DUPLICATES _shared_deps)
        epix_deploy_runtime_libs(${_NAME} DEPENDENCY_TARGETS ${_shared_deps})
      endif()
    endforeach()
  endforeach()
endfunction()

function(epix_add_tests GROUP_NAME)
  if(NOT EPIX_ENABLE_TEST)
    return()
  endif()
  cmake_parse_arguments(PARSE_ARGV 1 _T "SERIAL" "SOURCE_DIR" "INCLUDE_DIRS;SHARED_DEPS")
  set(_extra "")
  if(_T_SERIAL)
    list(APPEND _extra SERIAL)
  endif()
  if(_T_SOURCE_DIR)
    list(APPEND _extra SOURCE_DIR "${_T_SOURCE_DIR}")
  endif()
  if(_T_INCLUDE_DIRS)
    list(APPEND _extra INCLUDE_DIRS ${_T_INCLUDE_DIRS})
  endif()
  if(_T_SHARED_DEPS)
    list(APPEND _extra SHARED_DEPS ${_T_SHARED_DEPS})
  endif()
  _epix_add_group_executables(${GROUP_NAME} TEST ${_extra}
    LINK_LIBS GTest::gtest_main ${EPIX_ENGINE_TARGET}
  )
endfunction()

function(epix_add_examples GROUP_NAME)
  if(NOT EPIX_ENABLE_EXAMPLE)
    return()
  endif()
  cmake_parse_arguments(PARSE_ARGV 1 _E "" "SOURCE_DIR;EXCLUDE_REGEX;GLOB_TYPE" "INCLUDE_DIRS;SHARED_DEPS")
  set(_libs ${EPIX_ENGINE_TARGET})
  # A static mimalloc in the executable would own a different heap than the
  # engine DLL, so allocations crossing that boundary would be freed by the
  # wrong allocator. Only a static engine can use it.
  if(EPIX_EXAMPLE_USE_MIMALLOC AND TARGET mimalloc-static AND NOT EPIX_ENABLE_SHARED)
    list(APPEND _libs mimalloc-static)
  endif()
  set(_extra "")
  if(_E_SOURCE_DIR)
    list(APPEND _extra SOURCE_DIR "${_E_SOURCE_DIR}")
  endif()
  if(_E_EXCLUDE_REGEX)
    list(APPEND _extra EXCLUDE_REGEX "${_E_EXCLUDE_REGEX}")
  endif()
  if(_E_GLOB_TYPE)
    list(APPEND _extra GLOB_TYPE "${_E_GLOB_TYPE}")
  endif()
  if(_E_INCLUDE_DIRS)
    list(APPEND _extra INCLUDE_DIRS ${_E_INCLUDE_DIRS})
  endif()
  if(_E_SHARED_DEPS)
    list(APPEND _extra SHARED_DEPS ${_E_SHARED_DEPS})
  endif()
  _epix_add_group_executables(${GROUP_NAME} ${_extra}
    LINK_LIBS ${_libs}
  )
endfunction()

# ── Runtime library deployment ──────────────────────────────────────────────

set(EPIX_RUNTIME_SHARED_LIBS "" CACHE INTERNAL "Shared library targets the engine needs at runtime")

function(epix_deploy_runtime_libs TARGET_NAME)
  cmake_parse_arguments(PARSE_ARGV 1 _DEPLOY "" "" "DEPENDENCY_TARGETS")
  foreach(DEP IN LISTS _DEPLOY_DEPENDENCY_TARGETS)
    add_custom_command(
      TARGET ${TARGET_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${DEP}>
        $<TARGET_FILE_DIR:${TARGET_NAME}>
      COMMENT "Copying runtime library ${DEP} to $<TARGET_FILE_DIR:${TARGET_NAME}>..."
    )
  endforeach()
endfunction()

function(deploy_epix_runtime_deps TARGET_NAME)
  foreach(DEP IN LISTS EPIX_RUNTIME_SHARED_LIBS)
    add_custom_command(
      TARGET ${TARGET_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${DEP}>
        $<TARGET_FILE_DIR:${TARGET_NAME}>
      COMMENT "Deploying epix runtime library ${DEP} to $<TARGET_FILE_DIR:${TARGET_NAME}>..."
    )
  endforeach()
endfunction()
