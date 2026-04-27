module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>
#endif
#include <spdlog/spdlog.h>

module epix.extension.fallingsand;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.tasks;

namespace epix::ext::fallingsand {

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation — private helpers
// ──────────────────────────────────────────────────────────────────────────────

SandSimulation::CellState SandSimulation::cell_state(std::int64_t x, std::int64_t y) const {
    auto cell = get_cell<Element>({x, y});
    if (cell.has_value()) return CellState::Occupied;
    return std::visit(
        [](auto&& error) -> CellState {
            using E = std::remove_cvref_t<decltype(error)>;
            if constexpr (std::is_same_v<E, grid::LayerError>) {
                return error == grid::LayerError::EmptyCell ? CellState::EmptyInChunk : CellState::Blocked;
            }
            return CellState::Blocked;
        },
        cell.error());
}

bool SandSimulation::has_cell(std::int64_t x, std::int64_t y) const { return cell_state(x, y) == CellState::Occupied; }

bool SandSimulation::set_cell(std::int64_t x, std::int64_t y, Element value) {
    return insert_cell({x, y}, std::move(value)).has_value();
}

bool SandSimulation::clear_cell(std::int64_t x, std::int64_t y) { return remove_cell<Element>({x, y}).has_value(); }

bool SandSimulation::move_cell(std::int64_t fx, std::int64_t fy, std::int64_t tx, std::int64_t ty) {
    if (cell_state(tx, ty) != CellState::EmptyInChunk) return false;
    auto from = get_cell<Element>({fx, fy});
    if (!from.has_value()) return false;
    Element moved = from->get();
    if (!remove_cell<Element>({fx, fy}).has_value()) return false;
    if (!insert_cell({tx, ty}, std::move(moved)).has_value()) return false;
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation::step_cells — default powder gravity logic.
// TODO: dispatch to per-element step functions once the interface is finalised.
// ──────────────────────────────────────────────────────────────────────────────

void SandSimulation::step_cells() {
    static std::uint64_t s_tick = 0;
    const std::uint64_t tick    = s_tick++;
    const std::int64_t cw       = static_cast<std::int64_t>(chunk_width());
    auto chunk_coords           = std::ranges::to<std::vector>(iter_chunk_pos());
    static auto rng             = std::mt19937{std::random_device{}()};
    std::shuffle(chunk_coords.begin(), chunk_coords.end(), rng);

    auto& pool = tasks::ComputeTaskPool::get();

    for (auto&& [rx, ry] : std::views::cartesian_product(std::views::iota(0, 3), std::views::iota(0, 3))) {
        std::vector<tasks::Task<void>> group_tasks;
        for (auto&& cpos : std::views::filter(chunk_coords, [rx, ry](auto&& cpos) {
                 auto x_r = (cpos[0] % 3 + 3) % 3;
                 auto y_r = (cpos[1] % 3 + 3) % 3;
                 return x_r == rx && y_r == ry;
             })) {
            group_tasks.push_back(pool.spawn([cpos, cw, tick, this] {
                for (auto&& [x, y] : std::views::transform(
                         std::views::elements<0>(get_chunk(cpos).value().get().iter<Element>()),
                         [cpos, cw](auto&& cell_pos) {
                             return std::array<std::int64_t, 2>{
                                 static_cast<std::int64_t>(cpos[0]) * cw + static_cast<std::int64_t>(cell_pos[0]),
                                 static_cast<std::int64_t>(cpos[1]) * cw + static_cast<std::int64_t>(cell_pos[1]),
                             };
                         })) {
                    if (!has_cell(x, y)) continue;
                    if (move_cell(x, y, x, y - 1)) continue;
                    const bool prefer_left = ((x + y + static_cast<std::int64_t>(tick)) & 1) == 0;
                    if (prefer_left) {
                        if (move_cell(x, y, x - 1, y - 1)) continue;
                        (void)move_cell(x, y, x + 1, y - 1);
                    } else {
                        if (move_cell(x, y, x + 1, y - 1)) continue;
                        (void)move_cell(x, y, x - 1, y - 1);
                    }
                }
            }));
        }
        for (auto& t : group_tasks) t.block();
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation — public methods
// ──────────────────────────────────────────────────────────────────────────────

void SandSimulation::step() { step_cells(); }

}  // namespace epix::ext::fallingsand