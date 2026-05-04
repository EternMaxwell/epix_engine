module;
#ifndef EPIX_IMPORT_STD
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stack>
#include <utility>
#include <vector>
#endif

export module epix.extension.grid:polygon;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import glm;
import :concepts;
import :bit_grid;
import :grid_view;

namespace epix::ext::grid {

// ─────────────────────────────────────────────────────────────────────────────
// Internal concept (not exported)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Minimal structural concept for 2-D boolean-queryable grids.
 *
 * Satisfied by `bit_grid<2>` and by any `any_grid<G>` whose pos_type is
 * `std::array<std::uint32_t, 2>`.
 * Used only internally so that algorithm implementations can be shared between
 * external grids and the internal `bit_grid<2>` bitmap.
 */
template <typename G>
concept poly_grid = requires(const G& g) {
    { g.dimensions() } -> std::convertible_to<std::array<std::uint32_t, 2>>;
    { g.contains(std::declval<const std::array<std::uint32_t, 2>&>()) } -> std::convertible_to<bool>;
};

// ─────────────────────────────────────────────────────────────────────────────
// Public types
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief A closed ring of integer-grid vertices, in CW order for outer rings
 *        and CCW order for hole rings (consistent with mapbox::earcut input).
 */
export struct Ring {
    std::vector<glm::ivec2> points;

    bool empty() const { return points.empty(); }
    std::size_t size() const { return points.size(); }
};

/**
 * @brief A simple polygon (one outer ring + zero or more holes).
 */
export struct Polygon {
    Ring outer;
    std::vector<Ring> holes;

    bool empty() const { return outer.empty(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Internal algorithms (work on bit_grid<2>)
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

/// @brief Rasterise any poly_grid into a bit_grid<2> snapshot.
template <poly_grid G>
bit_grid<2> rasterise(const G& g) {
    auto dims = g.dimensions();
    bit_grid<2> out(dims);
    for (std::uint32_t y = 0; y < dims[1]; ++y)
        for (std::uint32_t x = 0; x < dims[0]; ++x)
            if (g.contains({x, y})) (void)out.set({x, y});
    return out;
}

/// @brief Rasterise an any_grid with a predicate: only cells where pred(value) is true are set.
template <typename G, typename Pred>
    requires any_grid<G> && (std::tuple_size_v<typename G::pos_type> == 2) &&
             std::invocable<Pred, const typename G::cell_type&>
bit_grid<2> rasterise_with_pred(const G& g, Pred&& pred) {
    auto dims = g.dimensions();
    bit_grid<2> out(dims);
    for (std::uint32_t y = 0; y < dims[1]; ++y)
        for (std::uint32_t x = 0; x < dims[0]; ++x) {
            std::array<std::uint32_t, 2> pos{x, y};
            if (!g.contains(pos)) continue;
            auto r = g.get(pos);
            if (r && static_cast<bool>(pred(r->get()))) (void)out.set(pos);
        }
    return out;
}

/// @brief Return a bit_grid<2> of all border-reachable empty cells ("outland").
inline bit_grid<2> get_outland(const bit_grid<2>& g, bool include_diagonal = false) {
    auto dims = g.dimensions();
    bit_grid<2> outland(dims);
    std::stack<std::pair<std::int32_t, std::int32_t>> stack;

    auto gc = [&](const bit_grid<2>& gr, std::int32_t x, std::int32_t y) {
        return gr.contains({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)});
    };
    auto gs = [&](bit_grid<2>& gr, std::int32_t x, std::int32_t y) {
        (void)gr.set({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)});
    };
    auto inbounds = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && static_cast<std::uint32_t>(x) < dims[0] && static_cast<std::uint32_t>(y) < dims[1];
    };

    for (std::int32_t x = 0; x < static_cast<std::int32_t>(dims[0]); ++x) {
        if (!gc(g, x, 0)) stack.push({x, 0});
        if (!gc(g, x, static_cast<std::int32_t>(dims[1]) - 1)) stack.push({x, static_cast<std::int32_t>(dims[1]) - 1});
    }
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(dims[1]); ++y) {
        if (!gc(g, 0, y)) stack.push({0, y});
        if (!gc(g, static_cast<std::int32_t>(dims[0]) - 1, y)) stack.push({static_cast<std::int32_t>(dims[0]) - 1, y});
    }

    auto try_push = [&](std::int32_t x, std::int32_t y) {
        if (!inbounds(x, y)) return;
        if (gc(g, x, y) || gc(outland, x, y)) return;
        stack.push({x, y});
    };
    while (!stack.empty()) {
        auto [x, y] = stack.top();
        stack.pop();
        if (gc(outland, x, y)) continue;
        gs(outland, x, y);
        try_push(x - 1, y);
        try_push(x + 1, y);
        try_push(x, y - 1);
        try_push(x, y + 1);
        if (include_diagonal) {
            try_push(x - 1, y - 1);
            try_push(x + 1, y - 1);
            try_push(x - 1, y + 1);
            try_push(x + 1, y + 1);
        }
    }
    return outland;
}

