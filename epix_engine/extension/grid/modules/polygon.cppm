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

namespace epix::ext::grid {

// ─────────────────────────────────────────────────────────────────────────────
// Concepts
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief A 2D grid that can be queried for cell occupancy.
 *
 * Required interface:
 *   - `g.size()` returns something convertible to `std::array<std::int32_t, 2>` ({width, height}).
 *   - `g.contains(x, y)` returns a bool indicating whether the cell at `(x, y)` is occupied.
 *
 * Out-of-bounds queries to `contains()` must return `false` (do not throw).
 */
export template <typename G>
concept BoolGrid = requires(const G& g, std::int32_t x, std::int32_t y) {
    { g.size() } -> std::convertible_to<std::array<std::int32_t, 2>>;
    { g.contains(x, y) } -> std::convertible_to<bool>;
};

/**
 * @brief A 2D grid whose cells store data that can be queried via `get(x, y)`.
 *
 * Used together with a predicate to define what counts as "occupied" for
 * polygon extraction. The predicate must be a callable `bool(const G&, int32_t, int32_t)`.
 *
 * Note: `contains(x, y)` is still required and means "cell present in the grid"
 * (e.g. for `dense_grid` / `sparse_grid` it's the structural occupancy check);
 * the polygon predicate further filters those cells based on stored data.
 */
export template <typename G>
concept DataGrid = requires(const G& g, std::int32_t x, std::int32_t y) {
    { g.size() } -> std::convertible_to<std::array<std::int32_t, 2>>;
    { g.contains(x, y) } -> std::convertible_to<bool>;
    g.get(x, y);
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
// Filtered view: adapt (DataGrid + predicate) -> BoolGrid
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Wraps a `DataGrid` together with a predicate so that the result
 *        satisfies `BoolGrid`.  `contains(x, y)` returns true iff
 *        `g.contains(x, y) && pred(g, x, y)`.
 *
 * The view holds references to the underlying grid and predicate; both must
 * outlive the view.  Constructed implicitly via @ref filter.
 */
export template <typename G, typename Pred>
struct FilteredView {
    const G& grid;
    Pred pred;  // by value: typically a lambda; cheap to copy/move

    std::array<std::int32_t, 2> size() const { return std::array<std::int32_t, 2>{grid.size()}; }
    bool contains(std::int32_t x, std::int32_t y) const {
        if (!grid.contains(x, y)) return false;
        return static_cast<bool>(pred(grid, x, y));
    }
};

/**
 * @brief Convenience factory for @ref FilteredView.
 *
 * Usage:
 * @code
 * auto poly = get_polygon_simplified(filter(chunk, [&](auto& g, int x, int y) {
 *     return registry[g.get(x, y).base_id].type == ElementType::Solid;
 * }));
 * @endcode
 */
export template <typename G, typename Pred>
FilteredView<G, std::decay_t<Pred>> filter(const G& g, Pred&& pred) {
    return FilteredView<G, std::decay_t<Pred>>{g, std::forward<Pred>(pred)};
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal binary grid (rasterised bitmap used by all algorithms)
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

/// @brief Fixed-size 2D bitmap.  Out-of-bounds queries return false.
struct binary_grid {
    std::int32_t w = 0;
    std::int32_t h = 0;
    std::vector<std::uint8_t> bits;  ///< row-major; 0/1

    binary_grid() = default;
    binary_grid(std::int32_t w_, std::int32_t h_) : w(w_), h(h_), bits(static_cast<std::size_t>(w_) * h_, 0u) {}

    std::array<std::int32_t, 2> size() const { return {w, h}; }
    std::int32_t size(int axis) const { return axis == 0 ? w : h; }

    bool in_bounds(std::int32_t x, std::int32_t y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    bool contains(std::int32_t x, std::int32_t y) const {
        if (!in_bounds(x, y)) return false;
        return bits[static_cast<std::size_t>(y) * w + x] != 0u;
    }
    void set(std::int32_t x, std::int32_t y, bool v = true) {
        if (!in_bounds(x, y)) return;
        bits[static_cast<std::size_t>(y) * w + x] = v ? 1u : 0u;
    }
};

/// @brief Rasterise any BoolGrid into a binary_grid.
template <typename G>
    requires BoolGrid<G>
binary_grid rasterise(const G& g) {
    auto sz = std::array<std::int32_t, 2>{g.size()};
    binary_grid out(sz[0], sz[1]);
    for (std::int32_t y = 0; y < sz[1]; ++y) {
        for (std::int32_t x = 0; x < sz[0]; ++x) {
            if (g.contains(x, y)) out.set(x, y, true);
        }
    }
    return out;
}

/// @brief Return a binary_grid of all cells reachable from the border that are
///        NOT occupied in @p g — i.e. the "outside" / outland region.
inline binary_grid get_outland(const binary_grid& g, bool include_diagonal = false) {
    binary_grid outland(g.w, g.h);
    std::stack<std::pair<std::int32_t, std::int32_t>> stack;
    for (std::int32_t x = 0; x < g.w; ++x) {
        if (!g.contains(x, 0)) stack.push({x, 0});
        if (!g.contains(x, g.h - 1)) stack.push({x, g.h - 1});
    }
    for (std::int32_t y = 0; y < g.h; ++y) {
        if (!g.contains(0, y)) stack.push({0, y});
        if (!g.contains(g.w - 1, y)) stack.push({g.w - 1, y});
    }
    auto try_push = [&](std::int32_t x, std::int32_t y) {
        if (!g.in_bounds(x, y)) return;
        if (g.contains(x, y) || outland.contains(x, y)) return;
        stack.push({x, y});
    };
    while (!stack.empty()) {
        auto [x, y] = stack.top();
        stack.pop();
        if (outland.contains(x, y)) continue;
        outland.set(x, y, true);
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

/// @brief Split a binary grid into one binary grid per connected component
///        of occupied cells.
inline std::vector<binary_grid> split(const binary_grid& g, bool include_diagonal = false) {
    std::vector<binary_grid> components;
    binary_grid visited = get_outland(g, include_diagonal);
    while (true) {
        std::int32_t sx = -1, sy = -1;
        for (std::int32_t y = 0; y < g.h && sx == -1; ++y) {
            for (std::int32_t x = 0; x < g.w; ++x) {
                if (!visited.contains(x, y) && g.contains(x, y)) {
                    sx = x;
                    sy = y;
                    break;
                }
            }
        }
        if (sx == -1) break;
        components.emplace_back(g.w, g.h);
        auto& comp = components.back();
        std::stack<std::pair<std::int32_t, std::int32_t>> stack;
        stack.push({sx, sy});
        auto try_push = [&](std::int32_t x, std::int32_t y) {
            if (!g.in_bounds(x, y)) return;
            if (visited.contains(x, y) || !g.contains(x, y)) return;
            stack.push({x, y});
        };
        while (!stack.empty()) {
            auto [x, y] = stack.top();
            stack.pop();
            if (comp.contains(x, y)) continue;
            comp.set(x, y, true);
            visited.set(x, y, true);
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

// ─────────────────────────────────────────────────────────────────────────────
// Outline tracing & hole detection (work on any BoolGrid)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Trace the outer outline of @p grid (in CW order on screen, i.e.
 *        suitable as a Box2D chain loop with cells lying to the right of each
 *        edge).
 *
 * @param grid              Any BoolGrid.
 * @param include_diagonal  If true, diagonally-adjacent occupied cells are
 *                          considered connected.
 * @return Outline as a Ring of integer vertex coordinates.  Empty if grid is empty.
 *
 * Algorithm: square-tracing / Moore-neighbour, ported verbatim from the
 * legacy `feature/pixel_b2d` branch.
 */
export template <typename G>
    requires BoolGrid<G>
Ring find_outline(const G& grid, bool include_diagonal = false) {
    Ring out;
    static constexpr std::array<glm::ivec2, 4> move    = {glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(1, 0),
                                                          glm::ivec2(0, -1)};
    static constexpr std::array<glm::ivec2, 4> offsets = {glm::ivec2{-1, -1}, glm::ivec2{-1, 0}, glm::ivec2{0, 0},
                                                          glm::ivec2{0, -1}};
    auto sz                                            = std::array<std::int32_t, 2>{grid.size()};
    glm::ivec2 start(-1, -1);
    for (std::int32_t y = 0; y < sz[1]; ++y) {
        for (std::int32_t x = 0; x < sz[0]; ++x) {
            if (grid.contains(x, y)) {
                start = {x, y};
                break;
            }
        }
        if (start.x != -1) break;
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
            if (!grid.contains(outside.x, outside.y) && grid.contains(inside.x, inside.y)) {
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
 *
 * Each returned ring is the outline of one hole, in CCW order (mapbox::earcut-friendly).
 */
export template <typename G>
    requires BoolGrid<G>
std::vector<Ring> find_holes(const G& grid, bool include_diagonal = false) {
    auto bg      = detail::rasterise(grid);
    auto outland = detail::get_outland(bg, !include_diagonal);
    // holes_solid[k] = component k of the "interior void" region
    // (non-occupied cells that are NOT in the outland)
    detail::binary_grid voids(bg.w, bg.h);
    for (std::int32_t y = 0; y < bg.h; ++y) {
        for (std::int32_t x = 0; x < bg.w; ++x) {
            if (!bg.contains(x, y) && !outland.contains(x, y)) voids.set(x, y, true);
        }
    }
    auto components = detail::split(voids, !include_diagonal);
    std::vector<Ring> holes;
    holes.reserve(components.size());
    for (auto& c : components) {
        holes.emplace_back(find_outline(c, !include_diagonal));
    }
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
 * @brief Extract one polygon (outer ring + holes) from a connected BoolGrid.
 *        If the grid contains multiple components only the first one found is
 *        returned;  use @ref get_polygons_multi for multi-component grids.
 */
export template <typename G>
    requires BoolGrid<G>
Polygon get_polygon(const G& grid, bool include_diagonal = false) {
    Polygon p;
    p.outer = find_outline(grid, include_diagonal);
    if (p.outer.empty()) return p;
    p.holes = find_holes(grid, include_diagonal);
    return p;
}

/**
 * @brief Like @ref get_polygon but additionally simplifies all rings via
 *        Douglas–Peucker with the given @p epsilon.
 */
export template <typename G>
    requires BoolGrid<G>
Polygon get_polygon_simplified(const G& grid, float epsilon = 1.0f, bool include_diagonal = false) {
    Polygon p = get_polygon(grid, include_diagonal);
    if (p.empty()) return p;
    p.outer = douglas_peucker(p.outer.points, epsilon);
    for (auto& h : p.holes) h = douglas_peucker(h.points, epsilon);
    return p;
}

/**
 * @brief Extract one polygon per connected component of @p grid.
 */
export template <typename G>
    requires BoolGrid<G>
std::vector<Polygon> get_polygons_multi(const G& grid, bool include_diagonal = false) {
    auto bg    = detail::rasterise(grid);
    auto comps = detail::split(bg, include_diagonal);
    std::vector<Polygon> result;
    result.reserve(comps.size());
    for (auto& c : comps) {
        result.push_back(get_polygon(c, include_diagonal));
    }
    return result;
}

/**
 * @brief Extract one simplified polygon per connected component of @p grid.
 */
export template <typename G>
    requires BoolGrid<G>
std::vector<Polygon> get_polygons_simplified_multi(const G& grid, float epsilon = 1.0f, bool include_diagonal = false) {
    auto bg    = detail::rasterise(grid);
    auto comps = detail::split(bg, include_diagonal);
    std::vector<Polygon> result;
    result.reserve(comps.size());
    for (auto& c : comps) {
        result.push_back(get_polygon_simplified(c, epsilon, include_diagonal));
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Predicate-based overloads (DataGrid + callable -> forwards to FilteredView)
// ─────────────────────────────────────────────────────────────────────────────

/// @copydoc find_outline
export template <typename G, typename Pred>
    requires DataGrid<G> && std::regular_invocable<Pred&, const G&, std::int32_t, std::int32_t>
Ring find_outline(const G& grid, Pred&& pred, bool include_diagonal = false) {
    return find_outline(filter(grid, std::forward<Pred>(pred)), include_diagonal);
}

/// @copydoc find_holes
export template <typename G, typename Pred>
    requires DataGrid<G> && std::regular_invocable<Pred&, const G&, std::int32_t, std::int32_t>
std::vector<Ring> find_holes(const G& grid, Pred&& pred, bool include_diagonal = false) {
    return find_holes(filter(grid, std::forward<Pred>(pred)), include_diagonal);
}

/// @copydoc get_polygon
export template <typename G, typename Pred>
    requires DataGrid<G> && std::regular_invocable<Pred&, const G&, std::int32_t, std::int32_t>
Polygon get_polygon(const G& grid, Pred&& pred, bool include_diagonal = false) {
    return get_polygon(filter(grid, std::forward<Pred>(pred)), include_diagonal);
}

/// @copydoc get_polygon_simplified
export template <typename G, typename Pred>
    requires DataGrid<G> && std::regular_invocable<Pred&, const G&, std::int32_t, std::int32_t>
Polygon get_polygon_simplified(const G& grid, Pred&& pred, float epsilon = 1.0f, bool include_diagonal = false) {
    return get_polygon_simplified(filter(grid, std::forward<Pred>(pred)), epsilon, include_diagonal);
}

/// @copydoc get_polygons_multi
export template <typename G, typename Pred>
    requires DataGrid<G> && std::regular_invocable<Pred&, const G&, std::int32_t, std::int32_t>
std::vector<Polygon> get_polygons_multi(const G& grid, Pred&& pred, bool include_diagonal = false) {
    return get_polygons_multi(filter(grid, std::forward<Pred>(pred)), include_diagonal);
}

/// @copydoc get_polygons_simplified_multi
export template <typename G, typename Pred>
    requires DataGrid<G> && std::regular_invocable<Pred&, const G&, std::int32_t, std::int32_t>
std::vector<Polygon> get_polygons_simplified_multi(const G& grid,
                                                   Pred&& pred,
                                                   float epsilon         = 1.0f,
                                                   bool include_diagonal = false) {
    return get_polygons_simplified_multi(filter(grid, std::forward<Pred>(pred)), epsilon, include_diagonal);
}

}  // namespace epix::ext::grid
