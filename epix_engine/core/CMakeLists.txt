file(GLOB_RECURSE MODULE_SOURCES "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_core STATIC ${MODULE_SOURCES})
epix_parse_options(epix_core)
target_include_directories(epix_core PUBLIC ${MODULE_INCLUDES})

# --- tests ---
add_executable(test_untypedvec tests/test_untypedvec.cpp)
target_link_libraries(test_untypedvec PRIVATE epix_core)
add_test(NAME test_untypedvec COMMAND test_untypedvec)

# performance comparison test between untyped_vector and std::vector
add_executable(test_untypedvec_perf tests/test_untypedvec_perf.cpp)
target_link_libraries(test_untypedvec_perf PRIVATE epix_core)
add_test(NAME test_untypedvec_perf COMMAND test_untypedvec_perf)