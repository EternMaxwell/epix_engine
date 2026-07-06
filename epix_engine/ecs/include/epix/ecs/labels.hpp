#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <epix/common.hpp>
#include <functional>
#include <type_traits>
#endif

#include <epix/ecs/label.hpp>

#ifndef EPIX_MAKE_LABEL
#define EPIX_MAKE_LABEL(type)                                                         \
    struct type : public ::epix::ecs::Label {                                         \
       public:                                                                        \
        type() noexcept = default;                                                    \
        template <typename T>                                                         \
        type(T t) noexcept                                                            \
            requires(!std::is_same_v<std::decay_t<T>, type> && std::is_object_v<T> && \
                     std::constructible_from<Label, T>)                               \
            : Label(t) {}                                                             \
    };
#endif

namespace epix::ecs {
/** @brief Label type for identifying system sets within a schedule. */
EPIX_EXPORT EPIX_MAKE_LABEL(SystemSetLabel);
/** @brief Label type for identifying schedules. */
EPIX_EXPORT EPIX_MAKE_LABEL(ScheduleLabel);
}  // namespace epix::ecs