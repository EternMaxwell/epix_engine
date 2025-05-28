#pragma once

#include <epix/app.h>
#include <epix/render/window.h>
#include <epix/vulkan.h>

namespace epix::render {
struct RenderPlugin : public epix::Plugin {
    bool validation = false;
    EPIX_API void build(epix::App&) override;
};
}  // namespace epix::render