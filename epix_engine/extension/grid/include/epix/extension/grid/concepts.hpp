#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <utility>

#include <epix/traits.hpp>

namespace epix::ext::grid {
/** @brief Error codes returned by grid operations. */
enum class grid_error {
    OutOfBounds,           /**< Position is outside the grid bounds. */
    InvalidPos,            /**< Position is invalid. */
    EmptyCell,             /**< The cell at the given position is empty. */
    AlreadyOccupied,       /**< The cell at the given position is already occupied. */
    NotSupportedOperation, /**< The requested operation is not supported by this grid type. */
};
// ============================================================
// Grid concepts
// ============================================================

namespace detail {
/**
 * @brief Validates that a type R is an acceptable return from get() for
 *        cell type Cell — either `expected<reference_wrapper<const Cell>, grid_error>`
 *        (stable reference into storage) or `expected<Cell, grid_error>` (computed value).
 */
template <typename R, typename Cell>
concept valid_get_return = std::same_as<R, std::expected<std::reference_wrapper<const Cell>, grid_error>> ||
                           std::same_as<R, std::expected<std::reference_wrapper<Cell>, grid_error>> ||
                           std::same_as<R, std::expected<Cell, grid_error>>;

/** @brief Derive pos_type from the return type of dimensions(). */
template <typename G>
using grid_dimensions_type = std::remove_cvref_t<decltype(std::declval<G&>().dimensions())>;

/** @brief Derive pos_type: prefer explicit `pos_type` member, else from contains() arg. */
template <typename G, typename = void>
struct _grid_pos_type {
    // Fallback: extract from contains()'s argument using function_traits
    using type = std::remove_cvref_t<
        std::tuple_element_t<0, typename function_traits<decltype(&std::decay_t<G>::contains)>::args_tuple>>;
};
template <typename G>
struct _grid_pos_type<G, std::void_t<typename std::decay_t<G>::pos_type>> {
    using type = typename std::decay_t<G>::pos_type;
};
template <typename G>
using grid_pos_type = typename _grid_pos_type<G>::type;

/** @brief Unwrap get()'s return type to extract the cell value type. */
template <typename T>
struct unwrap_get_return;
template <typename Cell>
struct unwrap_get_return<std::expected<std::reference_wrapper<const Cell>, grid_error>> {
    using type = Cell;
};
template <typename Cell>
struct unwrap_get_return<std::expected<std::reference_wrapper<Cell>, grid_error>> {
    using type = Cell;
};
template <typename Cell>
struct unwrap_get_return<std::expected<Cell, grid_error>> {
    using type = Cell;
};

/** @brief Derive cell_type: prefer explicit member, else unwrap from get() return via function_traits. */
template <typename G, typename = void>
struct _grid_cell_type {
    using type =
        typename unwrap_get_return<decltype(std::declval<G>().get(std::declval<const grid_pos_type<G>&>()))>::type;
};
template <typename G>
struct _grid_cell_type<G, std::void_t<typename std::decay_t<G>::cell_type>> {
    using type = typename std::decay_t<G>::cell_type;
};
template <typename G>
using grid_cell_type = typename _grid_cell_type<G>::type;

/** @brief Unwrap an expected<reference_wrapper<const Cell>, error> → const Cell&, etc. */
template <typename T>
struct unwrap_get_access_type;
template <typename Cell>
struct unwrap_get_access_type<std::expected<std::reference_wrapper<const Cell>, grid_error>> {
    using type = const Cell&;
};
template <typename Cell>
struct unwrap_get_access_type<std::expected<std::reference_wrapper<Cell>, grid_error>> {
    using type = Cell&;
};
template <typename Cell>
struct unwrap_get_access_type<std::expected<Cell, grid_error>> {
    using type = Cell;
};

/** @brief The unwrapped access type from get() — `const cell_type&`, `cell_type&`, or `cell_type`. */
template <typename G>
using grid_get_type = typename unwrap_get_access_type<
    std::remove_cvref_t<decltype(std::declval<G>().get(std::declval<const grid_pos_type<G>&>()))>>::type;

template <typename T>
struct add_const {
    using type = const T;
};
template <typename T>
struct add_const<T&> {
    using type = const T&;
};
template <typename T>
using add_const_t = typename add_const<T>::type;

}  // namespace detail

// ============================================================
// Public type helpers
// ============================================================

template <typename G>
using grid_pos_type = detail::grid_pos_type<G>;

template <typename G>
using grid_dimensions_type = detail::grid_dimensions_type<G>;

/**
 * @brief Structural concept: query dimensions, containment, and cell values.
 *
 * Checks `get(pos)` on `G`
 * access via a non-const `get()` overload or read-only access via `get() const`.
 *
 * A `viewable_grid` exposes:
 *  - `dimensions()`  → array<unsigned_integral, N>
 *  - `contains(pos)` → bool
 *  - `get(pos)`      → expected<reference_wrapper<const cell_type>, grid_error>
 *                       OR expected<cell_type, grid_error>
 */
template <typename G>
concept viewable_grid = requires(std::remove_reference_t<G>& g, const std::remove_reference_t<G>& cg) {
    requires std::tuple_size_v<detail::grid_dimensions_type<G>> == std::tuple_size_v<detail::grid_pos_type<G>>;
    { cg.dimensions() } -> std::same_as<detail::grid_dimensions_type<G>>;
    { cg.contains(std::declval<const detail::grid_pos_type<G>&>()) } -> std::same_as<bool>;
    { g.get(std::declval<const detail::grid_pos_type<G>&>()) } -> detail::valid_get_return<detail::grid_cell_type<G>>;
};

/**
 * @brief Full read/write container concept.
 *
 * Extends `viewable_grid` with mutation primitives:
 *  - `set(pos, value)`     → expected<reference_wrapper<cell_type>, grid_error>
 *  - `set_new(pos, value)` → expected<reference_wrapper<cell_type>, grid_error>
 *  - `remove(pos)`         → expected<void, grid_error>
 *  - `take(pos)`           → expected<cell_type, grid_error>
 *  - `clear()`             → void
 */
template <typename G>
concept grid_container = viewable_grid<G> && requires(std::remove_reference_t<G>& g) {
    {
        g.set(std::declval<const detail::grid_pos_type<G>&>(), std::declval<detail::grid_cell_type<G>>())
    } -> std::same_as<std::expected<std::reference_wrapper<detail::grid_cell_type<G>>, grid_error>>;
    {
        g.set_new(std::declval<const detail::grid_pos_type<G>&>(), std::declval<detail::grid_cell_type<G>>())
    } -> std::same_as<std::expected<std::reference_wrapper<detail::grid_cell_type<G>>, grid_error>>;
    { g.remove(std::declval<const detail::grid_pos_type<G>&>()) } -> std::same_as<std::expected<void, grid_error>>;
    {
        g.take(std::declval<const detail::grid_pos_type<G>&>())
    } -> std::same_as<std::expected<detail::grid_cell_type<G>, grid_error>>;
    { g.clear() } -> std::same_as<void>;
};

/**
 * @brief Unsafe bounds-unchecked access.
 */
template <typename G>
concept unsafe_viewable_grid = viewable_grid<G> && requires(std::remove_reference_t<G>& g) {
    { g.get_unsafe(std::declval<const detail::grid_pos_type<G>&>()) } -> std::convertible_to<detail::grid_get_type<G>>;
};

/**
 * @brief Full unsafe container: unchecked set/remove/take.
 */
template <typename G>
concept unsafe_grid_container = unsafe_viewable_grid<G> && requires(std::remove_reference_t<G>& g) {
    {
        g.set_unsafe(std::declval<const detail::grid_pos_type<G>&>(), std::declval<detail::grid_cell_type<G>>())
    } -> std::same_as<detail::grid_cell_type<G>&>;
    { g.remove_unsafe(std::declval<const detail::grid_pos_type<G>&>()) } -> std::same_as<void>;
    { g.take_unsafe(std::declval<const detail::grid_pos_type<G>&>()) } -> std::same_as<detail::grid_cell_type<G>>;
};

/**
 * @brief Iteration: produces positions, cell values, and (pos, cell) pairs.
 *
 *  - `iter_pos()`   → input_range of pos_type
 *  - `iter_cells()` → input_range of cell_type& or const cell_type&
 *  - `iter()`       → input_range of (pos_type, cell_type&) or (pos_type, const cell_type&)
 */
template <typename G>
concept iterable_grid = viewable_grid<G> && requires(std::remove_reference_t<G>& g) {
    { g.iter_pos() } -> std::ranges::input_range;
    { g.iter_cells() } -> std::ranges::input_range;
    { g.iter() } -> std::ranges::input_range;
    requires std::same_as<std::ranges::range_value_t<decltype(g.iter_pos())>, detail::grid_pos_type<G>>;
    requires std::convertible_to<std::ranges::range_reference_t<decltype(g.iter_cells())>, detail::grid_get_type<G>>;
    requires std::same_as<std::remove_cvref_t<std::tuple_element_t<0, std::ranges::range_value_t<decltype(g.iter())>>>,
                          detail::grid_pos_type<G>>;
    requires std::convertible_to<std::tuple_element_t<1, std::ranges::range_reference_t<decltype(g.iter())>>,
                                 detail::grid_get_type<G>>;
};

/**
 * @brief A grid that exposes the number of occupied cells via `count()`.
 *
 * `count()` returns the current number of stored/occupied cells as a
 * type convertible to `std::size_t`.
 */
template <typename G>
concept counted_grid = viewable_grid<G> && requires(const std::remove_reference_t<G>& g) {
    { g.count() } -> std::convertible_to<std::size_t>;
};

/**
 * @brief Composite concept: full read/write grid with iteration.
 *
 * Satisfied by any type that is simultaneously:
 *   `viewable_grid` + `grid_container` + `iterable_grid` + `counted_grid`.
 */
template <typename G>
concept basic_grid = viewable_grid<G> && grid_container<G> && iterable_grid<G> && counted_grid<G> &&
                     viewable_grid<detail::add_const_t<G>> && iterable_grid<detail::add_const_t<G>>;

// ============================================================
// grid_trait — constrained on viewable_grid, derives facts from the interface
// ============================================================

/**
 * @brief Compile-time traits for any grid type, constrained on `viewable_grid<G>`.
 *
 * No user specialization is needed — all fields are derived from the grid interface.
 *
 * Type members:
 *   - `pos_type`   — `std::array<std::int32_t, dim>` (the grid's position type)
 *   - `coord_type` — element type of `pos_type` (always `std::int32_t`)
 *   - `cell_type`  — the value type stored or computed by the grid (derived from get())
 *   - `value_type` — alias for `cell_type`
 *   - `get_type`   — the unwrapped element type from get(): `const cell_type&`
 *                    for reference grids, or `cell_type` for value grids
 *   - `mut_get_type` — `cell_type&`, the mutable element type
 *
 * Constants:
 *   - `dim`            — number of spatial dimensions
 *
 * Methods (conditionally enabled by the corresponding concept):
 *   - `contains` / `get`                           — always: const → const access, non-const → mutable access
 *   - `set` / `set_new` / `remove` / `take`        — grid_container
 *   - `get_unsafe` / `get_mut_unsafe`              — unsafe_viewable/mutable_viewable_grid
 *   - `set_unsafe` / `remove_unsafe` / `take_unsafe`                  — unsafe_grid_container
 *   - `iter_pos` / `iter_cells` / `iter`           — always: const → const iter, non-const → mutable iter
 */
template <viewable_grid G>
struct grid_trait {
    using pos_type   = detail::grid_pos_type<G>;
    using coord_type = typename pos_type::value_type;
    using cell_type  = detail::grid_cell_type<G>;
    using value_type = cell_type;
    /** @brief Value type returned by get() */
    using get_type = typename detail::unwrap_get_access_type<
        std::remove_cvref_t<decltype(std::declval<G>().get(std::declval<const pos_type&>()))>>::type;
    using get_value_type = typename decltype(std::declval<G>().get(std::declval<const pos_type&>()))::value_type;

