export module epix.utils:variant_visitor;

import std;

namespace utils {
export template <typename... Visitors>
struct visitor : Visitors... {
    using Visitors::operator()...;
};
export template <typename... Visitors>
visitor(Visitors...) -> visitor<Visitors...>;
}  // namespace utils