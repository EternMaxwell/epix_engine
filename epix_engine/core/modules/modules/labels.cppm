module;

export module epix.core:labels;

import std;

import :label;

#ifndef EPIX_MAKE_LABEL
#define EPIX_MAKE_LABEL(type)                                                         \
    struct type : public ::core::Label {                                              \
       public:                                                                        \
        type() = default;                                                             \
        template <typename T>                                                         \
        type(T t)                                                                     \
            requires(!std::is_same_v<std::decay_t<T>, type> && std::is_object_v<T> && \
                     std::constructible_from<Label, T>)                               \
            : Label(t) {}                                                             \
    };
#endif

namespace core {
export EPIX_MAKE_LABEL(SystemSetLabel);
export EPIX_MAKE_LABEL(ScheduleLabel);
export EPIX_MAKE_LABEL(AppLabel);
}  // namespace core

// Temporary. Partial specializations are errornous in modules in most compilers currently
namespace std {
template <>
struct hash<::core::AppLabel> {
    size_t operator()(const ::core::AppLabel& label) const noexcept { return std::hash<::core::Label>()(label); }
};
template <>
struct hash<::core::ScheduleLabel> {
    size_t operator()(const ::core::ScheduleLabel& label) const noexcept { return std::hash<::core::Label>()(label); }
};
template <>
struct hash<::core::SystemSetLabel> {
    size_t operator()(const ::core::SystemSetLabel& label) const noexcept { return std::hash<::core::Label>()(label); }
};
}  // namespace std