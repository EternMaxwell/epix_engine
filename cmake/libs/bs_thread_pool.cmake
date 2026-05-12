add_library(BSThreadPool STATIC)
target_include_directories(BSThreadPool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/thread-pool/include)
file(GLOB_RECURSE BSTHREADPOOL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libs/thread-pool/modules/*.cppm")
target_sources(BSThreadPool
  PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES ${BSTHREADPOOL_SOURCES}
)
target_compile_definitions(BSThreadPool PRIVATE BS_THREAD_POOL_NATIVE_EXTENSIONS)
if (EPIX_IMPORT_STD)
  target_compile_definitions(BSThreadPool PRIVATE BS_THREAD_POOL_IMPORT_STD)
endif()
