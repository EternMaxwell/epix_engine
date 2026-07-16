#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#endif

#include <epix/ecs/core/type_id.hpp>

namespace epix::ecs {
EPIX_EXPORT struct RequiredComponentsRegistrator;
EPIX_EXPORT template <typename T>
struct Component {
    static void register_required_components(TypeId self, RequiredComponentsRegistrator& registrator) {
        if constexpr (requires { T::register_required_components(registrator, self); }) {
            T::register_required_components(registrator, self);
        } else if constexpr (requires { T::register_required_components(self, registrator); }) {
            T::register_required_components(self, registrator);
        } else if constexpr (requires { T::register_required_components(registrator); }) {
            T::register_required_components(registrator);
        }
    }
};
}  // namespace epix::ecs
