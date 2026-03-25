epix_add_module(epix_assets)
target_link_libraries(epix_assets PUBLIC epix_core)
target_link_libraries(epix_assets PUBLIC stduuid)
# target_link_libraries(epix_assets PUBLIC asio)
target_link_libraries(epix_assets PUBLIC efsw)

# --- tests: auto-discover ---
# Recursively find all .cpp test sources under tests/ and create an executable + ctest entry
# file(GLOB_RECURSE CORE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
# foreach(TEST_SRC IN LISTS CORE_TEST_SOURCES)
#   # create a unique target name based on relative path: replace separators and strip extension
#   file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
#   string(CONCAT TEST_NAME "test_assets_" "${TEST_REL}")
#   string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
#   string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
#   string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")
  
#   add_executable(${TEST_NAME} "${TEST_SRC}")
#   target_link_libraries(${TEST_NAME} PRIVATE epix_assets)
#   target_link_libraries(${TEST_NAME} PRIVATE GTest::gtest_main)
#   add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
# endforeach()

if(EPIX_ENABLE_TEST)
	# Add explicit test for memory VFS
	add_executable(test_assets_memory_vfs tests/memory_vfs_tests.cpp)
	epix_parse_options(test_assets_memory_vfs)
	# Test sources (module interfaces are built by the epix_assets module target)
	target_sources(test_assets_memory_vfs
		PRIVATE tests/memory_vfs_tests.cpp
	)
	target_link_libraries(test_assets_memory_vfs PRIVATE epix_assets)
	target_link_libraries(test_assets_memory_vfs PRIVATE GTest::gtest_main)

	# Ensure module library is built before compiling/running the test target
	add_dependencies(test_assets_memory_vfs epix_assets)
	add_test(NAME test_assets_memory_vfs COMMAND test_assets_memory_vfs)

	# Additional test: thread-safety and watcher tests
	add_executable(test_assets_memory_vfs_thread_watcher tests/memory_vfs_thread_watcher_tests.cpp)
	epix_parse_options(test_assets_memory_vfs_thread_watcher)
	target_sources(test_assets_memory_vfs_thread_watcher
		PRIVATE tests/memory_vfs_thread_watcher_tests.cpp
	)
	target_link_libraries(test_assets_memory_vfs_thread_watcher PRIVATE epix_assets)
	target_link_libraries(test_assets_memory_vfs_thread_watcher PRIVATE GTest::gtest_main)

	add_dependencies(test_assets_memory_vfs_thread_watcher epix_assets)
	add_test(NAME test_assets_memory_vfs_thread_watcher COMMAND test_assets_memory_vfs_thread_watcher)
endif()