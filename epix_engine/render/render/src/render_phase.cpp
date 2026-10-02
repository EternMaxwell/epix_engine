
#include <epix/render/render_phase.hpp>

namespace epix::render::phase {

std::string_view to_str(DrawError error) noexcept {
    switch (error.type) {
        case DrawError::ErrorType::Skip:
            return "Skipped";
        case DrawError::ErrorType::RenderCommandFailure:
            return "Render command execution failed";
        case DrawError::ErrorType::InvalidViewQuery:
            return "Invalid view query";
        case DrawError::ErrorType::ViewEntityMissing:
            return "View entity missing in view query";
        case DrawError::ErrorType::InvalidEntityQuery:
            return "Invalid entity query";
        default:
            return "Unknown error";
    }
}

std::string to_string(const DrawError& error) {
    switch (error.type) {
        case DrawError::ErrorType::RenderCommandFailure:
            return error.message.empty() ? std::string{"Failed to execute render command"} : error.message;
        case DrawError::ErrorType::InvalidViewQuery:
            return "Failed to get execute view query";
        case DrawError::ErrorType::InvalidEntityQuery:
            return "Failed to get execute entity query";
        case DrawError::ErrorType::ViewEntityMissing:
            return "View entity not found";
        case DrawError::ErrorType::Skip:
        default:
            return std::string(to_str(error));
    }
}

void TrackedRenderPass::reset_tracking() noexcept {
    if (!m_stores_state) return;
    m_pipeline = nullptr;
    for (auto& [group, offsets] : m_bind_groups) {
        (void)group;
        group = nullptr;
        offsets.clear();
    }
    for (auto& buffer : m_vertex_buffers) {
        buffer.reset();
    }
    m_index_buffer.reset();
    m_stores_state = false;
}

}  // namespace epix::render::phase
