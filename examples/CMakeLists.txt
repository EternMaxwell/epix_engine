cmake_minimum_required(VERSION 3.21)

project("examples")

set(EPIX_ENABLE_TRACY ON)

# add_subdirectory(window-gl)
add_subdirectory(ecs)
# add_subdirectory(font-gl)
# add_subdirectory(task-queue)
add_subdirectory(vk-integration-trial)