    static constexpr std::size_t dim = std::tuple_size_v<pos_type>;

    auto contains(G& g, const pos_type& pos) const -> bool { return g.contains(pos); }

    auto get(G& g, const pos_type& pos) const { return g.get(pos); }
    template <typename... Args>
        requires std::constructible_from<value_type, Args...> && grid_container<G>
    auto set(G& g, const pos_type& pos, Args&&... value) const
        -> std::expected<std::reference_wrapper<value_type>, grid_error> {
        return g.set(pos, std::forward<Args>(value)...);
    }
    template <typename... Args>
        requires std::constructible_from<value_type, Args...> && grid_container<G>
    auto set_new(G& g, const pos_type& pos, Args&&... value)
        -> std::expected<std::reference_wrapper<value_type>, grid_error> {
        return g.set_new(pos, std::forward<Args>(value)...);
    }
    auto get_unsafe(G& g, const pos_type& pos) const -> value_type&
        requires unsafe_viewable_grid<G>
    {
        return g.get_unsafe(pos);
    }
    template <typename... Args>
        requires std::constructible_from<value_type, Args...> && unsafe_grid_container<G>
    auto set_unsafe(G& g, const pos_type& pos, Args&&... value) const -> value_type& {
        return g.set_unsafe(pos, std::forward<Args>(value)...);
    }
    auto remove_unsafe(G& g, const pos_type& pos) const -> void
        requires unsafe_grid_container<G>
    {
        return g.remove_unsafe(pos);
    }
    auto take_unsafe(G& g, const pos_type& pos) const -> value_type
        requires unsafe_grid_container<G>
    {
        return g.take_unsafe(pos);
    }

