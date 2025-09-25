#pragma once

#include <concepts>

namespace epix::core::storage {
template <std::convertible_to<size_t> I, typename V>
struct SparseArray;
};