add_rules("mode.release", "mode.debug", "mode.releasedbg")
add_cxflags("/utf-8")

add_requires("wgpu-native v24.0.0+1")
add_requires("glfw 3.4")
add_requires("glm")
add_requires("entt v3.15.0")
add_requires("spdlog v1.14.1")
add_requires("freetype 2.13.1")
add_requires("box2d v3.1.0")
add_requires("tracy v0.11.1")
add_requires("stb 2025.03.14")
add_requires("thread-pool v5.0.0")
add_requires("stduuid v1.2.3")
add_requires("mapbox_earcut 2.2.4")
add_requires("gtest v1.16.0")

-- the xrepo imgui package didn't define necessary definitions for wgpu backend
-- so we manually add them here
includes("libs/imgui/xmake.lua")

option("epix_examples")
    set_default(true)
    set_showmenu(true)
    set_description("Build epix engine examples")
option_end()

option("epix_enable_tracy")
    set_default(true)
    set_showmenu(true)
    set_description("Enable Tracy profiler")
    add_defines("EPIX_ENABLE_TRACY", { public = false })
option_end()

option("epix_config")
    set_default(true)
    set_languages("cxx23", { public = true })
    set_description("Config for epix engine")
    if get_config("epix_enable_tracy") then
        add_defines("EPIX_ENABLE_TRACY", { public = false })
    end
option()

-- epix engine
includes("epix_engine/xmake.lua")

-- examples
if get_config("epix_examples") then
    includes("examples/xmake.lua")
end