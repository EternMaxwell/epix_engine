#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <variant>
#endif

namespace epix::utils {
/** @brief Overload set helper for std::visit on std::variant.
 *
 * Combines multiple callable objects into a single visitor using
 * aggregate initialization and CTAD.
 * @tparam Visitors Callable types to combine.
 */
EPIX_EXPORT template <typename... Visitors>
struct visitor : Visitors... {
    using Visitors::operator()...;
};
EPIX_EXPORT template <typename... Visitors>
visitor(Visitors...) -> visitor<Visitors...>;
}  // namespace epix::utils