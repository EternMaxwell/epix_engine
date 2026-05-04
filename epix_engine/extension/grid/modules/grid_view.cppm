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
 * Stores G directly — if G is an lvalue-reference type (e.g. `dense_grid<2,int>&`),
 * the view holds a reference; if G is a value type, the view owns a copy.
 * The factory function @ref filter deduces G as a reference for lvalues.
 *
 * Methods are added conditionally based on what concepts G satisfies:
 *  - always:                          `dimensions()`, `contains()`, `get()`
 *  - `mutable_viewable_grid<G>`:      `get_mut()`
 *  - `grid_container<G>`:             `set()`, `set_new()`, `remove()`, `take()`
 *  - `unsafe_viewable_grid<G>`:       `get_unsafe()`
 *  - `unsafe_mutable_viewable_grid<G>`: `get_mut_unsafe()`
 *  - `unsafe_grid_container<G>`:      `set_unsafe()`, `remove_unsafe()`, `take_unsafe()`
 *  - `iterable_grid<G>`:              `iter_pos()`, `iter_cells()`, `iter()`
 *  - `mutable_iterable_grid<G>`:      `iter_cells_mut()`, `iter_mut()`
 */
export template <viewable_grid G, std::invocable<const typename std::decay_t<G>::cell_type&> Pred>
struct filter_view {
    using pos_type  = typename std::decay_t<G>::pos_type;
    using cell_type = typename std::decay_t<G>::cell_type;

    G grid;
    Pred pred;

    // ─── viewable_grid interface (always present) ─────────────────────────

    pos_type dimensions() const { return grid.dimensions(); }

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

    // ─── mutable_viewable_grid interface ─────────────────────────────────

    auto get_mut(const pos_type& pos) -> std::expected<std::reference_wrapper<cell_type>, grid_error>
        requires mutable_viewable_grid<G>
    {
        return grid.get_mut(pos).and_then(
            [&](cell_type& cell) -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
                if (!pred(cell)) return std::unexpected(grid_error::EmptyCell);
                return std::ref(cell);
            });
    }

    // ─── grid_container interface ─────────────────────────────────────────

    auto set(const pos_type& pos, cell_type val) -> std::expected<std::reference_wrapper<cell_type>, grid_error>
        requires grid_container<G>
    {
        return grid.set(pos, std::move(val));
    }

    template <typename... Args>
        requires grid_container<G> && std::constructible_from<cell_type, Args...>
    auto set_new(const pos_type& pos, Args&&... args) -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
        return grid.set_new(pos, std::forward<Args>(args)...);
    }

    auto remove(const pos_type& pos) -> std::expected<void, grid_error>
        requires grid_container<G>
    {
        return grid.remove(pos);
    }

    auto take(const pos_type& pos) -> std::expected<cell_type, grid_error>
        requires grid_container<G>
    {
        if (!contains(pos)) return std::unexpected(grid_error::EmptyCell);
        return grid.take(pos);
    }

    // ─── unsafe_viewable_grid interface ──────────────────────────────────

    const cell_type& get_unsafe(const pos_type& pos) const
        requires unsafe_viewable_grid<G>
    {
        return grid.get_unsafe(pos);
    }

    // ─── unsafe_mutable_viewable_grid interface ───────────────────────────

    cell_type& get_mut_unsafe(const pos_type& pos)
        requires unsafe_mutable_viewable_grid<G>
    {
        return grid.get_mut_unsafe(pos);
    }

    // ─── unsafe_grid_container interface ─────────────────────────────────

    cell_type& set_unsafe(const pos_type& pos, cell_type val)
        requires unsafe_grid_container<G>
    {
        return grid.set_unsafe(pos, std::move(val));
    }

    void remove_unsafe(const pos_type& pos)
        requires unsafe_grid_container<G>
    {
        grid.remove_unsafe(pos);
    }

    cell_type take_unsafe(const pos_type& pos)
        requires unsafe_grid_container<G>
    {
        return grid.take_unsafe(pos);
    }

    // ─── iterable_grid interface ──────────────────────────────────────────

    auto iter_pos() const
        requires iterable_grid<G>
    {
        return grid.iter() |
               std::views::filter([this](const auto& kv) { return static_cast<bool>(pred(std::get<1>(kv))); }) |
               std::views::elements<0>;
    }

    auto iter_cells() const
        requires iterable_grid<G>
    {
        return grid.iter_cells() |
               std::views::filter([this](const cell_type& cell) { return static_cast<bool>(pred(cell)); });
    }

    auto iter() const
        requires iterable_grid<G>
    {
        return grid.iter() |
               std::views::filter([this](const auto& kv) { return static_cast<bool>(pred(std::get<1>(kv))); });
    }

    // ─── mutable_iterable_grid interface ─────────────────────────────────

    auto iter_cells_mut()
        requires mutable_iterable_grid<G>
    {
        return grid.iter_cells_mut() |
               std::views::filter([this](const cell_type& cell) { return static_cast<bool>(pred(cell)); });
    }

    auto iter_mut()
        requires mutable_iterable_grid<G>
    {
        return grid.iter_mut() |
               std::views::filter([this](const auto& kv) { return static_cast<bool>(pred(std::get<1>(kv))); });
    }
};

