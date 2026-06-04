#pragma once

#include <concepts>
#include <expected>
#include <functional>
#include <limits>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>


#include <epix/extension/grid/concepts.hpp>

namespace epix::ext::grid::views {

/**
 * @brief A lazy view that wraps a grid and a predicate: a cell is considered
 *        "occupied" only when the predicate returns true for its value.
 *
 * Stores G directly — if G is an lvalue-reference type (e.g. `dense_grid<2,int>&`),
 * the view holds a reference; if G is a value type, the view owns a copy.
 * The factory function @ref filter deduces G as a reference for lvalues.
 *
 * Methods are added conditionally based on what G supports:
 *  - always:              `dimensions()`, `contains()`, `get()` (const)
 *  - non-const G:         `get()` (mutable), `iter_cells()`, `iter()`
 *  - unsafe G:            `get_unsafe()` (const / non-const)
 *
 * Structural mutation (`set`, `set_new`, `remove`, `take` and their unsafe
 * variants) is intentionally omitted — views should not change topology.
 */
template <viewable_grid G, std::invocable<const typename std::decay_t<G>::cell_type&> Pred>
struct filter_view {
    using pos_type  = grid_trait<G>::pos_type;
    using cell_type = grid_trait<G>::cell_type;

    G grid;
    Pred pred;

    // ─── viewable_grid interface (always present) ─────────────────────────

    auto dimensions() const noexcept(noexcept(grid.dimensions())) { return grid.dimensions(); }

    bool contains(const pos_type& pos) const
        noexcept(noexcept(grid.get(pos)) && noexcept(pred(std::declval<const cell_type&>()))) {
        return grid.get(pos)
            .transform([&](const cell_type& cell) { return static_cast<bool>(pred(cell)); })
            .value_or(false);
    }

    auto get(const pos_type& pos) noexcept(noexcept(grid.get(pos)) && noexcept(pred(std::declval<const cell_type&>())))
        -> decltype(grid.get(pos)) {
        auto res = grid.get(pos);
        if (res.has_value() && !pred(cell_ref(res))) return std::unexpected(grid_error::EmptyCell);
        return res;
    }

    // ─── unsafe_viewable_grid interface ──────────────────────────────────

    decltype(auto) get_unsafe(const pos_type& pos) noexcept(noexcept(grid.get_unsafe(pos)))
        requires unsafe_viewable_grid<G>
    {
        return grid.get_unsafe(pos);
    }

    // ─── iterable_grid interface ──────────────────────────────────────────

    auto iter_pos()
        requires iterable_grid<G>
    {
        return std::ranges::elements_view<decltype(iter()), 0>(iter());
    }

    auto iter_cells()
        requires iterable_grid<G>
    {
        return std::ranges::filter_view(grid.iter_cells(),
                                        [this](const cell_type& cell) { return static_cast<bool>(pred(cell)); });
    }

    auto iter()
        requires iterable_grid<G>
    {
        return std::ranges::filter_view(grid.iter(),
                                        [this](const auto& kv) { return static_cast<bool>(pred(std::get<1>(kv))); });
    }

   private:
    static const cell_type& cell_ref(const auto& res) {
        if constexpr (requires { (*res).get(); })
            return (*res).get();
        else
            return *res;
    }
};

/**
 * @brief Construct a @ref filter_view wrapping @p g with @p pred.
 *
 * G is deduced as `decltype(g)` — a reference type for named lvalue variables,
 * or a value type when an rvalue is passed.
 */
template <viewable_grid G, std::invocable<const typename std::decay_t<G>::cell_type&> Pred>
filter_view<G, std::decay_t<Pred>> filter(G&& g, Pred&& pred) noexcept(
    std::is_nothrow_constructible_v<filter_view<G, std::decay_t<Pred>>, G, Pred>) {
    return {std::forward<G>(g), std::forward<Pred>(pred)};
}

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief A lazy view that projects each cell value through a callable, similar
 *        to `std::ranges::transform_view` for grid cells.
 *
 * The view preserves the wrapped grid's position domain. Iteration applies the
 * callable lazily to cell values. `get()` returns a normal grid reference when
 * the callable returns a stable lvalue reference; otherwise it returns the
 * projected value by value.
 */
template <viewable_grid G, typename Func>
    requires std::invocable<Func&, typename grid_trait<G>::get_type>
struct transform_view {
    using pos_type         = grid_trait<G>::pos_type;
    using source_cell_type = grid_trait<G>::cell_type;
    using cell_type        = std::remove_cvref_t<std::invoke_result_t<Func&, typename grid_trait<G>::get_type>>;
    using return_type      = std::conditional_t<
        std::is_lvalue_reference_v<std::invoke_result_t<Func&, typename grid_trait<G>::get_type>>,
        std::reference_wrapper<std::remove_reference_t<std::invoke_result_t<Func&, typename grid_trait<G>::get_type>>>,
        cell_type>;

