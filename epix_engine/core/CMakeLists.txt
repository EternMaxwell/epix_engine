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

# additional core unit tests
add_executable(test_bitvector tests/test_bitvector.cpp)
target_link_libraries(test_bitvector PRIVATE epix_core)
add_test(NAME test_bitvector COMMAND test_bitvector)

add_executable(test_type_registry tests/test_type_registry.cpp)
target_link_libraries(test_type_registry PRIVATE epix_core)
add_test(NAME test_type_registry COMMAND test_type_registry)

add_executable(test_sparse_set tests/test_sparse_set.cpp)
target_link_libraries(test_sparse_set PRIVATE epix_core)
add_test(NAME test_sparse_set COMMAND test_sparse_set)

add_executable(test_dense tests/test_dense.cpp)
target_link_libraries(test_dense PRIVATE epix_core)
add_test(NAME test_dense COMMAND test_dense)

add_executable(test_sparse_array tests/test_sparse_array.cpp)
target_link_libraries(test_sparse_array PRIVATE epix_core)
add_test(NAME test_sparse_array COMMAND test_sparse_array)

add_executable(test_table tests/test_table.cpp)
target_link_libraries(test_table PRIVATE epix_core)
add_test(NAME test_table COMMAND test_table)

add_executable(test_type_registry_mt tests/test_type_registry_mt.cpp)
target_link_libraries(test_type_registry_mt PRIVATE epix_core)
add_test(NAME test_type_registry_mt COMMAND test_type_registry_mt)

add_executable(test_bundle tests/test_bundle.cpp)
target_link_libraries(test_bundle PRIVATE epix_core)
add_test(NAME test_bundle COMMAND test_bundle)

add_executable(test_bundle_info tests/test_bundle_info.cpp)
target_link_libraries(test_bundle_info PRIVATE epix_core)
add_test(NAME test_bundle_info COMMAND test_bundle_info)

# new tests for archetype and bundle inserter
add_executable(test_archetype tests/test_archetype.cpp)
target_link_libraries(test_archetype PRIVATE epix_core)
add_test(NAME test_archetype COMMAND test_archetype)

add_executable(test_bundle_inserter tests/test_bundle_inserter.cpp)
target_link_libraries(test_bundle_inserter PRIVATE epix_core)
add_test(NAME test_bundle_inserter COMMAND test_bundle_inserter)

add_executable(test_entity_ref tests/test_entity_ref.cpp)
target_link_libraries(test_entity_ref PRIVATE epix_core)
add_test(NAME test_entity_ref COMMAND test_entity_ref)

add_executable(test_access tests/test_access.cpp)
target_link_libraries(test_access PRIVATE epix_core)
add_test(NAME test_access COMMAND test_access)

add_executable(test_randomized_spawn tests/test_randomized_spawn.cpp)
target_link_libraries(test_randomized_spawn PRIVATE epix_core)
add_test(NAME test_randomized_spawn COMMAND test_randomized_spawn)
