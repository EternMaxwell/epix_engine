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
// Grid concepts — view series (read-only) + mutable extensions
// ============================================================

/**
 * @brief Read-only structural concept: query dimensions, containment, and cell values.
 *
 * A `viewable_grid` exposes:
 *  - `pos_type`       — std::array<unsigned_or_signed_integral, N>
 *  - `cell_type`      — the stored element type
 *  - `dimensions()`  → array<unsigned_integral, N> matching pos_type arity
 *  - `contains(pos)` → bool
 *  - `get(pos)`      → expected<reference_wrapper<const cell_type>, grid_error>
 */
export template <typename G>
concept viewable_grid = requires(G g) {
    typename std::decay_t<G>::pos_type;
    typename std::decay_t<G>::cell_type;

    requires std::unsigned_integral<typename std::remove_cvref_t<decltype(g.dimensions())>::value_type>;
    requires std::tuple_size_v<std::remove_cvref_t<decltype(g.dimensions())>> ==
                 std::tuple_size_v<typename std::decay_t<G>::pos_type>;
    { g.contains(std::declval<const typename std::decay_t<G>::pos_type&>()) } -> std::same_as<bool>;
    {
        g.get(std::declval<const typename std::decay_t<G>::pos_type&>())
    } -> std::same_as<std::expected<std::reference_wrapper<const typename std::decay_t<G>::cell_type>, grid_error>>;
};

/**
 * @brief Mutable extension of `viewable_grid`: adds `get_mut(pos)`.
 *
 * Allows in-place modification of an existing cell without replacing it.
 *  - `get_mut(pos)` → expected<reference_wrapper<cell_type>, grid_error>
 */
export template <typename G>
concept mutable_viewable_grid = viewable_grid<G> && requires(G g) {
    {
        g.get_mut(std::declval<const typename std::decay_t<G>::pos_type&>())
    } -> std::same_as<std::expected<std::reference_wrapper<typename std::decay_t<G>::cell_type>, grid_error>>;
};

/**
 * @brief Full read/write container concept.
 *
 * Extends `mutable_viewable_grid` with mutation primitives:
 *  - `set(pos, value)`     → expected<reference_wrapper<cell_type>, grid_error>  (insert or overwrite)
 *  - `set_new(pos, value)` → expected<reference_wrapper<cell_type>, grid_error>  (insert; fails if occupied)
 *  - `remove(pos)`         → expected<void, grid_error>                          (erase cell)
 *  - `take(pos)`           → expected<cell_type, grid_error>                     (move-out and erase)
 */
export template <typename G>
concept grid_container = mutable_viewable_grid<G> && requires(G g) {
    {
        g.set(std::declval<const typename std::decay_t<G>::pos_type&>(),
              std::declval<typename std::decay_t<G>::cell_type>())
    } -> std::same_as<std::expected<std::reference_wrapper<typename std::decay_t<G>::cell_type>, grid_error>>;
    {
        g.set_new(std::declval<const typename std::decay_t<G>::pos_type&>(),
                  std::declval<typename std::decay_t<G>::cell_type>())
    } -> std::same_as<std::expected<std::reference_wrapper<typename std::decay_t<G>::cell_type>, grid_error>>;
    {
        g.remove(std::declval<const typename std::decay_t<G>::pos_type&>())
    } -> std::same_as<std::expected<void, grid_error>>;
    {
        g.take(std::declval<const typename std::decay_t<G>::pos_type&>())
    } -> std::same_as<std::expected<typename std::decay_t<G>::cell_type, grid_error>>;
};

/**
 * @brief Read-only bounds-unchecked access via `get_unsafe(pos)`.
 *
 * Returns `cell_type&` directly — no bounds check, no error path.
 * Typically satisfied by packed/dense grids with fixed dimensions.
 */
export template <typename G>
concept unsafe_viewable_grid = viewable_grid<G> && requires(G g) {
    {
        g.get_unsafe(std::declval<const typename std::decay_t<G>::pos_type&>())
    } -> std::same_as<const typename std::decay_t<G>::cell_type&>;
};

/**
 * @brief Mutable bounds-unchecked access via `get_mut_unsafe(pos)`.
 *
 * Returns `cell_type&` directly — no bounds check, no error path.
 */
