#include <epix/pbr.hpp>
#include <format>
#include <string>

namespace epix::pbr {

std::string MeshPipelineViewLayoutKey::label() const {
    // Bevy builds the label by appending the active flags in declaration order,
    // so it doubles as a unique layout name and a readable debug label
    // (`mesh_view_bindings.rs:99-136`).
    return std::format("mesh_view_layout{}{}{}{}{}{}{}", contains(MULTISAMPLED) ? "_multisampled" : "",
                       contains(DEPTH_PREPASS) ? "_depth" : "", contains(NORMAL_PREPASS) ? "_normal" : "",
                       contains(MOTION_VECTOR_PREPASS) ? "_motion" : "", contains(DEFERRED_PREPASS) ? "_deferred" : "",
                       contains(OIT_ENABLED) ? "_oit" : "", contains(ATMOSPHERE) ? "_atmosphere" : "");
}

}  // namespace epix::pbr
