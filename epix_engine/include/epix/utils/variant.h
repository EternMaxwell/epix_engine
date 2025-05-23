#pragma once

namespace epix::util {
template <typename... Ts>
struct visitor : Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
visitor(Ts...) -> visitor<Ts...>;
}  // namespace epix::util