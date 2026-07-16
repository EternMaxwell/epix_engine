#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#endif

#include <epix/ecs/type_id.hpp>

namespace epix::ecs {
EPIX_EXPORT struct RequiredComponentsRegistrator;
EPIX_EXPORT template <typename T>
struct Component {
    static void register_required_components(TypeId, RequiredComponentsRegistrator&) {}
};
}  // namespace epix::ecs