export template <typename G>
concept unsafe_mutable_viewable_grid = viewable_grid<G> && requires(G g) {
    {
        g.get_mut_unsafe(std::declval<const typename std::decay_t<G>::pos_type&>())
    } -> std::same_as<typename std::decay_t<G>::cell_type&>;
};

/**
 * @brief Full unsafe container: unchecked set/remove/take.
 *
 * Extends `unsafe_mutable_viewable_grid` with:
 *  - `set_unsafe(pos, value)` → cell_type&
 *  - `remove_unsafe(pos)`     → void
 *  - `take_unsafe(pos)`       → cell_type
 */
export template <typename G>
concept unsafe_grid_container = unsafe_mutable_viewable_grid<G> && requires(G g) {
    {
        g.set_unsafe(std::declval<const typename std::decay_t<G>::pos_type&>(),
                     std::declval<typename std::decay_t<G>::cell_type>())
    } -> std::same_as<typename std::decay_t<G>::cell_type&>;
    { g.remove_unsafe(std::declval<const typename std::decay_t<G>::pos_type&>()) } -> std::same_as<void>;
    {
        g.take_unsafe(std::declval<const typename std::decay_t<G>::pos_type&>())
    } -> std::same_as<typename std::decay_t<G>::cell_type>;
};

/**
 * @brief Read-only iteration: produces positions, cell values, and (pos, cell) pairs.
 *
 *  - `iter_pos()`   → input_range of pos_type
 *  - `iter_cells()` → input_range whose references are convertible to `const cell_type&`
 *  - `iter()`       → input_range of (pos_type, const cell_type&) tuples
 */
export template <typename G>
concept iterable_grid = viewable_grid<G> && requires(G g) {
    { g.iter_pos() } -> std::ranges::input_range;
    { g.iter_cells() } -> std::ranges::input_range;
    { g.iter() } -> std::ranges::input_range;
    requires std::same_as<std::ranges::range_value_t<decltype(g.iter_pos())>, typename std::decay_t<G>::pos_type>;
    requires std::convertible_to<std::ranges::range_reference_t<decltype(g.iter_cells())>,
                                 const typename std::decay_t<G>::cell_type&>;
    requires std::same_as<std::remove_cvref_t<std::tuple_element_t<0, std::ranges::range_value_t<decltype(g.iter())>>>,
                          typename std::decay_t<G>::pos_type>;
    requires std::convertible_to<std::tuple_element_t<1, std::ranges::range_reference_t<decltype(g.iter())>>,
                                 const typename std::decay_t<G>::cell_type&>;
};

/**
 * @brief Mutable iteration: extends `iterable_grid` with mutable ranges.
 *
 *  - `iter_cells_mut()` → input_range whose references are convertible to `cell_type&`
 *  - `iter_mut()`       → input_range of (pos_type, cell_type&) tuples
 */
export template <typename G>
concept mutable_iterable_grid = iterable_grid<G> && mutable_viewable_grid<G> && requires(G g) {
    { g.iter_cells_mut() } -> std::ranges::input_range;
    { g.iter_mut() } -> std::ranges::input_range;
    requires std::convertible_to<std::ranges::range_reference_t<decltype(g.iter_cells_mut())>,
                                 typename std::decay_t<G>::cell_type&>;
    requires std::same_as<
        std::remove_cvref_t<std::tuple_element_t<0, std::ranges::range_value_t<decltype(g.iter_mut())>>>,
        typename std::decay_t<G>::pos_type>;
    requires std::convertible_to<std::tuple_element_t<1, std::ranges::range_reference_t<decltype(g.iter_mut())>>,
                                 typename std::decay_t<G>::cell_type&>;
};

/**
 * @brief A grid with unsigned (non-negative) coordinates — i.e. a fixed-bounds grid.
 *
 * Covered by: `packed_grid`, `dense_grid`, `sparse_grid`, `tree_grid`.
 */
export template <typename G>
concept maybe_fixed_grid = viewable_grid<G> && std::unsigned_integral<typename std::decay_t<G>::pos_type::value_type>;

/**
 * @brief A grid with signed coordinates — can hold negative positions.
 *
 * Covered by: `dense_extendible_grid`, `tree_extendible_grid`.
 */
export template <typename G>
concept maybe_extendible_grid =
    viewable_grid<G> && std::signed_integral<typename std::decay_t<G>::pos_type::value_type>;

