#pragma once

#include <epix/app.h>
#include <epix/wgpu.h>

namespace epix::render::resources {
using RenderAdapter     = wgpu::Adapter;
using RenderAdapterInfo = wgpu::AdapterInfo;
using RenderInstance    = wgpu::Instance;
using RenderDevice      = wgpu::Device;
using RenderQueue       = wgpu::Queue;
}  // namespace epix::render::resources