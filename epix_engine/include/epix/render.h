#pragma once

#include <epix/app.h>
#include <epix/render/graph.h>
#include <epix/render/pipeline.h>
#include <epix/render/window.h>
#include <epix/vulkan.h>

namespace epix::render {
struct RenderPlugin : public epix::Plugin {
    bool validation = false;
    EPIX_API RenderPlugin& enable_validation(bool enable = true);
    EPIX_API void build(epix::App&) override;
    EPIX_API void finalize(epix::App&) override;
};
}  // namespace epix::render