/**
 * @brief A grid that supports negative coordinates and extends its bounds implicitly on insert.
 *
 * Alias for `maybe_extendible_grid` — extension is structural (signed coords imply implicit growth).
 * Covered by: `dense_extendible_grid`, `tree_extendible_grid`.
 */
export template <typename G>
concept extendible_grid = maybe_extendible_grid<G>;

/**
 * @brief A grid backed by a tree structure that exposes `coverage()`.
 *
 * `coverage()` returns the number of occupied cells as a `uint32_t`.
 * Covered by: `tree_grid`, `tree_extendible_grid`.
 */
export template <typename G>
concept tree_based_grid = viewable_grid<G> && requires(G g) {
    { g.coverage() } -> std::convertible_to<std::uint32_t>;
};

/**
 * @brief Composite concept: full read/write grid with iteration.
 *
 * Satisfied by any type that is simultaneously:
 *   `viewable_grid` + `mutable_viewable_grid` + `grid_container` + `iterable_grid` + `mutable_iterable_grid`
 */
export template <typename G>
concept basic_grid =
    viewable_grid<G> && mutable_viewable_grid<G> && grid_container<G> && iterable_grid<G> && mutable_iterable_grid<G>;

/**
 * @brief `basic_grid` over a signed-coordinate (extendible) domain.
 */
export template <typename G>
concept basic_extendible_grid = basic_grid<G> && extendible_grid<G>;

// ============================================================
// grid_trait — constrained on viewable_grid, derives facts from the interface
// ============================================================

/**
 * @brief Compile-time traits for any grid type, constrained on `viewable_grid<G>`.
 *
 * No user specialization is needed — all fields are derived from the grid interface.
 *
 * Type members:
 *   - `pos_type`   — `std::array<coord_type, dim>` (the grid's position type)
 *   - `coord_type` — element type of `pos_type` (signed ⇒ extendible, unsigned ⇒ fixed)
 *   - `value_type` — `cell_type` of the grid
 *
 * Constants:
 *   - `dim`            — number of spatial dimensions
 *   - `is_extendible`  — true when `coord_type` is signed (`extendible_grid<G>`)
 *   - `has_coverage`   — true when the grid satisfies `tree_based_grid<G>`
 *
 * Methods (conditionally enabled by the corresponding concept):
 *   - `contains` / `get`                           — always (viewable_grid)
 *   - `get_mut`                                    — mutable_viewable_grid
 *   - `set` / `set_new` / `remove` / `take`        — grid_container
 *   - `get_unsafe` / `get_mut_unsafe`              — unsafe_viewable/mutable_viewable_grid
 *   - `set_unsafe` / `remove_unsafe` / `take_unsafe`                  — unsafe_grid_container
 *   - `iter_pos` / `iter_cells` / `iter`           — iterable_grid
 *   - `iter_cells_mut` / `iter_mut`                — mutable_iterable_grid
 *   - `coverage`                                   — tree_based_grid
 */
export template <viewable_grid G>
struct grid_trait {
    using pos_type   = typename std::decay_t<G>::pos_type;
    using coord_type = typename pos_type::value_type;
    using value_type = typename std::decay_t<G>::cell_type;

    static constexpr std::size_t dim    = std::tuple_size_v<pos_type>;
    static constexpr bool is_extendible = extendible_grid<G>;
    static constexpr bool has_coverage  = tree_based_grid<G>;

    auto contains(const G& g, const pos_type& pos) const -> bool { return g.contains(pos); }
    auto get(const G& g, const pos_type& pos) const
        -> std::expected<std::reference_wrapper<const value_type>, grid_error> {
        return g.get(pos);
    }
    auto get_mut(G& g, const pos_type& pos) const -> std::expected<std::reference_wrapper<value_type>, grid_error>
        requires mutable_viewable_grid<G>
    {
        return g.get_mut(pos);
    }
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

    auto get_unsafe(const G& g, const pos_type& pos) const -> const value_type&
        requires unsafe_viewable_grid<G>
    {
        return g.get_unsafe(pos);
    }
    auto get_mut_unsafe(G& g, const pos_type& pos) const -> value_type&
        requires unsafe_mutable_viewable_grid<G>
    {
        return g.get_mut_unsafe(pos);
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
        requires mutable_iterable_grid<G>
    {
        return g.iter_cells_mut();
    }
    auto iter(const G& g) const
        requires iterable_grid<G>
    {
        return g.iter();
    }
    auto iter_mut(G& g) const
        requires mutable_iterable_grid<G>
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