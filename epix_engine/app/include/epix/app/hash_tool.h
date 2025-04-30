#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

#include "entity.h"
#include "label.h"

template <typename T>
    requires std::derived_from<T, epix::app::Label> ||
             std::same_as<std::decay_t<T>, epix::app::Entity>
struct std::hash<T> {
    inline size_t operator()(const T& label) const
        noexcept(noexcept(label.hash_code())) {
        return label.hash_code();
    }
};