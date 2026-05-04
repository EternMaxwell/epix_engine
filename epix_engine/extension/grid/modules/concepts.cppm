module;

#ifndef EPIX_IMPORT_STD
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#endif

export module epix.extension.grid:concepts;
#ifdef EPIX_IMPORT_STD
import std;
#endif

namespace epix::ext::grid {
/** @brief Error codes returned by grid operations. */
export enum class grid_error {
    OutOfBounds,     /**< Position is outside the grid bounds. */
    InvalidPos,      /**< Position is invalid. */
    EmptyCell,       /**< The cell at the given position is empty. */
    AlreadyOccupied, /**< The cell at the given position is already occupied. */
};
// ============================================================
// Grid concepts — purely structural, no grid_trait dependency
// ============================================================

/**
 * @brief Fundamental structural concept satisfied by any grid type.
 *
 * Requires:
 *  - pos_type nested type
 *  - cell_type nested type
 *  - dimensions() method returning std::array<pos_type::value_type, Dim> (the extent along each axis).
 *  - contains(pos) method for occupancy query, returning bool.
 *  - get(pos) method for const access to cell values, returning expected<reference_wrapper<const T>, grid_error>.
 *  - get_mut(pos) method for mutable access to cell values, returning expected<reference_wrapper<T>, grid_error>.
 *  - set(pos, ...) method for mutable access to cell values, returning expected<reference_wrapper<T>, grid_error>.
 *  - remove(pos) method for removing cell values, returning expected<void, grid_error>.
 *  - take(pos) method for removing and returning cell values, returning expected<T, grid_error>.
 */
export template <typename G>
concept any_grid = requires(const G& g, G& gm) {
    typename G::pos_type;
    typename G::cell_type;

    // dimensions() must return array<unsigned_integral, std::tuple_size_v<pos_type>>
    requires std::unsigned_integral<typename std::remove_cvref_t<decltype(g.dimensions())>::value_type>;
    requires std::tuple_size_v<std::remove_cvref_t<decltype(g.dimensions())>> ==
                 std::tuple_size_v<typename G::pos_type>;
    { g.contains(std::declval<const typename G::pos_type&>()) } -> std::same_as<bool>;
    {
        g.get(std::declval<const typename G::pos_type&>())
    } -> std::same_as<std::expected<std::reference_wrapper<const typename G::cell_type>, grid_error>>;
    {
        gm.get_mut(std::declval<const typename G::pos_type&>())
    } -> std::same_as<std::expected<std::reference_wrapper<typename G::cell_type>, grid_error>>;
    {
        gm.set(std::declval<const typename G::pos_type&>(), std::declval<typename G::cell_type>())
    } -> std::same_as<std::expected<std::reference_wrapper<typename G::cell_type>, grid_error>>;
    { gm.remove(std::declval<const typename G::pos_type&>()) } -> std::same_as<std::expected<void, grid_error>>;
    {
        gm.take(std::declval<const typename G::pos_type&>())
    } -> std::same_as<std::expected<typename G::cell_type, grid_error>>;
};

/**
 * @brief Concept for grids that also provide unsafe get/set operations without error handling.
 *
 * get_unsafe(pos) must return reference to cell value or throw on invalid pos.
 * get_mut_unsafe(pos) must return mutable reference to cell value or throw on invalid pos.
 * set_unsafe(pos, val) must set cell value and return reference to it, or throw on invalid pos.
 */
export template <typename G>
concept unsafe_grid = any_grid<G> && requires(const G& g, G& gm) {
    { g.get_unsafe(std::declval<const typename G::pos_type&>()) } -> std::same_as<const typename G::cell_type&>;
    { gm.get_mut_unsafe(std::declval<const typename G::pos_type&>()) } -> std::same_as<typename G::cell_type&>;
    {
        gm.set_unsafe(std::declval<const typename G::pos_type&>(), std::declval<typename G::cell_type>())
    } -> std::same_as<typename G::cell_type&>;
};

export template <typename G>
concept new_settable_grid = any_grid<G> && requires(G& g) {
    {
        g.set_new(std::declval<const typename G::pos_type&>(), std::declval<typename G::cell_type>())
    } -> std::same_as<std::expected<std::reference_wrapper<typename G::cell_type>, grid_error>>;
};
export template <typename G>
concept unsafe_new_settable_grid = any_grid<G> && requires(G& g) {
    {
        g.set_new_unsafe(std::declval<const typename G::pos_type&>(), std::declval<typename G::cell_type>())
    } -> std::same_as<typename G::cell_type&>;
};

/**
 * @brief Concept for grids that provide iteration over occupied cells.
 *
 * Requires:
 *  - iter_pos()     → input_range of pos_type
 *  - iter_cells()    → input_range of const cell references/values
 *  - iter_cells_mut() → input_range of mutable cell references/values
 *  - iter()          → input_range of (pos_type, const_cell_ref) pairs
 *  - iter_mut()      → input_range of (pos_type, mutable_cell_ref) pairs
 *
 * The value types of iter_cells/iter_cells_mut must be convertible from
 * cell_type (plain values or reference_wrappers are both accepted).
 * The second element of iter/iter_mut pairs must be convertible to a
 * cell_type reference.
 */
export template <typename G>
concept iterable_grid = any_grid<G> && requires(const G& g, G& gm) {
    { g.iter_pos() } -> std::ranges::input_range;
    { g.iter_cells() } -> std::ranges::input_range;
    { gm.iter_cells_mut() } -> std::ranges::input_range;
    { g.iter() } -> std::ranges::input_range;
    { gm.iter_mut() } -> std::ranges::input_range;
    // pos_type must be the value type of iter_pos()
    requires std::same_as<std::ranges::range_value_t<decltype(g.iter_pos())>, typename G::pos_type>;
    // iter_cells() values must be usable as const cell_type references
    requires std::convertible_to<std::ranges::range_reference_t<decltype(g.iter_cells())>,
                                 const typename G::cell_type&>;
    // iter_cells_mut() references must be usable as mutable cell_type references
    requires std::convertible_to<std::ranges::range_reference_t<decltype(gm.iter_cells_mut())>, typename G::cell_type&>;
    // iter() first element is pos_type
    requires std::same_as<std::remove_cvref_t<std::tuple_element_t<0, std::ranges::range_value_t<decltype(g.iter())>>>,
                          typename G::pos_type>;
    // iter() second element must be usable as const cell_type reference
    requires std::convertible_to<std::tuple_element_t<1, std::ranges::range_reference_t<decltype(g.iter())>>,
                                 const typename G::cell_type&>;
    // iter_mut() first element is pos_type
    requires std::same_as<
        std::remove_cvref_t<std::tuple_element_t<0, std::ranges::range_value_t<decltype(gm.iter_mut())>>>,
        typename G::pos_type>;
    // iter_mut() second element must be usable as mutable cell_type reference
    requires std::convertible_to<std::tuple_element_t<1, std::ranges::range_reference_t<decltype(gm.iter_mut())>>,
                                 typename G::cell_type&>;
};

/**
 * @brief A grid with unsigned (non-negative) coordinates.
 *        Covers: packed_grid, dense_grid, sparse_grid, tree_grid.
 */
export template <typename G>
concept maybe_fixed_grid = any_grid<G> && std::unsigned_integral<typename G::pos_type::value_type>;

/**
 * @brief A grid with signed (possibly negative) coordinates.
 *        Covers: dense_extendible_grid, tree_extendible_grid.
 */
export template <typename G>
concept maybe_extendible_grid = any_grid<G> && std::signed_integral<typename G::pos_type::value_type>;

/**
 * @brief A grid that supports negative coordinates and extends implicitly on insert.
 *        Covers: dense_extendible_grid, tree_extendible_grid.
 */
export template <typename G>
concept extendible_grid = maybe_extendible_grid<G>;

/**
 * @brief A grid backed by a tree structure that exposes coverage().
 *        Covers: tree_grid, tree_extendible_grid.
 */
export template <typename G>
concept tree_based_grid = any_grid<G> && requires(const G& g) {
    { g.coverage() } -> std::convertible_to<std::uint32_t>;
};

/**
 * @brief A basic grid concept combining gettable, settable, and containable.
 */
export template <typename G>
concept basic_grid = any_grid<G> && iterable_grid<G>;

/**
 * @brief An extendible grid concept combining basic_grid and extendible_grid.
 */
export template <typename G>
concept basic_extendible_grid = basic_grid<G> && extendible_grid<G>;

// ============================================================
// grid_trait — constrained on any_grid, derives facts from the interface
// ============================================================

/**
 * @brief Auto-detecting traits struct for any grid type.
 *
 * Constrained on any_grid<G>; no user specialization needed.
 * value_type unwraps reference_wrapper<T> if iter_cells() yields one.
 *
 * Members:
 *   - `using pos_type`              — std::array<coord_type, dim>
 *   - `using coord_type`               — element type of pos_array_t
 *   - `using value_type`               — T (reference_wrapper<T> is unwrapped)
 *   - `static constexpr dim`           — number of spatial dimensions
 *   - `static constexpr is_extendible` — true when coord_type is signed
 *   - `static constexpr has_coverage`  — true when the grid satisfies tree_based_grid
 *
 *   - functions from those concepts, constrained by concepts
 */
export template <any_grid G>
struct grid_trait {
    using pos_type   = typename G::pos_type;
    using coord_type = typename pos_type::value_type;
    using value_type = typename G::cell_type;

