set (SPDLOG_USE_STD_FORMAT ON CACHE BOOL "" FORCE)
# The engine and its consumers both log through spdlog, and a consumer may
# install its own sinks to capture the engine's output. With a static spdlog each
# image would keep its own logger registry, so a sink installed by the caller
# would never see what the engine logs; follow the engine's own link type.
if (EPIX_ENABLE_SHARED)
    set(SPDLOG_BUILD_SHARED ON CACHE BOOL "" FORCE)
endif()
add_subdirectory (libs/spdlog)
