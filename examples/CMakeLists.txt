cmake_minimum_required(VERSION 3.21)

# Recursively find all .cpp files
file(GLOB_RECURSE CPP_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach(CPP_FILE ${CPP_FILES})
    # Generate unique target name from relative path
    file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${CPP_FILE})
    string(REPLACE ".cpp" "" TARGET_PATH ${REL_PATH})
    string(REPLACE "/" "_" TARGET_NAME ${TARGET_PATH})  # Convert path to target name

    add_executable(example_${TARGET_NAME} ${CPP_FILE})
    target_link_libraries(example_${TARGET_NAME} PRIVATE epix)
endforeach()