    static constexpr std::size_t dim    = std::tuple_size_v<pos_type>;
    static constexpr bool is_extendible = extendible_grid<G>;
    static constexpr bool has_coverage  = tree_based_grid<G>;

    auto contains(const G& g, const pos_type& pos) const -> bool { return g.contains(pos); }
    auto get(const G& g, const pos_type& pos) const
        -> std::expected<std::reference_wrapper<const value_type>, grid_error> {
        return g.get(pos);
    }
    auto get_mut(G& g, const pos_type& pos) const -> std::expected<std::reference_wrapper<value_type>, grid_error> {
        return g.get_mut(pos);
    }
    template <typename... Args>
        requires std::constructible_from<value_type, Args...>
    auto set(G& g, const pos_type& pos, Args&&... value) const
        -> std::expected<std::reference_wrapper<value_type>, grid_error> {
        return g.set(pos, std::forward<Args>(value)...);
    }
    template <typename... Args>
        requires std::constructible_from<value_type, Args...> && new_settable_grid<G>
    auto set_new(G& g, const pos_type& pos, Args&&... value)
        -> std::expected<std::reference_wrapper<value_type>, grid_error> {
        return g.set_new(pos, std::forward<Args>(value)...);
    }

    auto get_unsafe(const G& g, const pos_type& pos) const -> const value_type&
        requires unsafe_grid<G>
    {
        return g.get_unsafe(pos);
    }
    auto get_mut_unsafe(G& g, const pos_type& pos) const -> value_type&
        requires unsafe_grid<G>
    {
        return g.get_mut_unsafe(pos);
    }
    template <typename... Args>
        requires std::constructible_from<value_type, Args...> && unsafe_grid<G>
    auto set_unsafe(G& g, const pos_type& pos, Args&&... value) const -> value_type& {
        return g.set_unsafe(pos, std::forward<Args>(value)...);
    }
    template <typename... Args>
        requires std::constructible_from<value_type, Args...> && unsafe_new_settable_grid<G>
    auto set_new_unsafe(G& g, const pos_type& pos, Args&&... value) const -> value_type& {
        return g.set_new_unsafe(pos, std::forward<Args>(value)...);
    }

    auto iter_pos(const G& g) const
        requires iterable_grid<G>
    {
        return g.iter_pos();
    }
    auto iter_cells(const G& g) const
        requires iterable_grid<G>
    {
        return g.iter_cells();
    }
    auto iter_cells_mut(G& g) const
        requires iterable_grid<G>
    {
        return g.iter_cells_mut();
    }
    auto iter(const G& g) const
        requires iterable_grid<G>
    {
        return g.iter();
    }
    auto iter_mut(G& g) const
        requires iterable_grid<G>
    {
        return g.iter_mut();
    }

    auto coverage(const G& g) const
        requires tree_based_grid<G>
    {
        return g.coverage();
    }
};
}  // namespace epix::ext::grid