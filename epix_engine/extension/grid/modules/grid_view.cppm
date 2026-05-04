module;
#ifndef EPIX_IMPORT_STD
#include <concepts>
#include <expected>
#include <functional>
#include <ranges>
#include <utility>
#endif

export module epix.extension.grid:grid_view;
#ifdef EPIX_IMPORT_STD
import std;
#endif

import :concepts;

namespace epix::ext::grid {

/**
 * @brief A lazy view that wraps a grid and a predicate: a cell is considered
 *        "occupied" only when the predicate returns true for its value.
 *
 * The predicate receives `const cell_type&` and must return something
 * convertible to bool.
 *
 * `filter_view` satisfies `any_grid` (when G satisfies `any_grid`).
 * It also conditionally satisfies `unsafe_grid` and `iterable_grid`
 * when the underlying grid G satisfies those concepts.
 *
 * The view stores a non-const reference to the underlying grid; the grid
 * must outlive the view.  Use @ref filter to construct one.
 */
export template <any_grid G, typename Pred>
    requires std::invocable<Pred, const typename G::cell_type&>
struct filter_view {
    using pos_type  = typename G::pos_type;
    using cell_type = typename G::cell_type;

    G& grid;
    Pred pred;

    /** @brief Forward dimensions() from the underlying grid. */
    pos_type dimensions() const { return grid.dimensions(); }

    // ─── any_grid interface ────────────────────────────────────────────────

    /** @brief A cell is occupied only when grid.contains(pos) && pred(value). */
    bool contains(const pos_type& pos) const {
        return grid.get(pos)
            .transform([&](const cell_type& cell) { return static_cast<bool>(pred(cell)); })
            .value_or(false);
    }

    auto get(const pos_type& pos) const -> std::expected<std::reference_wrapper<const cell_type>, grid_error> {
        return grid.get(pos).and_then(
            [&](const cell_type& cell) -> std::expected<std::reference_wrapper<const cell_type>, grid_error> {
                if (!pred(cell)) return std::unexpected(grid_error::EmptyCell);
                return std::cref(cell);
            });
    }

    auto get_mut(const pos_type& pos) -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
        return grid.get_mut(pos).and_then(
            [&](cell_type& cell) -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
                if (!pred(cell)) return std::unexpected(grid_error::EmptyCell);
                return std::ref(cell);
            });
    }

    /** @brief Unconditionally delegates to the underlying grid (no pred check). */
    auto set(const pos_type& pos, cell_type val) -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
        return grid.set(pos, std::move(val));
    }

    /** @brief Unconditionally delegates to the underlying grid. */
    auto remove(const pos_type& pos) -> std::expected<void, grid_error> { return grid.remove(pos); }

    auto take(const pos_type& pos) -> std::expected<cell_type, grid_error> {
        if (!contains(pos)) return std::unexpected(grid_error::EmptyCell);
        return grid.take(pos);
    }

    // ─── unsafe_grid interface (conditional) ──────────────────────────────

    const cell_type& get_unsafe(const pos_type& pos) const
        requires unsafe_grid<G>
    {
        return grid.get_unsafe(pos);
    }

    cell_type& get_mut_unsafe(const pos_type& pos)
        requires unsafe_grid<G>
    {
        return grid.get_mut_unsafe(pos);
    }

    cell_type& set_unsafe(const pos_type& pos, cell_type val)
        requires unsafe_grid<G>
    {
        return grid.set_unsafe(pos, std::move(val));
    }

    // ─── iterable_grid interface (conditional) ────────────────────────────

    /** @brief Iterate over positions of cells visible through the filter. */
    auto iter_pos() const
        requires iterable_grid<G>
    {
        return grid.iter() |
               std::views::filter([this](const auto& kv) { return static_cast<bool>(pred(std::get<1>(kv))); }) |
               std::views::elements<0>;
    }

    /** @brief Iterate over const-refs of cells visible through the filter. */
    auto iter_cells() const
        requires iterable_grid<G>
    {
        return grid.iter_cells() |
               std::views::filter([this](const cell_type& cell) { return static_cast<bool>(pred(cell)); });
    }

    /** @brief Iterate over mutable refs of cells visible through the filter. */
    auto iter_cells_mut()
        requires iterable_grid<G>
    {
        return grid.iter_cells_mut() |
               std::views::filter([this](const cell_type& cell) { return static_cast<bool>(pred(cell)); });
    }

    /** @brief Iterate over (pos, const_ref) pairs visible through the filter. */
    auto iter() const
        requires iterable_grid<G>
    {
        return grid.iter() |
               std::views::filter([this](const auto& kv) { return static_cast<bool>(pred(std::get<1>(kv))); });
    }

    /** @brief Iterate over (pos, mutable_ref) pairs visible through the filter. */
    auto iter_mut()
        requires iterable_grid<G>
    {
        return grid.iter_mut() |
               std::views::filter([this](const auto& kv) { return static_cast<bool>(pred(std::get<1>(kv))); });
    }
};

