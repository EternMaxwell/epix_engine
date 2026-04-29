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
    std::function<void(SandSimulation&)> m_fn;
    explicit Spawn(std::function<void(SandSimulation&)> fn) : m_fn(std::move(fn)) {}

   public:
    void operator()(SandSimulation& sim) const { m_fn(sim); }

    /**
     * @brief Fill a circle of @p radius cells centred at @p center with element @p id.
     * Cells that already contain an element are overwritten.
     */
    static Spawn circle(glm::ivec2 center, std::size_t id, int radius) {
        return Spawn([center, id, radius](SandSimulation& sim) {
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
        });
    }

    /**
     * @brief Fill the axis-aligned rectangle from @p min (inclusive) to @p max (inclusive)
     * with element @p id.
     */
    static Spawn rect(glm::ivec2 min, glm::ivec2 max, std::size_t id) {
        return Spawn([min, max, id](SandSimulation& sim) {
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
        });
    }

    /**
     * @brief Fill a rectangle centred at @p center of the given @p half-extents.
     */
    static Spawn rect_centered(glm::ivec2 center, std::size_t id, glm::ivec2 half_ext) {
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
    std::function<void(SandSimulation&)> m_fn;
    explicit Remove(std::function<void(SandSimulation&)> fn) : m_fn(std::move(fn)) {}

   public:
    void operator()(SandSimulation& sim) const { m_fn(sim); }

    static Remove circle(glm::ivec2 center, int radius) {
        return Remove([center, radius](SandSimulation& sim) {
            int r2 = radius * radius;
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    sim.erase_cell({center.x + dx, center.y + dy});
                }
        });
    }

    static Remove rect(glm::ivec2 min, glm::ivec2 max) {
        return Remove([min, max](SandSimulation& sim) {
            for (int dy = min.y; dy <= max.y; ++dy)
                for (int dx = min.x; dx <= max.x; ++dx) sim.erase_cell({dx, dy});
        });
    }

    static Remove rect_centered(glm::ivec2 center, glm::ivec2 half_ext) {
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

    explicit Explode(glm::ivec2 center_) : center(center_) {}

    Explode& with_intensity(float v) {
        intensity = v;
        return *this;
    }
    Explode& with_blast_radius(int r) {
        blast_radius = r;
        return *this;
    }
    Explode& with_push_radius(int r) {
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
