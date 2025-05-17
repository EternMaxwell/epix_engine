#pragma once

#include <epix/app.h>
#include <epix/render/window.h>
#include <epix/wgpu.h>

namespace epix::render {
struct RenderPlugin : public epix::Plugin {
    EPIX_API void build(epix::App&) override;
};
}  // namespace epix::render