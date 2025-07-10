#pragma once

#include <functional>

#include "epix/common.h"

namespace epix::render::gpu {
struct CommandBufferDesc {};
struct CommandBuffer {
    // functions
};
struct SemaphoreDesc {};
struct Semaphore {};
struct BufferDesc {};
struct Buffer {};
struct ImageDesc {};
struct Image {};
struct ImageViewDesc {};
struct ImageView {
};  // this image view can refer to both buffer view and image view
struct Context {
    // datas
    // we will have three queues here, for graphics, compute, and transfer.
    // and they can be optional.

    // immediate submit commands
    void graphics_commands(std::function<void(CommandBuffer&)> func);
    void compute_commands(std::function<void(CommandBuffer&)> func);
    void transfer_commands(std::function<void(CommandBuffer&)> func);
    // create_<obj> functions
};
};  // namespace epix::render::gpu