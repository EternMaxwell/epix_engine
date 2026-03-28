module;

export module epix.render.imgui:state;

import epix.core;
import std;

namespace imgui {

/** @brief Thread-safe snapshot of ImDrawData for pipelined rendering.
 *  Owns cloned draw lists so the render sub-app can use them even after
 *  the main world has started a new ImGui frame. */
export struct DrawDataSnapshot {
    struct DrawListClone {
        std::vector<char> cmd_buffer;
        std::vector<char> idx_buffer;
        std::vector<char> vtx_buffer;
        int cmd_count = 0;
        int idx_count = 0;
        int vtx_count = 0;
    };
    bool valid          = false;
    float display_pos_x = 0, display_pos_y = 0;
    float display_size_x = 0, display_size_y = 0;
    float fb_scale_x = 0, fb_scale_y = 0;
    int total_vtx_count = 0;
    int total_idx_count = 0;
    std::vector<DrawListClone> draw_lists;
};

/** @brief Resource holding the ImGui context and frame state.
 *  Stored as a resource in the main world and extracted to the render world. */
export struct ImGuiState {
    void* ctx         = nullptr;
    bool initialized  = false;
    bool frame_active = false;
    std::shared_ptr<DrawDataSnapshot> draw_snapshot;

    /** @brief Set ImGui context for the current thread. */
    void activate() const;
};

/** @brief System parameter that activates ImGui on the current thread.
 *
 *  Adding this to a system signature:
 *  - Serializes the system with all other ImGui-using systems
 *  - Sets the ImGui context on the worker thread before the system runs
 *
 *  After this, ImGui:: functions can be called directly. */
export struct Ctx {};
}  // namespace imgui

namespace core {
template <>
struct SystemParam<imgui::Ctx> : ParamBase {
    using State                    = TypeId;
    using Item                     = imgui::Ctx;
    static constexpr bool readonly = false;

    static State init_state(World& world) { return world.type_registry().type_id<imgui::ImGuiState>(); }

    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World&) {
        if (access.combined_access().has_resource_read(state)) {
            throw std::runtime_error(std::format(
                "imgui::Ctx in system [{}] conflicts with a previous Res<ImGuiState> or ResMut<ImGuiState>.",
                meta.name));
        }
        access.add_unfiltered_resource_write(state);
    }

    static std::expected<void, ValidateParamError> validate_param(State& state, const SystemMeta&, World& world) {
        if (world.get_resource<imgui::ImGuiState>()) return {};
        return std::unexpected(ValidateParamError{
            .param_type = meta::type_id<imgui::Ctx>(),
            .message    = "ImGuiState resource does not exist. Did you add imgui::ImGuiPlugin?",
        });
    }

    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        world.resource<imgui::ImGuiState>().activate();
        return imgui::Ctx{};
    }
};
}  // namespace core
