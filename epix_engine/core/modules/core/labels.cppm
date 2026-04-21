module;

#ifndef EPIX_IMPORT_STD
#include <concepts>
#include <cstddef>
#include <functional>
#include <type_traits>
#endif
export module epix.core:labels;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :label;

#ifndef EPIX_MAKE_LABEL
#define EPIX_MAKE_LABEL(type)                                                         \
    struct type : public ::epix::core::Label {                                        \
       public:                                                                        \
        type() = default;                                                             \
        template <typename T>                                                         \
        type(T t)                                                                     \
            requires(!std::is_same_v<std::decay_t<T>, type> && std::is_object_v<T> && \
                     std::constructible_from<Label, T>)                               \
            : Label(t) {}                                                             \
    };
#endif

namespace epix::core {
/** @brief Label type for identifying system sets within a schedule. */
export EPIX_MAKE_LABEL(SystemSetLabel);
/** @brief Label type for identifying schedules. */
export EPIX_MAKE_LABEL(ScheduleLabel);
/** @brief Label type for identifying sub-applications. */
export EPIX_MAKE_LABEL(AppLabel);
}  // namespace epix::core

// Temporary. Partial specializations are errornous in modules in most compilers currently
namespace std {
template <>
struct hash<::epix::core::AppLabel> {
    std::size_t operator()(const ::epix::core::AppLabel& label) const noexcept {
        return std::hash<::epix::core::Label>()(label);
    }
};
template <>
struct hash<::epix::core::ScheduleLabel> {
    std::size_t operator()(const ::epix::core::ScheduleLabel& label) const noexcept {
        return std::hash<::epix::core::Label>()(label);
    }
};
template <>
struct hash<::epix::core::SystemSetLabel> {
    std::size_t operator()(const ::epix::core::SystemSetLabel& label) const noexcept {
        return std::hash<::epix::core::Label>()(label);
    }
};
}  // namespace std