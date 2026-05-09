module;
#ifndef EPIX_IMPORT_STD
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#endif

export module epix.extension.fallingsand:ops;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import glm;
import epix.core;
import :elements;
import :structs;

namespace epix::ext::fallingsand::ops {

// ──────────────────────────────────────────────────────────────────────────────
// Spawn — paint a shape with a given element id.
// ──────────────────────────────────────────────────────────────────────────────

/**
 * @brief Reusable command: place elements in a geometric shape.
 *
 * Construction is done through static factory functions:
 * @code
 *   sim.apply(ops::Spawn::circle({0, 0}, sand_id, 5));
 *   sim.apply(ops::Spawn::rect({-4, -4}, {4, 4}, water_id));
 *   sim.apply(ops::Spawn::rect_centered({0, 0}, water_id, {8, 8}));
 * @endcode
 */
export struct Spawn {
   private:
    struct Circle {
        glm::ivec2 center;
        std::size_t id;
        int radius;

        void operator()(SandSimulation& sim) const {
            const ElementRegistry& registry = sim.registry();
            const ElementBase& base         = registry[id];
            int r2                          = radius * radius;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    std::int64_t px  = center.x + dx;
                    std::int64_t py  = center.y + dy;
                    std::uint64_t sd = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(px)) << 32) |
                                       static_cast<std::uint64_t>(static_cast<std::uint32_t>(py));
                    sim.put_cell({px, py}, Element{id, base.color_func(sd)});
                }
            }
        }
    };
    struct Rect {
        glm::ivec2 min;
        glm::ivec2 max;
        std::size_t id;

        void operator()(SandSimulation& sim) const {
            const ElementRegistry& registry = sim.registry();
            const ElementBase& base         = registry[id];
            for (int dy = min.y; dy <= max.y; ++dy) {
                for (int dx = min.x; dx <= max.x; ++dx) {
                    std::int64_t px = dx, py = dy;
                    std::uint64_t sd = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(px)) << 32) |
                                       static_cast<std::uint64_t>(static_cast<std::uint32_t>(py));
                    sim.put_cell({px, py}, Element{id, base.color_func(sd)});
                }
            }
        }
    };

    std::variant<Circle, Rect> m_shape;
    template <typename Shape>
        requires std::constructible_from<decltype(m_shape), Shape>
    Spawn(Shape&& shape) noexcept(std::is_nothrow_constructible_v<decltype(m_shape), Shape>)
        : m_shape(std::forward<Shape>(shape)) {}

   public:
    Spawn(const Spawn&)            = default;
    Spawn(Spawn&&)                 = default;
    Spawn& operator=(const Spawn&) = default;
    Spawn& operator=(Spawn&&)      = default;

    void operator()(SandSimulation& sim) const {
        std::visit([&sim](const auto& shape) { shape(sim); }, m_shape);
    }

    /**
     * @brief Fill a circle of @p radius cells centred at @p center with element @p id.
     * Cells that already contain an element are overwritten.
     */
    static Spawn circle(glm::ivec2 center, std::size_t id, int radius) noexcept {
        return Spawn::Circle{center, id, radius};
    }

    /**
     * @brief Fill the axis-aligned rectangle from @p min (inclusive) to @p max (inclusive)
     * with element @p id.
     */
    static Spawn rect(glm::ivec2 min, glm::ivec2 max, std::size_t id) noexcept { return Spawn::Rect{min, max, id}; }

    /**
     * @brief Fill a rectangle centred at @p center of the given @p half-extents.
     */
    static Spawn rect_centered(glm::ivec2 center, std::size_t id, glm::ivec2 half_ext) noexcept {
        return rect({center.x - half_ext.x, center.y - half_ext.y}, {center.x + half_ext.x, center.y + half_ext.y}, id);
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Remove — erase a geometric shape.
// ──────────────────────────────────────────────────────────────────────────────

/**
 * @brief Reusable command: remove elements in a geometric shape.
 *
 * @code
 *   sim.apply(ops::Remove::circle({0, 0}, 5));
 *   sim.apply(ops::Remove::rect({-4, -4}, {4, 4}));
 *   sim.apply(ops::Remove::rect_centered({0, 0}, {8, 8}));
 * @endcode
 */
export struct Remove {
   private:
    struct Circle {
        glm::ivec2 center;
        int radius;

        void operator()(SandSimulation& sim) const {
            int r2 = radius * radius;
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    sim.erase_cell({center.x + dx, center.y + dy});
                }
        }
    };
    struct Rect {
        glm::ivec2 min;
        glm::ivec2 max;
        void operator()(SandSimulation& sim) const {
            for (int dy = min.y; dy <= max.y; ++dy)
                for (int dx = min.x; dx <= max.x; ++dx) sim.erase_cell({dx, dy});
        }
    };

    std::variant<Circle, Rect> m_shape;
    template <typename Shape>
        requires std::constructible_from<decltype(m_shape), Shape>
    Remove(Shape&& shape) noexcept(std::is_nothrow_constructible_v<decltype(m_shape), Shape>)
        : m_shape(std::forward<Shape>(shape)) {}

   public:
    Remove(const Remove&)            = default;
    Remove(Remove&&)                 = default;
    Remove& operator=(const Remove&) = default;
    Remove& operator=(Remove&&)      = default;

    void operator()(SandSimulation& sim) const {
        std::visit([&sim](const auto& shape) { shape(sim); }, m_shape);
    }

    static Remove circle(glm::ivec2 center, int radius) noexcept { return Remove(Remove::Circle{center, radius}); }

    static Remove rect(glm::ivec2 min, glm::ivec2 max) noexcept { return Remove(Remove::Rect{min, max}); }

    static Remove rect_centered(glm::ivec2 center, glm::ivec2 half_ext) noexcept {
        return rect({center.x - half_ext.x, center.y - half_ext.y}, {center.x + half_ext.x, center.y + half_ext.y});
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Explode — blast + outward impulse, builder-style configuration.
// ──────────────────────────────────────────────────────────────────────────────

/**
 * @brief Reusable command: explosion at a given cell position.
 *
 * Builder pattern — all setters return *this& for chaining:
 * @code
 *   sim.apply(ops::Explode({cx, cy})
 *       .with_intensity(800.0f)
 *       .with_blast_radius(4)
 *       .with_push_radius(12));
 * @endcode
 *
 * - **blast_radius**: cells within this distance are removed.
 * - **push_radius**: cells in the ring [blast_radius, push_radius] receive an outward
 *   velocity impulse = `intensity * (1 - dist/push_radius)`.
 */
export struct Explode {
    glm::ivec2 center = {};
    float intensity   = 500.0f;
    int blast_radius  = 5;
    int push_radius   = 15;

    explicit Explode(glm::ivec2 center_) noexcept : center(center_) {}

    Explode& with_intensity(float v) noexcept {
        intensity = v;
        return *this;
    }
    Explode& with_blast_radius(int r) noexcept {
        blast_radius = r;
        return *this;
    }
    Explode& with_push_radius(int r) noexcept {
        push_radius = r;
        return *this;
    }

    void operator()(SandSimulation& sim) const {
        int pr = std::max(blast_radius, push_radius);
        // Push ring first (before blast clears cells).
        for (int dy = -pr; dy <= pr; ++dy) {
            for (int dx = -pr; dx <= pr; ++dx) {
                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (dist > static_cast<float>(pr)) continue;
                if (dist < static_cast<float>(blast_radius)) continue;

                std::int64_t cx = center.x + dx;
                std::int64_t cy = center.y + dy;
                auto cell_opt   = sim.get_cell<Element>({cx, cy});
                if (!cell_opt.has_value()) continue;

                Element elem  = cell_opt->get();  // copy
                float falloff = 1.0f - dist / static_cast<float>(pr);
                glm::vec2 dir = dist > 0.001f ? glm::vec2{static_cast<float>(dx), static_cast<float>(dy)} / dist
                                              : glm::vec2{0.0f, 1.0f};
                elem.velocity += dir * intensity * falloff;
                elem.set_freefall(true);
                elem.not_move_count = 0;
                sim.put_cell({cx, cy}, std::move(elem));
            }
        }
        // Remove blast zone.
        for (int dy = -blast_radius; dy <= blast_radius; ++dy)
            for (int dx = -blast_radius; dx <= blast_radius; ++dx) {
                if (dx * dx + dy * dy > blast_radius * blast_radius) continue;
                sim.erase_cell({center.x + dx, center.y + dy});
            }
    }
};

}  // namespace epix::ext::fallingsand::ops
