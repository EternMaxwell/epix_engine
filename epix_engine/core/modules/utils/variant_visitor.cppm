module;
#ifndef EPIX_IMPORT_STD
#include <variant>
#endif

export module epix.utils:variant_visitor;
#ifdef EPIX_IMPORT_STD
import std;
#endif
namespace epix::utils {
/** @brief Overload set helper for std::visit on std::variant.
 *
 * Combines multiple callable objects into a single visitor using
 * aggregate initialization and CTAD.
 * @tparam Visitors Callable types to combine.
 */
export template <typename... Visitors>
struct visitor : Visitors... {
    using Visitors::operator()...;
};
export template <typename... Visitors>
visitor(Visitors...) -> visitor<Visitors...>;
}  // namespace utils