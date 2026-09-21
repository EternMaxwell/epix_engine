set (GLM_ENABLE_CXX_20 ON)
add_subdirectory (libs/glm)
target_sources(glm
  PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/glm/glm/glm.cppm"
)
target_compile_features(glm PUBLIC cxx_std_23)
set_target_properties(glm PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
# Right-handed with a zero-to-one depth range, matching Bevy: glm's unqualified
# functions must not be left-handed, which is what made Transform::look_at point
# its +Z axis at the target while the engine's cameras are -Z forward.
target_compile_definitions(glm PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
