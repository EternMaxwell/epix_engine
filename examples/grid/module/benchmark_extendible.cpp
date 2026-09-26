#ifndef EPIX_IMPORT_STD
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <print>
#include <string>
#include <vector>
#endif
import epix.extension.grid;

#ifdef EPIX_IMPORT_STD
import std;
#endif

using namespace epix::ext::grid;

namespace {
using pos2 = std::array<std::int32_t, 2>;

struct benchmark_row {
    std::string scenario;
    double dense_ms;
    double dense_reserved_ms;
    double tree_ms;
};

template <typename Expected>
void require_success(const Expected& result, std::string_view context) {
    if (!result.has_value()) {
        throw std::runtime_error(std::string(context));
    }
}

std::vector<pos2> make_tight_positions(std::int32_t width, std::int32_t height) {
    std::vector<pos2> positions;
    positions.reserve(static_cast<std::size_t>(width * height));
    for (std::int32_t y = 0; y < height; y++) {
        for (std::int32_t x = 0; x < width; x++) {
            positions.push_back({x, y});
        }
    }
    return positions;
}

std::vector<pos2> make_spread_positions(std::size_t count, std::int32_t spacing, std::int32_t rows) {
    std::vector<pos2> positions;
    positions.reserve(count);
    for (std::size_t index = 0; index < count; index++) {
        const auto x = static_cast<std::int32_t>(index) * spacing;
        const auto y = static_cast<std::int32_t>(index % static_cast<std::size_t>(rows));
        positions.push_back({x, y});
    }
    return positions;
}

template <typename Grid, typename Scenario>
double run_benchmark(std::string_view grid_name,
                     std::string_view scenario_name,
                     Scenario&& scenario,
                     int iterations = 5) {
    double best_ms = std::numeric_limits<double>::max();
    for (int iteration = 0; iteration < iterations; iteration++) {
        Grid grid;
        const auto start = std::chrono::steady_clock::now();
        try {
            scenario(grid);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string(scenario_name) + " [" + std::string(grid_name) + "]: " + error.what());
        }
        const auto end        = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        best_ms               = std::min(best_ms, elapsed_ms);
    }
    return best_ms;
}

template <typename Scenario>
double run_dense_reserved_benchmark(
    std::string_view scenario_name, pos2 reserve_min, pos2 reserve_max, Scenario&& scenario, int iterations = 5) {
    double best_ms = std::numeric_limits<double>::max();
    for (int iteration = 0; iteration < iterations; iteration++) {
        dense_extendible_grid<2, int> grid;
        grid.extend(reserve_min, reserve_max);

        const auto start = std::chrono::steady_clock::now();
        try {
            scenario(grid);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string(scenario_name) + " [dense_reserved]: " + error.what());
        }
        const auto end        = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        best_ms               = std::min(best_ms, elapsed_ms);
    }
    return best_ms;
}

template <typename Scenario>
benchmark_row compare_grids(std::string scenario_name, pos2 reserve_min, pos2 reserve_max, Scenario&& scenario) {
    return benchmark_row{
        .scenario          = std::move(scenario_name),
        .dense_ms          = run_benchmark<dense_extendible_grid<2, int>>("dense", scenario_name, scenario),
        .dense_reserved_ms = run_dense_reserved_benchmark(scenario_name, reserve_min, reserve_max, scenario),
        .tree_ms           = run_benchmark<tree_extendible_grid<2, int>>("tree", scenario_name, scenario),
    };
}

void print_report(const std::vector<benchmark_row>& rows) {
    std::println("\nExtendible grid benchmark (best of 5, ms)");
    std::println("{:<28}{:>14}{:>14}{:>14}{:>14}{:>14}", "Scenario", "Dense", "DenseRes", "Tree", "Res/Dense",
                 "Tree/Res");
    std::println("{}", std::string(98, '-'));

    for (const auto& row : rows) {
        const double reserve_ratio = row.dense_ms > 0.0 ? row.dense_reserved_ms / row.dense_ms : 0.0;
        const double tree_ratio    = row.dense_reserved_ms > 0.0 ? row.tree_ms / row.dense_reserved_ms : 0.0;
        std::println("{:<28}{:>14.3f}{:>14.3f}{:>14.3f}{:>14.3f}{:>14.3f}", row.scenario, row.dense_ms,
                     row.dense_reserved_ms, row.tree_ms, reserve_ratio, tree_ratio);
    }
}
}  // namespace

int main() {
    const auto tight_positions  = make_tight_positions(64, 64);
    const auto spread_positions = make_spread_positions(4096, 16, 4);
    const pos2 tight_min{0, 0};
    const pos2 tight_max{63, 63};
    const pos2 spread_min{0, 0};
    const pos2 spread_max{spread_positions.back()[0], 3};

    std::vector<benchmark_row> rows;
    rows.reserve(5);

    rows.push_back(compare_grids("tight_insert", tight_min, tight_max, [&](auto& grid) {
        int value = 0;
        for (const auto& pos : tight_positions) {
            require_success(grid.set(pos, value++), "tight_insert set failed");
        }
    }));

    rows.push_back(compare_grids("spread_insert", spread_min, spread_max, [&](auto& grid) {
        int value = 0;
        for (const auto& pos : spread_positions) {
            require_success(grid.set(pos, value++), "spread_insert set failed");
        }
    }));

    rows.push_back(compare_grids("tight_churn", tight_min, tight_max, [&](auto& grid) {
        for (std::size_t index = 0; index < 2048; index++) {
            require_success(grid.set(tight_positions[index], static_cast<int>(index)), "tight_churn seed failed");
        }

        for (int round = 0; round < 8; round++) {
            for (std::size_t index = round % 2; index < 2048; index += 2) {
                require_success(grid.remove(tight_positions[index]), "tight_churn remove failed");
            }
            for (std::size_t index = round % 2; index < 2048; index += 2) {
                require_success(grid.set(tight_positions[index], static_cast<int>(index + round)),
                                "tight_churn reinsert failed");
            }
        }
    }));

    rows.push_back(compare_grids("spread_churn", spread_min, spread_max, [&](auto& grid) {
        for (std::size_t index = 0; index < spread_positions.size(); index++) {
            require_success(grid.set(spread_positions[index], static_cast<int>(index)), "spread_churn seed failed");
        }

        for (int round = 0; round < 6; round++) {
            for (std::size_t index = round % 3; index < spread_positions.size(); index += 3) {
                require_success(grid.remove(spread_positions[index]), "spread_churn remove failed");
            }
            for (std::size_t index = round % 3; index < spread_positions.size(); index += 3) {
                require_success(grid.set(spread_positions[index], static_cast<int>(index + round)),
                                "spread_churn reinsert failed");
            }
        }
    }));

    rows.push_back(compare_grids("spread_remove_shrink", spread_min, spread_max, [&](auto& grid) {
        for (std::size_t index = 0; index < spread_positions.size(); index++) {
            require_success(grid.set(spread_positions[index], static_cast<int>(index)),
                            "spread_remove_shrink seed failed");
        }

        for (std::size_t index = 0; index + 64 < spread_positions.size(); index++) {
            require_success(grid.remove(spread_positions[index]), "spread_remove_shrink remove failed");
        }

        grid.shrink();
    }));

    print_report(rows);
}