    auto iter_pos(G& g) const
        requires iterable_grid<G>
    {
        return g.iter_pos();
    }
    auto iter_cells(G& g) const
        requires iterable_grid<G>
    {
        return g.iter_cells();
    }
    auto iter(G& g) const
        requires iterable_grid<G>
    {
        return g.iter();
    }
};

// ============================================================
// recursive_grid — helper variable template (recursive, allowed)
// Concepts cannot refer to themselves, but variable templates can.
// ============================================================

namespace detail {
template <typename G, std::size_t Depth>
inline constexpr bool is_recursive_grid_v = []() -> bool {
    if constexpr (!viewable_grid<G>) {
        return false;
    } else if constexpr (Depth == 0) {
        return true;
    } else {
        return is_recursive_grid_v<detail::grid_cell_type<G>, Depth - 1>;
    }
}();
}  // namespace detail

/**
 * @brief Recursively checks that G is a viewable_grid whose cell_type is also a viewable_grid,
 *        up to `Depth` levels of nesting.
 *
 * - `Depth=0`: only requires `viewable_grid<G>` (base case).
 * - `Depth=1`: requires `viewable_grid<G>` and `viewable_grid<G::cell_type>`.
 * - `Depth=N`: requires N+1 levels of viewable_grid nesting.
 *
 * Example:
 *   `recursive_grid<packed_grid<2, packed_grid<2, int>>>`    // true  (cell_type is a grid)
 *   `recursive_grid<packed_grid<2, int>>`                     // false (cell_type is int, not a grid)
 */
template <typename G, std::size_t Depth = 1>
concept recursive_grid = detail::is_recursive_grid_v<G, Depth>;
}  // namespace epix::ext::grid