#pragma once

#include <epix/common.h>

#include <memory>
#include <variant>


namespace epix::assets {
template <typename T>
struct Handle {
    std::variant<std::shared_ptr<T>, std::weak_ptr<T>> data;
};
}  // namespace epix::assets