/**
 * @brief Construct a @ref filter_view wrapping @p g with @p pred.
 *
 * G is deduced as `decltype(g)` — a reference type for named lvalue variables,
 * or a value type when an rvalue is passed.
 */
export template <viewable_grid G, std::invocable<const typename std::decay_t<G>::cell_type&> Pred>
filter_view<G, std::decay_t<Pred>> filter(G&& g, Pred&& pred) {
    return {std::forward<G>(g), std::forward<Pred>(pred)};
}

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief A lazy view that wraps a grid and a position predicate: a cell is
 *        considered "occupied" only when the predicate returns true for its
 *        position (regardless of the stored value).
 *
 * Stores G directly — if G is an lvalue-reference type (e.g. `dense_grid<2,int>&`),
 * the view holds a reference; if G is a value type, the view owns a copy.
 * The factory function @ref shadow deduces G as a reference for lvalues.
 *
 * Methods are added conditionally based on what concepts G satisfies:
 *  - always:                          `dimensions()`, `contains()`, `get()`
 *  - `mutable_viewable_grid<G>`:      `get_mut()`
 *  - `grid_container<G>`:             `set()`, `set_new()`, `remove()`, `take()`
 *  - `unsafe_viewable_grid<G>`:       `get_unsafe()`
 *  - `unsafe_mutable_viewable_grid<G>`: `get_mut_unsafe()`
 *  - `unsafe_grid_container<G>`:      `set_unsafe()`, `remove_unsafe()`, `take_unsafe()`
 *  - `iterable_grid<G>`:              `iter_pos()`, `iter_cells()`, `iter()`
 *  - `mutable_iterable_grid<G>`:      `iter_cells_mut()`, `iter_mut()`
 */
export template <viewable_grid G, std::invocable<const typename std::decay_t<G>::pos_type&> Pred>
struct shadow_view {
    using pos_type  = typename std::decay_t<G>::pos_type;
    using cell_type = typename std::decay_t<G>::cell_type;

    G grid;
    Pred pred;

    // ─── viewable_grid interface (always present) ─────────────────────────

    pos_type dimensions() const { return grid.dimensions(); }

    bool contains(const pos_type& pos) const { return static_cast<bool>(pred(pos)) && grid.contains(pos); }

    auto get(const pos_type& pos) const -> std::expected<std::reference_wrapper<const cell_type>, grid_error> {
        if (!pred(pos)) return std::unexpected(grid_error::EmptyCell);
        return grid.get(pos);
    }

    // ─── mutable_viewable_grid interface ─────────────────────────────────

