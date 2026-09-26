add_subdirectory (libs/imgui)

# The sources above are compiled into epix_engine itself when the engine is
# shared (see epix_engine/CMakeLists.txt): imgui's current-context pointer is
# process-wide state, and a static imgui would exist once per image — the
# engine's plugin would set the context in its own copy while a caller drawing
# UI (the examples call ImGui:: directly) would look at an unset one. IMGUI_API
# then maps onto the engine's DLL interface. The static target stays declared
# for the static build and is not built when nothing links it.
if (EPIX_ENABLE_SHARED)
    set_target_properties(imgui PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
