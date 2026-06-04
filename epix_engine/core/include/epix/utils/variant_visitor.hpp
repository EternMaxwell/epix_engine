#pragma once

#include <variant>

namespace epix::utils {
/** @brief Overload set helper for std::visit on std::variant.
 *
 * Combines multiple callable objects into a single visitor using
 * aggregate initialization and CTAD.
 * @tparam Visitors Callable types to combine.
 */
template <typename... Visitors>
struct visitor : Visitors... {
    using Visitors::operator()...;
};
template <typename... Visitors>
visitor(Visitors...) -> visitor<Visitors...>;
}  // namespace utils