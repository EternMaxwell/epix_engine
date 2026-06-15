#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/async_broadcast.hpp>
#endif

// Re-export async_broadcast into assets::async_broadcast namespace alias
// so existing assets code using async_broadcast:: still works.
namespace epix::assets::async_broadcast {
using namespace epix::async_broadcast;
}