/// @brief Split a bit_grid<2> into one grid per connected component.
inline std::vector<bit_grid<2>> split(const bit_grid<2>& g, bool include_diagonal = false) {
    auto dims = g.dimensions();
    std::vector<bit_grid<2>> components;
    bit_grid<2> visited = get_outland(g, include_diagonal);

    auto gc = [&](const bit_grid<2>& gr, std::int32_t x, std::int32_t y) {
        return gr.contains({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)});
    };
    auto gs = [&](bit_grid<2>& gr, std::int32_t x, std::int32_t y) {
        (void)gr.set({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)});
    };
    auto inbounds = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && static_cast<std::uint32_t>(x) < dims[0] && static_cast<std::uint32_t>(y) < dims[1];
    };

    while (true) {
        std::int32_t sx = -1, sy = -1;
        for (std::int32_t y = 0; y < static_cast<std::int32_t>(dims[1]) && sx == -1; ++y)
            for (std::int32_t x = 0; x < static_cast<std::int32_t>(dims[0]); ++x)
                if (!gc(visited, x, y) && gc(g, x, y)) {
                    sx = x;
                    sy = y;
                    break;
                }
        if (sx == -1) break;

        components.emplace_back(dims);
        auto& comp = components.back();
        std::stack<std::pair<std::int32_t, std::int32_t>> stack;
        stack.push({sx, sy});

        auto try_push = [&](std::int32_t x, std::int32_t y) {
            if (!inbounds(x, y)) return;
            if (gc(visited, x, y) || !gc(g, x, y)) return;
            stack.push({x, y});
        };
        while (!stack.empty()) {
            auto [x, y] = stack.top();
            stack.pop();
            if (gc(comp, x, y)) continue;
            gs(comp, x, y);
            gs(visited, x, y);
            try_push(x - 1, y);
            try_push(x + 1, y);
            try_push(x, y - 1);
            try_push(x, y + 1);
            if (include_diagonal) {
                try_push(x - 1, y - 1);
                try_push(x + 1, y - 1);
                try_push(x - 1, y + 1);
                try_push(x + 1, y + 1);
            }
        }
    }
    return components;
}

}  // namespace detail

/// @brief Public alias for the rasterised binary bitmap.
/// Useful for storing/comparing snapshots of any grid.
export using BinaryGrid = bit_grid<2>;

/// @brief Rasterise any 2-D any_grid into a BinaryGrid snapshot.
export template <typename G>
    requires any_grid<G> && (std::tuple_size_v<typename G::pos_type> == 2)
BinaryGrid rasterise(const G& g) {
    return detail::rasterise(g);
}

/// @brief Rasterise a bit_grid<2> (identity snapshot — bit_grid is not any_grid).
export inline BinaryGrid rasterise(const bit_grid<2>& g) {
    return detail::rasterise(g);
}

// ─────────────────────────────────────────────────────────────────────────────
// Outline tracing & hole detection
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Trace the outer outline of @p grid (in CW order on screen).
 *
 * @param grid              Any poly_grid (bit_grid<2> or any_grid with 2-D pos_type).
 * @param include_diagonal  If true, diagonally-adjacent occupied cells are connected.
 * @return Outline as a Ring of integer vertex coordinates.  Empty if grid is empty.
 */
export template <poly_grid G>
Ring find_outline(const G& grid, bool include_diagonal = false) {
    Ring out;
    static constexpr std::array<glm::ivec2, 4> move    = {glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(1, 0),
                                                          glm::ivec2(0, -1)};
    static constexpr std::array<glm::ivec2, 4> offsets = {glm::ivec2{-1, -1}, glm::ivec2{-1, 0}, glm::ivec2{0, 0},
                                                          glm::ivec2{0, -1}};
    auto dims                                          = grid.dimensions();
    auto gc                                            = [&](std::int32_t x, std::int32_t y) -> bool {
        if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= dims[0] || static_cast<std::uint32_t>(y) >= dims[1])
            return false;
        return grid.contains({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)});
    };
    glm::ivec2 start(-1, -1);
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(dims[1]) && start.x == -1; ++y)
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(dims[0]); ++x)
            if (gc(x, y)) {
                start = {x, y};
                break;
            }
    if (start.x == -1) return out;
    glm::ivec2 current = start;
    int dir            = 0;
    do {
        out.points.push_back(current);
        for (int ndir = (include_diagonal ? dir + 3 : dir + 1) % 4; ndir != (dir + 2) % 4;
             ndir     = (include_diagonal ? ndir + 1 : ndir + 3) % 4) {
            auto outside = current + offsets[ndir];
            auto inside  = current + offsets[(ndir + 1) % 4];
            if (!gc(outside.x, outside.y) && gc(inside.x, inside.y)) {
                current = current + move[ndir];
                if (dir == ndir) out.points.pop_back();
                dir = ndir;
                break;
            }
        }
    } while (current != start);
    return out;
}

/**
 * @brief Find holes (interior void regions enclosed by occupied cells) of @p grid.
 * Each ring is in CCW order (mapbox::earcut-friendly).
 */
