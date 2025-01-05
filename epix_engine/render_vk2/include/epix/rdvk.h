#pragma once

#include "render_vk/components.h"
#include "render_vk/resources.h"
#include "render_vk/systems.h"
#include "render_vk/vulkan_headers.h"

namespace epix {
namespace render_vk {
using namespace prelude;
struct RenderVKPlugin : Plugin {
    bool vsync = false;
    EPIX_API void build(App& app) override;
};
}  // namespace render_vk
}  // namespace epix