    G grid;
    Func func;

    auto dimensions() const noexcept(noexcept(grid.dimensions())) { return grid.dimensions(); }

    bool contains(const pos_type& pos) const noexcept(noexcept(grid.contains(pos))) { return grid.contains(pos); }

    auto get(const pos_type& pos) noexcept(
        noexcept(grid.get(pos)) && noexcept(std::invoke(func, std::declval<typename grid_trait<G>::get_type>())))
        -> std::expected<return_type, grid_error> {
        return grid.get(pos).transform(
            [this](typename grid_trait<G>::get_type cell) -> return_type { return std::invoke(func, cell); });
    }

    decltype(auto) get_unsafe(const pos_type& pos) noexcept(noexcept(std::invoke(func, grid.get_unsafe(pos))))
        requires unsafe_viewable_grid<G>
    {
        return std::invoke(func, grid.get_unsafe(pos));
    }

    auto iter_pos()
        requires iterable_grid<G>
    {
        return grid.iter_pos();
    }

    auto iter_cells()
        requires iterable_grid<G>
    {
        return std::ranges::transform_view(
            grid.iter_cells(), [this](source_cell_type& cell) -> decltype(auto) { return std::invoke(func, cell); });
    }

    auto iter()
        requires iterable_grid<G>
    {
        return std::ranges::transform_view(grid.iter(), [this](auto&& kv) {
            return std::tuple<pos_type, std::invoke_result_t<Func&, source_cell_type&>>(
                std::get<0>(kv), std::invoke(func, std::get<1>(kv)));
        });
    }
};  // namespace epix::ext::grid::views

/**
 * @brief Construct a @ref transform_view wrapping @p g with @p func.
 */
template <viewable_grid G, typename Func>
    requires std::invocable<const std::decay_t<Func>&, const typename std::decay_t<G>::cell_type&>
transform_view<G, std::decay_t<Func>> transform(G&& g, Func&& func) noexcept(
    std::is_nothrow_constructible_v<transform_view<G, std::decay_t<Func>>, G, Func>) {
    return {std::forward<G>(g), std::forward<Func>(func)};
}

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief A lazy coordinate-window view over a grid.
 *
 * The view exposes `new_dimensions()` cells starting at `new_origin` in the
 * wrapped grid. View position `{0, 0, ...}` maps to `new_origin`; iteration
 * yields positions relative to that origin.
 */
template <viewable_grid G>
struct offset_view {
    using trait           = grid_trait<G>;
    using pos_type        = trait::pos_type;
    using coord_type      = typename pos_type::value_type;
    using dimensions_type = detail::grid_dimensions_type<G>;
    using cell_type       = trait::cell_type;

    G grid;
    pos_type origin;
    dimensions_type new_dimensions;

   private:
    bool in_view_bounds(const pos_type& pos) const noexcept {
        for (std::size_t i = 0; i < std::tuple_size_v<pos_type>; ++i) {
            if constexpr (std::signed_integral<coord_type>) {
                if (pos[i] < 0) return false;
                if (static_cast<std::make_unsigned_t<coord_type>>(pos[i]) >= new_dimensions[i]) return false;
            } else {
                if (pos[i] >= new_dimensions[i]) return false;
            }
        }
        return true;
    }

    auto translate(const pos_type& pos) const noexcept -> std::expected<pos_type, grid_error> {
        if (!in_view_bounds(pos)) return std::unexpected(grid_error::OutOfBounds);
        pos_type result{};
        for (std::size_t i = 0; i < std::tuple_size_v<pos_type>; ++i) {
            if constexpr (std::signed_integral<coord_type>) {
                auto value = static_cast<long long>(origin[i]) + static_cast<long long>(pos[i]);
                if (value < static_cast<long long>(std::numeric_limits<coord_type>::min()) ||
                    value > static_cast<long long>(std::numeric_limits<coord_type>::max()))
                    return std::unexpected(grid_error::OutOfBounds);
                result[i] = static_cast<coord_type>(value);
            } else {
                auto value = static_cast<unsigned long long>(origin[i]) + static_cast<unsigned long long>(pos[i]);
                if (value > static_cast<unsigned long long>(std::numeric_limits<coord_type>::max()))
                    return std::unexpected(grid_error::OutOfBounds);
                result[i] = static_cast<coord_type>(value);
            }
        }
        return result;
    }