export template <poly_grid G>
std::vector<Ring> find_holes(const G& grid, bool include_diagonal = false) {
    auto bg      = detail::rasterise(grid);
    auto dims    = bg.dimensions();
    auto outland = detail::get_outland(bg, !include_diagonal);
    bit_grid<2> voids(dims);
    for (std::uint32_t y = 0; y < dims[1]; ++y)
        for (std::uint32_t x = 0; x < dims[0]; ++x) {
            std::array<std::uint32_t, 2> p{x, y};
            if (!bg.contains(p) && !outland.contains(p)) (void)voids.set(p);
        }
    auto components = detail::split(voids, !include_diagonal);
    std::vector<Ring> holes;
    holes.reserve(components.size());
    for (auto& c : components) holes.emplace_back(find_outline(c, !include_diagonal));
    return holes;
}

// ─────────────────────────────────────────────────────────────────────────────
// Douglas–Peucker simplification
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

inline float perpendicular_distance_sq(const glm::ivec2& p, const glm::ivec2& a, const glm::ivec2& b) {
    glm::vec2 pa = glm::vec2(p - a);
    glm::vec2 ba = glm::vec2(b - a);
    float bb     = ba.x * ba.x + ba.y * ba.y;
    if (bb <= 0.0f) {
        return pa.x * pa.x + pa.y * pa.y;
    }
    float t        = (pa.x * ba.x + pa.y * ba.y) / bb;
    glm::vec2 proj = ba * t;
    glm::vec2 d    = pa - proj;
    return d.x * d.x + d.y * d.y;
}

inline void dp_recurse(
    std::span<const glm::ivec2> pts, float eps_sq, std::vector<std::uint8_t>& keep, std::size_t lo, std::size_t hi) {
    if (hi <= lo + 1) return;
    float max_d   = -1.0f;
    std::size_t k = lo;
    for (std::size_t i = lo + 1; i < hi; ++i) {
        float d = perpendicular_distance_sq(pts[i], pts[lo], pts[hi]);
        if (d > max_d) {
            max_d = d;
            k     = i;
        }
    }
    if (max_d > eps_sq) {
        keep[k] = 1u;
        dp_recurse(pts, eps_sq, keep, lo, k);
        dp_recurse(pts, eps_sq, keep, k, hi);
    }
}

}  // namespace detail

/**
 * @brief Douglas–Peucker line simplification on a closed polyline.
 *
 * @param pts      Input ring vertices (treated as a closed loop).
 * @param epsilon  Maximum perpendicular distance (in pixel units) a point may
 *                 deviate from the simplified line.  Set to 0 to keep all points.
 * @return Simplified ring.
 */
export inline Ring douglas_peucker(std::span<const glm::ivec2> pts, float epsilon) {
    Ring out;
    const std::size_t n = pts.size();
    if (n < 3 || epsilon <= 0.0f) {
        out.points.assign(pts.begin(), pts.end());
        return out;
    }
    std::vector<std::uint8_t> keep(n, 0u);
    keep.front() = 1u;
    keep.back()  = 1u;
    detail::dp_recurse(pts, epsilon * epsilon, keep, 0, n - 1);
    out.points.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (keep[i]) out.points.push_back(pts[i]);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// High-level polygon extraction
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Extract one polygon (outer ring + holes) from a connected grid.
 * If the grid has multiple components only the first is returned;
 * use @ref get_polygons_multi for multi-component grids.
 */
export template <poly_grid G>
Polygon get_polygon(const G& grid, bool include_diagonal = false) {
    Polygon p;
    p.outer = find_outline(grid, include_diagonal);
    if (p.outer.empty()) return p;
    p.holes = find_holes(grid, include_diagonal);
    return p;
}

/** @brief Like @ref get_polygon but simplifies all rings via Douglas–Peucker. */
export template <poly_grid G>
Polygon get_polygon_simplified(const G& grid, float epsilon = 1.0f, bool include_diagonal = false) {
    Polygon p = get_polygon(grid, include_diagonal);
    if (p.empty()) return p;
    p.outer = douglas_peucker(p.outer.points, epsilon);
    for (auto& h : p.holes) h = douglas_peucker(h.points, epsilon);
    return p;
}

/** @brief Extract one polygon per connected component of @p grid. */
export template <poly_grid G>
std::vector<Polygon> get_polygons_multi(const G& grid, bool include_diagonal = false) {
    auto bg    = detail::rasterise(grid);
    auto comps = detail::split(bg, include_diagonal);
    std::vector<Polygon> result;
    result.reserve(comps.size());
    for (auto& c : comps) result.push_back(get_polygon(c, include_diagonal));
    return result;
}

/** @brief Extract one simplified polygon per connected component of @p grid. */
export template <poly_grid G>
std::vector<Polygon> get_polygons_simplified_multi(const G& grid, float epsilon = 1.0f, bool include_diagonal = false) {
    auto bg    = detail::rasterise(grid);
    auto comps = detail::split(bg, include_diagonal);
    std::vector<Polygon> result;
    result.reserve(comps.size());
    for (auto& c : comps) result.push_back(get_polygon_simplified(c, epsilon, include_diagonal));
    return result;
}

}  // namespace epix::ext::grid