/**
 * @brief Construct a @ref filter_view wrapping @p g with @p pred.
 *
 * @p pred must be callable as `bool(const cell_type&)`.
 * The view stores a reference to @p g which must outlive the view.
 */
export template <any_grid G, typename Pred>
    requires std::invocable<Pred, const typename G::cell_type&>
filter_view<G, std::decay_t<Pred>> filter(G& g, Pred&& pred) {
    return {g, std::forward<Pred>(pred)};
}

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief A lazy view that wraps a grid and a position predicate: a cell is
 *        considered "occupied" only when the predicate returns true for its
 *        position (regardless of the stored value).
 *
 * The predicate receives `const pos_type&` and must return something
 * convertible to bool.
 *
 * `shadow_view` satisfies `any_grid` unconditionally and conditionally
 * satisfies `unsafe_grid` / `iterable_grid` when the underlying grid G does.
 *
 * The view stores a non-const reference to the underlying grid; the grid must
 * outlive the view.  Use @ref shadow to construct one.
 */
export template <any_grid G, typename Pred>
    requires std::invocable<Pred, const typename G::pos_type&>
struct shadow_view {
    using pos_type  = typename G::pos_type;
    using cell_type = typename G::cell_type;

    G& grid;
    Pred pred;

    /** @brief Forward dimensions() from the underlying grid. */
    pos_type dimensions() const { return grid.dimensions(); }

    // ─── any_grid interface ────────────────────────────────────────────────

    /** @brief A position is occupied only when grid.contains(pos) && pred(pos). */
    bool contains(const pos_type& pos) const { return static_cast<bool>(pred(pos)) && grid.contains(pos); }

    auto get(const pos_type& pos) const -> std::expected<std::reference_wrapper<const cell_type>, grid_error> {
        if (!pred(pos)) return std::unexpected(grid_error::EmptyCell);
        return grid.get(pos);
    }

    auto get_mut(const pos_type& pos) -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
        if (!pred(pos)) return std::unexpected(grid_error::EmptyCell);
        return grid.get_mut(pos);
    }

    /** @brief Unconditionally delegates to the underlying grid (no pred check). */
    auto set(const pos_type& pos, cell_type val) -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
        return grid.set(pos, std::move(val));
    }

    /** @brief Unconditionally delegates to the underlying grid. */
    auto remove(const pos_type& pos) -> std::expected<void, grid_error> { return grid.remove(pos); }

    auto take(const pos_type& pos) -> std::expected<cell_type, grid_error> {
        if (!pred(pos)) return std::unexpected(grid_error::EmptyCell);
        return grid.take(pos);
    }

    // ─── unsafe_grid interface (conditional) ──────────────────────────────

    const cell_type& get_unsafe(const pos_type& pos) const
        requires unsafe_grid<G>
    {
        return grid.get_unsafe(pos);
    }

    cell_type& get_mut_unsafe(const pos_type& pos)
        requires unsafe_grid<G>
    {
        return grid.get_mut_unsafe(pos);
    }

    cell_type& set_unsafe(const pos_type& pos, cell_type val)
        requires unsafe_grid<G>
    {
        return grid.set_unsafe(pos, std::move(val));
    }

    // ─── iterable_grid interface (conditional) ────────────────────────────

    /** @brief Iterate over positions passing the predicate. */
    auto iter_pos() const
        requires iterable_grid<G>
    {
        return grid.iter_pos() |
               std::views::filter([this](const pos_type& pos) { return static_cast<bool>(pred(pos)); });
    }

    /** @brief Iterate over const-refs of cells at positions passing the predicate. */
    auto iter_cells() const
        requires iterable_grid<G>
    {
        return iter() | std::views::transform([](const auto& kv) -> const cell_type& { return std::get<1>(kv); });
    }

    /** @brief Iterate over mutable refs of cells at positions passing the predicate. */
    auto iter_cells_mut()
        requires iterable_grid<G>
    {
        return iter_mut() | std::views::transform([](auto& kv) -> cell_type& { return std::get<1>(kv); });
    }

    /** @brief Iterate over (pos, const_ref) pairs at positions passing the predicate. */
    auto iter() const
        requires iterable_grid<G>
    {
        return grid.iter() |
               std::views::filter([this](const auto& kv) { return static_cast<bool>(pred(std::get<0>(kv))); });
    }

    /** @brief Iterate over (pos, mutable_ref) pairs at positions passing the predicate. */
    auto iter_mut()
        requires iterable_grid<G>
    {
        return grid.iter_mut() |
               std::views::filter([this](const auto& kv) { return static_cast<bool>(pred(std::get<0>(kv))); });
    }
};

/**
 * @brief Construct a @ref shadow_view wrapping @p g with @p pred.
 *
 * @p pred must be callable as `bool(const pos_type&)`.
 * The view stores a reference to @p g which must outlive the view.
 */
export template <any_grid G, typename Pred>
    requires std::invocable<Pred, const typename G::pos_type&>
shadow_view<G, std::decay_t<Pred>> shadow(G& g, Pred&& pred) {
    return {g, std::forward<Pred>(pred)};
}

}  // namespace epix::ext::grid