    pos_type translate_unsafe(const pos_type& pos) const noexcept {
        pos_type result{};
        for (std::size_t i = 0; i < std::tuple_size_v<pos_type>; ++i)
            result[i] = static_cast<coord_type>(origin[i] + pos[i]);
        return result;
    }

    auto to_view_pos(const pos_type& source_pos) const noexcept -> std::expected<pos_type, grid_error> {
        pos_type result{};
        for (std::size_t i = 0; i < std::tuple_size_v<pos_type>; ++i) {
            if constexpr (std::signed_integral<coord_type>) {
                auto value = static_cast<long long>(source_pos[i]) - static_cast<long long>(origin[i]);
                if (value < 0 || value > static_cast<long long>(std::numeric_limits<coord_type>::max()) ||
                    static_cast<std::make_unsigned_t<coord_type>>(value) >= new_dimensions[i])
                    return std::unexpected(grid_error::OutOfBounds);
                result[i] = static_cast<coord_type>(value);
            } else {
                if (source_pos[i] < origin[i]) return std::unexpected(grid_error::OutOfBounds);
                auto value = static_cast<unsigned long long>(source_pos[i] - origin[i]);
                if (value >= static_cast<unsigned long long>(new_dimensions[i]))
                    return std::unexpected(grid_error::OutOfBounds);
                result[i] = static_cast<coord_type>(value);
            }
        }
        return result;
    }

   public:
    auto dimensions() const noexcept { return new_dimensions; }

    bool contains(const pos_type& pos) const noexcept(noexcept(grid.contains(std::declval<const pos_type&>()))) {
        auto source_pos = translate(pos);
        return source_pos.has_value() && grid.contains(source_pos.value());
    }

    auto get(const pos_type& pos) noexcept(noexcept(grid.get(std::declval<const pos_type&>())))
        -> decltype(grid.get(pos)) {
        auto source_pos = translate(pos);
        if (!source_pos.has_value()) return std::unexpected(source_pos.error());
        return grid.get(source_pos.value());
    }

    auto set(const pos_type& pos,
             cell_type val) noexcept(noexcept(grid.set(std::declval<const pos_type&>(), std::move(val))))
        -> std::expected<std::reference_wrapper<cell_type>, grid_error>
        requires grid_container<G>
    {
        auto source_pos = translate(pos);
        if (!source_pos.has_value()) return std::unexpected(source_pos.error());
        return grid.set(source_pos.value(), std::move(val));
    }

    template <typename... Args>
        requires grid_container<G> && std::constructible_from<cell_type, Args...>
    auto set_new(const pos_type& pos, Args&&... args) noexcept(noexcept(grid.set_new(pos, std::forward<Args>(args)...)))
        -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
        auto source_pos = translate(pos);
        if (!source_pos.has_value()) return std::unexpected(source_pos.error());
        return grid.set_new(source_pos.value(), std::forward<Args>(args)...);
    }

    auto remove(const pos_type& pos) noexcept(noexcept(grid.remove(std::declval<const pos_type&>())))
        -> std::expected<void, grid_error>
        requires grid_container<G>
    {
        auto source_pos = translate(pos);
        if (!source_pos.has_value()) return std::unexpected(source_pos.error());
        return grid.remove(source_pos.value());
    }

    auto take(const pos_type& pos) noexcept(noexcept(grid.take(std::declval<const pos_type&>())))
        -> std::expected<cell_type, grid_error>
        requires grid_container<G>
    {
        auto source_pos = translate(pos);
        if (!source_pos.has_value()) return std::unexpected(source_pos.error());
        return grid.take(source_pos.value());
    }

    decltype(auto) get_unsafe(const pos_type& pos) noexcept(noexcept(grid.get_unsafe(translate_unsafe(pos))))
        requires unsafe_viewable_grid<G>
    {
        return grid.get_unsafe(translate_unsafe(pos));
    }

    auto set_unsafe(const pos_type& pos,
                    cell_type val) noexcept(noexcept(grid.set_unsafe(translate_unsafe(pos), std::move(val))))
        -> decltype(grid.set_unsafe(translate_unsafe(pos), std::move(val)))
        requires unsafe_grid_container<G>
    {
        return grid.set_unsafe(translate_unsafe(pos), std::move(val));
    }

    void remove_unsafe(const pos_type& pos) noexcept(noexcept(grid.remove_unsafe(translate_unsafe(pos))))
        requires unsafe_grid_container<G>
    {
        grid.remove_unsafe(translate_unsafe(pos));
    }

