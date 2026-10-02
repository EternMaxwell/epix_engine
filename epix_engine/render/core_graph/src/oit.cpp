#include <epix/core_graph/oit.hpp>

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <cstdint>
#endif

namespace epix::core_graph {

// Bevy `is_oit_supported` (`oit/resolve/mod.rs:65-91`): the adapter's
// `DownlevelFlags::FRAGMENT_WRITABLE_STORAGE` first, then the device's
// `max_storage_buffers_per_shader_stage` against `OIT_REQUIRED_STORAGE_BUFFERS`,
// with Bevy's two warning texts emitted when the caller asks for them.
bool is_oit_supported(bool fragment_writable_storage,
                      std::uint32_t max_storage_buffers_per_shader_stage,
                      bool warn) {
    if (!fragment_writable_storage) {
        if (warn) {
            spdlog::warn(
                "[core_graph] OrderIndependentTransparencyPlugin not loaded. GPU lacks support: "
                "DownlevelFlags::FRAGMENT_WRITABLE_STORAGE.");
        }
        return false;
    }
    if (max_storage_buffers_per_shader_stage < OIT_REQUIRED_STORAGE_BUFFERS) {
        if (warn) {
            spdlog::warn(
                "[core_graph] OrderIndependentTransparencyPlugin not loaded. RenderDevice lacks support: "
                "max_storage_buffers_per_shader_stage ({}) < OIT_REQUIRED_STORAGE_BUFFERS ({}).",
                max_storage_buffers_per_shader_stage, OIT_REQUIRED_STORAGE_BUFFERS);
        }
        return false;
    }
    return true;
}

}  // namespace epix::core_graph