    auto get_mut(const pos_type& pos) -> std::expected<std::reference_wrapper<cell_type>, grid_error>
        requires mutable_viewable_grid<G>
    {
        if (!pred(pos)) return std::unexpected(grid_error::EmptyCell);
        return grid.get_mut(pos);
    }

    // ─── grid_container interface ─────────────────────────────────────────

    auto set(const pos_type& pos, cell_type val) -> std::expected<std::reference_wrapper<cell_type>, grid_error>
        requires grid_container<G>
    {
        return grid.set(pos, std::move(val));
    }

    template <typename... Args>
        requires grid_container<G> && std::constructible_from<cell_type, Args...>
    auto set_new(const pos_type& pos, Args&&... args) -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
        return grid.set_new(pos, std::forward<Args>(args)...);
    }

    auto remove(const pos_type& pos) -> std::expected<void, grid_error>
        requires grid_container<G>
    {
        return grid.remove(pos);
    }

    auto take(const pos_type& pos) -> std::expected<cell_type, grid_error>
        requires grid_container<G>
    {
        if (!pred(pos)) return std::unexpected(grid_error::EmptyCell);
        return grid.take(pos);
    }

    // ─── unsafe_viewable_grid interface ──────────────────────────────────

    const cell_type& get_unsafe(const pos_type& pos) const
        requires unsafe_viewable_grid<G>
    {
        return grid.get_unsafe(pos);
    }

    // ─── unsafe_mutable_viewable_grid interface ───────────────────────────

    cell_type& get_mut_unsafe(const pos_type& pos)
        requires unsafe_mutable_viewable_grid<G>
    {
        return grid.get_mut_unsafe(pos);
    }

    // ─── unsafe_grid_container interface ─────────────────────────────────

    cell_type& set_unsafe(const pos_type& pos, cell_type val)
        requires unsafe_grid_container<G>
    {
        return grid.set_unsafe(pos, std::move(val));
    }

    void remove_unsafe(const pos_type& pos)
        requires unsafe_grid_container<G>
    {
        grid.remove_unsafe(pos);
    }

    cell_type take_unsafe(const pos_type& pos)
        requires unsafe_grid_container<G>
    {
        return grid.take_unsafe(pos);
    }

    // ─── iterable_grid interface ──────────────────────────────────────────

    auto iter_pos() const
        requires iterable_grid<G>
    {
        return grid.iter_pos() |
               std::views::filter([this](const pos_type& pos) { return static_cast<bool>(pred(pos)); });
    }

    auto iter_cells() const
        requires iterable_grid<G>
    {
        return iter() | std::views::transform([](const auto& kv) -> const cell_type& { return std::get<1>(kv); });
    }

    auto iter() const
        requires iterable_grid<G>
    {
        return grid.iter() |
               std::views::filter([this](const auto& kv) { return static_cast<bool>(pred(std::get<0>(kv))); });
    }

    // ─── mutable_iterable_grid interface ─────────────────────────────────

    auto iter_cells_mut()
        requires mutable_iterable_grid<G>
    {
        return iter_mut() | std::views::transform([](auto& kv) -> cell_type& { return std::get<1>(kv); });
    }

    auto iter_mut()
        requires mutable_iterable_grid<G>
    {
        return grid.iter_mut() |
               std::views::filter([this](const auto& kv) { return static_cast<bool>(pred(std::get<0>(kv))); });
    }
};

/**
 * @brief Construct a @ref shadow_view wrapping @p g with @p pred.
 *
 * G is deduced as `decltype(g)` — a reference type for named lvalue variables,
 * or a value type when an rvalue is passed.
 */
export template <viewable_grid G, std::invocable<const typename std::decay_t<G>::pos_type&> Pred>
shadow_view<G, std::decay_t<Pred>> shadow(G&& g, Pred&& pred) {
    return {std::forward<G>(g), std::forward<Pred>(pred)};
}

}  // namespace epix::ext::grid
