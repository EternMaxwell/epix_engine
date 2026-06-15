#pragma once

#include <epix/common.hpp>
#ifndef EPIX_CXX_MODULE
#include <epix/utils.hpp>
#endif

namespace epix::core {
EPIX_EXPORT using utils::ConQueue;
EPIX_EXPORT using utils::Sender;
EPIX_EXPORT using utils::Receiver;
EPIX_EXPORT using utils::bit_vector;
EPIX_EXPORT using utils::int_base;
EPIX_EXPORT using utils::make_channel;
}  // namespace epix::core