    auto take_unsafe(const pos_type& pos) noexcept(noexcept(grid.take_unsafe(translate_unsafe(pos))))
        -> decltype(grid.take_unsafe(translate_unsafe(pos)))
        requires unsafe_grid_container<G>
    {
        return grid.take_unsafe(translate_unsafe(pos));
    }

    auto iter_pos()
        requires iterable_grid<G>
    {
        return std::ranges::elements_view<decltype(iter()), 0>(iter());
    }

    auto iter_cells()
        requires iterable_grid<G>
    {
        return std::ranges::elements_view<decltype(iter()), 1>(iter());
    }

    auto iter()
        requires iterable_grid<G>
    {
        return std::ranges::transform_view(
            std::ranges::filter_view(grid.iter(),
                                     [this](const auto& kv) { return to_view_pos(std::get<0>(kv)).has_value(); }),
            [this](const auto& kv) {
                return std::tuple<pos_type, decltype(std::get<1>(kv))>(to_view_pos(std::get<0>(kv)).value(),
                                                                       std::get<1>(kv));
            });
    }
};

/**
 * @brief Construct an @ref offset_view wrapping @p g with a new origin and dimensions.
 */
template <viewable_grid G>
offset_view<G> offset(
    G&& g,
    typename std::decay_t<G>::pos_type new_origin,
    std::remove_cvref_t<decltype(std::declval<const std::decay_t<G>&>().dimensions())>
        new_dimensions) noexcept(std::is_nothrow_constructible_v<offset_view<G>,
                                                                 G,
                                                                 typename std::decay_t<G>::pos_type,
                                                                 std::remove_cvref_t<
                                                                     decltype(std::declval<const std::decay_t<G>&>()
                                                                                  .dimensions())>>) {
    return {std::forward<G>(g), new_origin, new_dimensions};
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
 * Methods are added conditionally based on what G supports:
 *  - always:              `dimensions()`, `contains()`, `get()` (const)
 *  - non-const G:         `get()` (mutable), `iter_cells()`, `iter()`
 *  - unsafe G:            `get_unsafe()` (const / non-const)
 *
 * Structural mutation (`set`, `set_new`, `remove`, `take` and their unsafe
 * variants) is intentionally omitted — views should not change topology.
 */
template <viewable_grid G, std::invocable<const typename std::decay_t<G>::pos_type&> Pred>
struct shadow_view {
    using pos_type  = grid_trait<G>::pos_type;
    using cell_type = grid_trait<G>::cell_type;

    G grid;
    Pred pred;

    // ─── viewable_grid interface (always present) ─────────────────────────

    auto dimensions() const noexcept(noexcept(grid.dimensions())) { return grid.dimensions(); }

    bool contains(const pos_type& pos) const noexcept(noexcept(pred(pos)) && noexcept(grid.contains(pos))) {
        return static_cast<bool>(pred(pos)) && grid.contains(pos);
    }

    auto get(const pos_type& pos) noexcept(noexcept(pred(pos)) && noexcept(grid.get(pos))) -> decltype(grid.get(pos)) {
        if (!pred(pos)) return std::unexpected(grid_error::EmptyCell);
        return grid.get(pos);
    }

    // ─── unsafe_viewable_grid interface ──────────────────────────────────

    decltype(auto) get_unsafe(const pos_type& pos) noexcept(noexcept(grid.get_unsafe(pos)))
        requires unsafe_viewable_grid<G>
    {
        return grid.get_unsafe(pos);
    }

    // ─── iterable_grid interface ──────────────────────────────────────────

    auto iter_pos()
        requires iterable_grid<G>
    {
        return std::views::filter(grid.iter_pos(),
                                  [this](const pos_type& pos) { return static_cast<bool>(pred(pos)); });
    }

    auto iter_cells()
        requires iterable_grid<G>
    {
        return std::ranges::elements_view<decltype(iter()), 1>(iter());
    }

    auto iter()
        requires iterable_grid<G>
    {
        return std::views::filter(grid.iter(),
                                  [this](const auto& kv) { return static_cast<bool>(pred(std::get<0>(kv))); });
    }
};

/**
 * @brief Construct a @ref shadow_view wrapping @p g with @p pred.
 *
 * G is deduced as `decltype(g)` — a reference type for named lvalue variables,
 * or a value type when an rvalue is passed.
 */
template <viewable_grid G, std::invocable<const typename std::decay_t<G>::pos_type&> Pred>
shadow_view<G, std::decay_t<Pred>> shadow(G&& g, Pred&& pred) noexcept(
    std::is_nothrow_constructible_v<shadow_view<G, std::decay_t<Pred>>, G, Pred>) {
    return {std::forward<G>(g), std::forward<Pred>(pred)};
}

}  // namespace epix::ext::grid::views
