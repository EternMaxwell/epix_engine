#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/async_channel.hpp>
#endif

// Re-export async_channel into assets::async_channel namespace alias
// so existing assets code using async_channel:: still works.
namespace epix::assets::async_channel {
using namespace epix::async_channel;
}
