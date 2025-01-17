#pragma once

#include <epix/rdvk.h>

#include "draw_dbg/components.h"
#include "draw_dbg/systems.h"

namespace epix::render::debug {
namespace vulkan {
using namespace epix::prelude;
struct DebugRenderPlugin : Plugin {
    size_t max_vertex_count = 2048 * 256;
    size_t max_model_count  = 2048 * 64;
    EPIX_API void build(App& app) override;
};
}  // namespace vulkan
namespace vulkan2 {
struct DebugPipelines {

};
}  // namespace vulkan2
}  // namespace epix::render::debug