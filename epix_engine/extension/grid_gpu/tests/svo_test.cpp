#include <gtest/gtest.h>

import std;
import epix.extension.grid;
import epix.extension.grid_gpu;

using namespace epix::ext::grid;
using namespace epix::ext::grid_gpu;

// ============================================================
// Buffer traversal helper
// Mirrors the GPU shader traversal logic on the CPU so we can
// validate the serialised buffer without running a GPU.
// ============================================================

/// Walk the flat SvoBuffer and return the DATA INDEX at position `pos`
/// (relative coords, already adjusted for origin), or -1 if absent.
/// child_count = ChildCount (per-axis branching factor, default 2).
static int svo_cpu_lookup(const SvoBuffer& buf, const std::vector<int32_t>& pos, uint32_t child_count = 2u) {
    const auto& w = buf.words;
    if (w.size() < 2u) return -1;

    SvoHeader hdr  = buf.header();
    uint32_t dim   = hdr.dim;
    uint32_t depth = hdr.depth;
    uint32_t cpn   = hdr.child_per_node;

    if (depth == 0u || hdr.data_count == 0u) return -1;

    // Read origin
    std::vector<int32_t> origin(dim);
    for (uint32_t i = 0; i < dim; ++i) origin[i] = static_cast<int32_t>(w[2u + i]);

    // Compute coverage at root
    uint32_t coverage = 1u;
    for (uint32_t i = 0; i < depth; ++i) coverage *= child_count;

    // pos relative to origin
    std::vector<uint32_t> rel(dim);
    for (uint32_t axis = 0; axis < dim; ++axis) {
        int64_t delta = static_cast<int64_t>(pos[axis]) - static_cast<int64_t>(origin[axis]);
        if (delta < 0 || static_cast<uint64_t>(delta) >= coverage) return -1;
        rel[axis] = static_cast<uint32_t>(delta);
    }

    uint32_t pool_base = 2u + dim;   // word index of root descriptor
    uint32_t node_idx  = pool_base;  // current descriptor word index

    for (uint32_t level = 0u; level < depth; ++level) {
        // stride at this level
        uint32_t stride = 1u;
        for (uint32_t i = 0u; i < depth - level - 1u; ++i) stride *= child_count;

        // flat child index (row-major, axis 0 MSB)
        uint32_t ci = 0u;
        for (uint32_t axis = 0u; axis < dim; ++axis) {
            uint32_t digit = (rel[axis] / stride) % child_count;
            ci             = ci * child_count + digit;
        }

        uint32_t word       = w[node_idx];
        uint32_t valid_mask = word & ((1u << cpn) - 1u);
        uint32_t leaf_mask  = (word >> cpn) & ((1u << cpn) - 1u);
        // child_offset is always 1 and is no longer encoded in the descriptor
        if (!((valid_mask >> ci) & 1u)) return -1;  // absent
        uint32_t pbc      = static_cast<uint32_t>(std::popcount(valid_mask & ((1u << ci) - 1u)));
        uint32_t slot_pos = node_idx + 1u + pbc;

        if ((leaf_mask >> ci) & 1u) return static_cast<int>(w[slot_pos]);  // data index

        node_idx = w[slot_pos];  // follow interior child
    }

    return -1;
}

// ============================================================
// tree_extendible_grid 2D 鈥?basic tests
// ============================================================

TEST(SvoUpload2D, EmptyGrid) {
    tree_extendible_grid<2, int> grid;
    SvoBuffer buf = svo_upload(grid).value();

    ASSERT_GE(buf.size(), 2u);
    SvoHeader hdr = buf.header();
    EXPECT_EQ(hdr.dim, 2u);
    EXPECT_EQ(hdr.data_count, 0u);
    EXPECT_EQ(hdr.depth, 0u);
}

TEST(SvoUpload2D, SingleCell) {
    tree_extendible_grid<2, int> grid;
    grid.set({3, 5}, 42);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.dim, 2u);
    EXPECT_EQ(hdr.data_count, 1u);
    EXPECT_GT(hdr.depth, 0u);
    EXPECT_EQ(hdr.child_per_node, 4u);  // 2^Dim = 2^2 = 4

    // The only cell should have data index 0
    int idx = svo_cpu_lookup(buf, {3, 5});
    EXPECT_EQ(idx, 0);

    // Absent positions should return -1
    EXPECT_EQ(svo_cpu_lookup(buf, {0, 0}), -1);
    EXPECT_EQ(svo_cpu_lookup(buf, {3, 4}), -1);
}

TEST(SvoUpload2D, MultipleCells) {
    tree_extendible_grid<2, int> grid;
    // Insert in order 鈥?iter_pos preserves insertion order
    grid.set({0, 0}, 10);
    grid.set({1, 0}, 20);
    grid.set({0, 1}, 30);
    grid.set({3, 2}, 40);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.data_count, 4u);

    // Ordinal 0 = first inserted = {0,0}
    EXPECT_EQ(svo_cpu_lookup(buf, {0, 0}), 0);
    EXPECT_EQ(svo_cpu_lookup(buf, {1, 0}), 1);
    EXPECT_EQ(svo_cpu_lookup(buf, {0, 1}), 2);
    EXPECT_EQ(svo_cpu_lookup(buf, {3, 2}), 3);

    // Absent
    EXPECT_EQ(svo_cpu_lookup(buf, {1, 1}), -1);
    EXPECT_EQ(svo_cpu_lookup(buf, {2, 2}), -1);
}

TEST(SvoUpload2D, IndexMatchesIterPosOrder) {
    tree_extendible_grid<2, int> grid;
    grid.set({-1, 2}, 0);
    grid.set({0, 0}, 1);
    grid.set({2, 3}, 2);
    grid.set({-3, -3}, 3);

    SvoBuffer buf = svo_upload(grid).value();

    // Collect positions in CPU iteration order
    std::vector<std::array<int32_t, 2>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    // Each position must map to its ordinal index
    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        int idx = svo_cpu_lookup(buf, {positions[i][0], positions[i][1]});
        EXPECT_EQ(idx, static_cast<int>(i)) << "Position (" << positions[i][0] << "," << positions[i][1]
                                            << ") expected index " << i << " but got " << idx;
    }
}

// ============================================================
// tree_extendible_grid 3D
// ============================================================

TEST(SvoUpload3D, SingleCell) {
    tree_extendible_grid<3, int> grid;
    grid.set({1, 2, 3}, 99);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.dim, 3u);
    EXPECT_EQ(hdr.data_count, 1u);
    EXPECT_EQ(hdr.child_per_node, 8u);  // 2^3

    EXPECT_EQ(svo_cpu_lookup(buf, {1, 2, 3}), 0);
    EXPECT_EQ(svo_cpu_lookup(buf, {1, 2, 4}), -1);
}

TEST(SvoUpload3D, MultipleCells) {
    tree_extendible_grid<3, int> grid;
    grid.set({0, 0, 0}, 1);
    grid.set({1, 1, 1}, 2);
    grid.set({0, 1, 0}, 3);
    grid.set({5, 3, 2}, 4);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.data_count, 4u);

    std::vector<std::array<int32_t, 3>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        int idx = svo_cpu_lookup(buf, {p[0], p[1], p[2]});
        EXPECT_EQ(idx, static_cast<int>(i))
            << "Position (" << p[0] << "," << p[1] << "," << p[2] << ") expected " << i << " got " << idx;
    }
}

// ============================================================
// tree_grid (unsigned coords) 2D
// ============================================================

TEST(SvoUploadTreeGrid2D, BasicLookup) {
    tree_grid<2, int> grid({8u, 8u});
    grid.set({0u, 0u}, 10);
    grid.set({3u, 5u}, 20);
    grid.set({7u, 7u}, 30);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.dim, 2u);
    EXPECT_EQ(hdr.data_count, 3u);

    std::vector<std::array<uint32_t, 2>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        int idx = svo_cpu_lookup(buf, {static_cast<int32_t>(p[0]), static_cast<int32_t>(p[1])});
        EXPECT_EQ(idx, static_cast<int>(i));
    }
}

TEST(SvoUploadTreeGrid2D, EmptyGrid) {
    tree_grid<2, int> grid({4u, 4u});
    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.data_count, 0u);
}

// ============================================================
// tree_grid 3D
// ============================================================

TEST(SvoUploadTreeGrid3D, BasicLookup) {
    tree_grid<3, int> grid({4u, 4u, 4u});
    grid.set({0u, 0u, 0u}, 1);
    grid.set({1u, 2u, 3u}, 2);
    grid.set({3u, 3u, 3u}, 3);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.dim, 3u);
    EXPECT_EQ(hdr.data_count, 3u);

    std::vector<std::array<uint32_t, 3>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        int idx =
            svo_cpu_lookup(buf, {static_cast<int32_t>(p[0]), static_cast<int32_t>(p[1]), static_cast<int32_t>(p[2])});
        EXPECT_EQ(idx, static_cast<int>(i));
    }
}

// ============================================================
// Buffer header integrity
// ============================================================

TEST(SvoHeader, FieldEncoding) {
    tree_extendible_grid<2, int> grid;
    grid.set({0, 0}, 1);
    grid.set({1, 1}, 2);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.dim, 2u);
    EXPECT_EQ(hdr.child_per_node, 4u);
    EXPECT_GT(hdr.depth, 0u);
    EXPECT_EQ(hdr.data_count, 2u);

    // data is accessible via raw pointer
    ASSERT_NE(buf.data(), nullptr);
    EXPECT_GT(buf.byte_size(), 0u);
    EXPECT_EQ(buf.byte_size(), buf.size() * sizeof(uint32_t));
}

// ============================================================
// 1D tree
// ============================================================

TEST(SvoUpload1D, BasicLookup) {
    tree_extendible_grid<1, int> grid;
    grid.set({0}, 10);
    grid.set({3}, 20);
    grid.set({7}, 30);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.dim, 1u);
    EXPECT_EQ(hdr.data_count, 3u);
    EXPECT_EQ(hdr.child_per_node, 2u);  // 2^1

    std::vector<std::array<int32_t, 1>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        int idx = svo_cpu_lookup(buf, {positions[i][0]});
        EXPECT_EQ(idx, static_cast<int>(i));
    }
}

// ============================================================
// Dense grid (all leaves)
// ============================================================

TEST(SvoUpload2D, DenseSmallGrid) {
    // Fill all 4 cells of a 2x2 grid
    tree_extendible_grid<2, int> grid;
    grid.set({0, 0}, 1);
    grid.set({0, 1}, 2);
    grid.set({1, 0}, 3);
    grid.set({1, 1}, 4);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.data_count, 4u);

    std::vector<std::array<int32_t, 2>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        EXPECT_EQ(svo_cpu_lookup(buf, {p[0], p[1]}), static_cast<int>(i));
    }

    // Absent positions
    EXPECT_EQ(svo_cpu_lookup(buf, {2, 0}), -1);
    EXPECT_EQ(svo_cpu_lookup(buf, {0, 2}), -1);
}

// ============================================================
// dense_grid (unsigned coords, fixed-size sparse storage)
// ============================================================

TEST(SvoUploadDenseGrid2D, EmptyGrid) {
    dense_grid<2, int> grid({8u, 8u});
    SvoBuffer buf = svo_upload(grid).value();
    EXPECT_EQ(buf.header().data_count, 0u);
    EXPECT_EQ(buf.header().dim, 2u);
}

TEST(SvoUploadDenseGrid2D, SingleCell) {
    dense_grid<2, int> grid({8u, 8u});
    grid.set({3u, 5u}, 42);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.dim, 2u);
    EXPECT_EQ(hdr.data_count, 1u);
    EXPECT_EQ(hdr.child_per_node, 4u);
    EXPECT_EQ(svo_cpu_lookup(buf, {3, 5}), 0);
    EXPECT_EQ(svo_cpu_lookup(buf, {0, 0}), -1);
}

TEST(SvoUploadDenseGrid2D, MultipleCells_IndexOrder) {
    dense_grid<2, int> grid({8u, 8u});
    grid.set({0u, 0u}, 10);
    grid.set({3u, 5u}, 20);
    grid.set({7u, 7u}, 30);

    SvoBuffer buf = svo_upload(grid).value();
    EXPECT_EQ(buf.header().data_count, 3u);

    std::vector<std::array<uint32_t, 2>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        EXPECT_EQ(svo_cpu_lookup(buf, {static_cast<int32_t>(p[0]), static_cast<int32_t>(p[1])}), static_cast<int>(i));
    }
}

TEST(SvoUploadDenseGrid3D, BasicLookup) {
    dense_grid<3, int> grid({4u, 4u, 4u});
    grid.set({0u, 0u, 0u}, 1);
    grid.set({1u, 2u, 3u}, 2);
    grid.set({3u, 3u, 3u}, 3);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.dim, 3u);
    EXPECT_EQ(hdr.data_count, 3u);

    std::vector<std::array<uint32_t, 3>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        int idx =
            svo_cpu_lookup(buf, {static_cast<int32_t>(p[0]), static_cast<int32_t>(p[1]), static_cast<int32_t>(p[2])});
        EXPECT_EQ(idx, static_cast<int>(i));
    }
    EXPECT_EQ(svo_cpu_lookup(buf, {2, 0, 0}), -1);
}

// ============================================================
// sparse_grid (unsigned coords, stable indices, recycling)
// ============================================================

TEST(SvoUploadSparseGrid2D, EmptyGrid) {
    sparse_grid<2, int> grid({8u, 8u});
    SvoBuffer buf = svo_upload(grid).value();
    EXPECT_EQ(buf.header().data_count, 0u);
    EXPECT_EQ(buf.header().dim, 2u);
}

TEST(SvoUploadSparseGrid2D, SingleCell) {
    sparse_grid<2, int> grid({8u, 8u});
    grid.set({2u, 4u}, 99);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.data_count, 1u);
    EXPECT_EQ(svo_cpu_lookup(buf, {2, 4}), 0);
    EXPECT_EQ(svo_cpu_lookup(buf, {0, 0}), -1);
}

TEST(SvoUploadSparseGrid2D, InsertRemoveInsert_IndicesReassigned) {
    // Insert three cells, remove the middle one, insert a new cell.
    // svo_upload rebuilds indices by iter_pos order (valid positions only).
    sparse_grid<2, int> grid({8u, 8u});
    grid.set({0u, 0u}, 10);
    grid.set({1u, 1u}, 20);
    grid.set({2u, 2u}, 30);
    grid.remove({1u, 1u});
    grid.set({3u, 3u}, 40);

    // After remove+insert, iter_pos visits remaining: {0,0}, {2,2}, {3,3}
    SvoBuffer buf = svo_upload(grid).value();
    EXPECT_EQ(buf.header().data_count, 3u);

    std::vector<std::array<uint32_t, 2>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        EXPECT_EQ(svo_cpu_lookup(buf, {static_cast<int32_t>(p[0]), static_cast<int32_t>(p[1])}), static_cast<int>(i));
    }
    EXPECT_EQ(svo_cpu_lookup(buf, {1, 1}), -1);
}

TEST(SvoUploadSparseGrid3D, BasicLookup) {
    sparse_grid<3, int> grid({4u, 4u, 4u});
    grid.set({0u, 0u, 0u}, 1);
    grid.set({2u, 3u, 1u}, 2);

    SvoBuffer buf = svo_upload(grid).value();
    EXPECT_EQ(buf.header().data_count, 2u);

    std::vector<std::array<uint32_t, 3>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        int idx =
            svo_cpu_lookup(buf, {static_cast<int32_t>(p[0]), static_cast<int32_t>(p[1]), static_cast<int32_t>(p[2])});
        EXPECT_EQ(idx, static_cast<int>(i));
    }
}

// ============================================================
// dense_extendible_grid (signed coords, resizable)
// ============================================================

TEST(SvoUploadDenseExtGrid2D, EmptyGrid) {
    dense_extendible_grid<2, int> grid;
    SvoBuffer buf = svo_upload(grid).value();
    EXPECT_EQ(buf.header().data_count, 0u);
    EXPECT_EQ(buf.header().dim, 2u);
}

TEST(SvoUploadDenseExtGrid2D, SingleCell) {
    dense_extendible_grid<2, int> grid;
    grid.set({-3, 5}, 42);

    SvoBuffer buf = svo_upload(grid).value();
    SvoHeader hdr = buf.header();

    EXPECT_EQ(hdr.data_count, 1u);
    EXPECT_EQ(hdr.child_per_node, 4u);
    EXPECT_EQ(svo_cpu_lookup(buf, {-3, 5}), 0);
    EXPECT_EQ(svo_cpu_lookup(buf, {0, 0}), -1);
}

TEST(SvoUploadDenseExtGrid2D, MultipleCells_WithNegativeCoords) {
    dense_extendible_grid<2, int> grid;
    grid.set({-2, -1}, 10);
    grid.set({0, 1}, 20);
    grid.set({3, -3}, 30);

    SvoBuffer buf = svo_upload(grid).value();
    EXPECT_EQ(buf.header().data_count, 3u);

    std::vector<std::array<int32_t, 2>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        EXPECT_EQ(svo_cpu_lookup(buf, {p[0], p[1]}), static_cast<int>(i))
            << "Position (" << p[0] << "," << p[1] << ") expected index " << i;
    }
    EXPECT_EQ(svo_cpu_lookup(buf, {0, 0}), -1);
}

TEST(SvoUploadDenseExtGrid2D, IndexMatchesIterPosOrder) {
    dense_extendible_grid<2, int> grid;
    grid.set({-5, 2}, 0);
    grid.set({0, 0}, 1);
    grid.set({4, -1}, 2);
    grid.set({-1, -4}, 3);

    SvoBuffer buf = svo_upload(grid).value();

    std::vector<std::array<int32_t, 2>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        EXPECT_EQ(svo_cpu_lookup(buf, {p[0], p[1]}), static_cast<int>(i));
    }
}

TEST(SvoUploadDenseExtGrid3D, BasicLookup) {
    dense_extendible_grid<3, int> grid;
    grid.set({0, 0, 0}, 1);
    grid.set({-1, 2, -3}, 2);
    grid.set({5, -2, 1}, 3);

    SvoBuffer buf = svo_upload(grid).value();
    EXPECT_EQ(buf.header().data_count, 3u);
    EXPECT_EQ(buf.header().dim, 3u);

    std::vector<std::array<int32_t, 3>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        EXPECT_EQ(svo_cpu_lookup(buf, {p[0], p[1], p[2]}), static_cast<int>(i));
    }
    EXPECT_EQ(svo_cpu_lookup(buf, {0, 1, 0}), -1);
}

// ============================================================
// svo_cpu_lookup64 - mirrors GPU traversal for SvoBuffer64
// ============================================================

/// Walk the flat SvoBuffer64 and return the DATA INDEX at position `pos`
/// (absolute coordinates), or -1 if absent.
static int64_t svo_cpu_lookup64(const SvoBuffer64& buf, const std::vector<int64_t>& pos, uint64_t child_count = 2u) {
    const auto& w = buf.words;
    if (w.size() < 2u) return -1;

    SvoHeader64 hdr = buf.header();
    uint64_t dim    = hdr.dim;
    uint64_t depth  = hdr.depth;
    uint64_t cpn    = hdr.child_per_node;

    if (depth == 0u || hdr.data_count == 0u) return -1;

    // Read origin (stored as bit-cast uint64; reinterpret as int64 for signed coords)
    std::vector<int64_t> origin(static_cast<std::size_t>(dim));
    for (uint64_t i = 0; i < dim; ++i)
        origin[static_cast<std::size_t>(i)] = static_cast<int64_t>(w[2u + static_cast<std::size_t>(i)]);

    // Coverage at root
    uint64_t coverage = 1u;
    for (uint64_t i = 0; i < depth; ++i) coverage *= child_count;

    // Relative coordinates
    std::vector<uint64_t> rel(static_cast<std::size_t>(dim));
    for (uint64_t axis = 0; axis < dim; ++axis) {
        int64_t delta = pos[static_cast<std::size_t>(axis)] - origin[static_cast<std::size_t>(axis)];
        if (delta < 0 || static_cast<uint64_t>(delta) >= coverage) return -1;
        rel[static_cast<std::size_t>(axis)] = static_cast<uint64_t>(delta);
    }

    uint64_t pool_base = 2u + dim;
    uint64_t node_idx  = pool_base;

    for (uint64_t level = 0u; level < depth; ++level) {
        uint64_t stride = 1u;
        for (uint64_t i = 0u; i < depth - level - 1u; ++i) stride *= child_count;

        // Flat child index (row-major, axis 0 MSB)
        uint64_t ci = 0u;
        for (uint64_t axis = 0u; axis < dim; ++axis) {
            uint64_t digit = (rel[static_cast<std::size_t>(axis)] / stride) % child_count;
            ci             = ci * child_count + digit;
        }

        uint64_t word       = w[static_cast<std::size_t>(node_idx)];
        uint64_t valid_mask = word & ((uint64_t{1} << cpn) - 1u);
        uint64_t leaf_mask  = (word >> cpn) & ((uint64_t{1} << cpn) - 1u);

        if (!((valid_mask >> ci) & 1u)) return -1;

        uint64_t pbc      = static_cast<uint64_t>(std::popcount(valid_mask & ((uint64_t{1} << ci) - 1u)));
        uint64_t slot_pos = node_idx + 1u + pbc;

        if ((leaf_mask >> ci) & 1u) return static_cast<int64_t>(w[static_cast<std::size_t>(slot_pos)]);

        node_idx = w[static_cast<std::size_t>(slot_pos)];
    }

    return -1;
}

// ============================================================
// SvoBuffer64 - tree_extendible_grid 2D
// ============================================================

TEST(SvoUpload64_2D, EmptyGrid) {
    tree_extendible_grid<2, int> grid;
    SvoBuffer64 buf = svo_upload64(grid).value();

    ASSERT_GE(buf.size(), 2u);
    SvoHeader64 hdr = buf.header();
    EXPECT_EQ(hdr.dim, 2u);
    EXPECT_EQ(hdr.data_count, 0u);
    EXPECT_EQ(hdr.depth, 0u);
    EXPECT_EQ(buf.byte_size(), buf.size() * sizeof(uint64_t));
}

TEST(SvoUpload64_2D, SingleCell) {
    tree_extendible_grid<2, int> grid;
    grid.set({3, 5}, 42);

    SvoBuffer64 buf = svo_upload64(grid).value();
    SvoHeader64 hdr = buf.header();

    EXPECT_EQ(hdr.dim, 2u);
    EXPECT_EQ(hdr.data_count, 1u);
    EXPECT_GT(hdr.depth, 0u);
    EXPECT_EQ(hdr.child_per_node, 4u);

    EXPECT_EQ(svo_cpu_lookup64(buf, {3, 5}), 0);
    EXPECT_EQ(svo_cpu_lookup64(buf, {0, 0}), -1);
    EXPECT_EQ(svo_cpu_lookup64(buf, {3, 4}), -1);
}

TEST(SvoUpload64_2D, MultipleCells) {
    tree_extendible_grid<2, int> grid;
    grid.set({0, 0}, 10);
    grid.set({1, 0}, 20);
    grid.set({0, 1}, 30);
    grid.set({3, 2}, 40);

    SvoBuffer64 buf = svo_upload64(grid).value();
    EXPECT_EQ(buf.header().data_count, 4u);

    EXPECT_EQ(svo_cpu_lookup64(buf, {0, 0}), 0);
    EXPECT_EQ(svo_cpu_lookup64(buf, {1, 0}), 1);
    EXPECT_EQ(svo_cpu_lookup64(buf, {0, 1}), 2);
    EXPECT_EQ(svo_cpu_lookup64(buf, {3, 2}), 3);
    EXPECT_EQ(svo_cpu_lookup64(buf, {1, 1}), -1);
}

TEST(SvoUpload64_2D, IndexMatchesIterPosOrder) {
    tree_extendible_grid<2, int> grid;
    grid.set({-1, 2}, 0);
    grid.set({0, 0}, 1);
    grid.set({2, 3}, 2);
    grid.set({-3, -3}, 3);

    SvoBuffer64 buf = svo_upload64(grid).value();

    std::vector<std::array<int32_t, 2>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        int64_t idx = svo_cpu_lookup64(buf, {positions[i][0], positions[i][1]});
        EXPECT_EQ(idx, static_cast<int64_t>(i));
    }
}

// ============================================================
// SvoBuffer64 - tree_extendible_grid 3D
// ============================================================

TEST(SvoUpload64_3D, SingleCell) {
    tree_extendible_grid<3, int> grid;
    grid.set({1, 2, 3}, 99);

    SvoBuffer64 buf = svo_upload64(grid).value();
    SvoHeader64 hdr = buf.header();

    EXPECT_EQ(hdr.dim, 3u);
    EXPECT_EQ(hdr.data_count, 1u);
    EXPECT_EQ(hdr.child_per_node, 8u);  // 2^3

    EXPECT_EQ(svo_cpu_lookup64(buf, {1, 2, 3}), 0);
    EXPECT_EQ(svo_cpu_lookup64(buf, {1, 2, 4}), -1);
}

TEST(SvoUpload64_3D, IndexMatchesIterPosOrder) {
    tree_extendible_grid<3, int> grid;
    grid.set({0, 0, 0}, 1);
    grid.set({1, 1, 1}, 2);
    grid.set({0, 1, 0}, 3);
    grid.set({5, 3, 2}, 4);

    SvoBuffer64 buf = svo_upload64(grid).value();
    EXPECT_EQ(buf.header().data_count, 4u);

    std::vector<std::array<int32_t, 3>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        EXPECT_EQ(svo_cpu_lookup64(buf, {p[0], p[1], p[2]}), static_cast<int64_t>(i));
    }
}

// ============================================================
// SvoBuffer64 - dense_grid 2D (flat grid)
// ============================================================

TEST(SvoUpload64_DenseGrid2D, EmptyGrid) {
    dense_grid<2, int> grid({8u, 8u});
    SvoBuffer64 buf = svo_upload64(grid).value();
    EXPECT_EQ(buf.header().data_count, 0u);
    EXPECT_EQ(buf.header().dim, 2u);
}

TEST(SvoUpload64_DenseGrid2D, SingleCell) {
    dense_grid<2, int> grid({8u, 8u});
    grid.set({3u, 5u}, 42);

    SvoBuffer64 buf = svo_upload64(grid).value();
    SvoHeader64 hdr = buf.header();

    EXPECT_EQ(hdr.data_count, 1u);
    EXPECT_EQ(hdr.child_per_node, 4u);
    EXPECT_EQ(svo_cpu_lookup64(buf, {3, 5}), 0);
    EXPECT_EQ(svo_cpu_lookup64(buf, {0, 0}), -1);
}

TEST(SvoUpload64_DenseGrid2D, MultipleCells) {
    dense_grid<2, int> grid({8u, 8u});
    grid.set({0u, 0u}, 10);
    grid.set({3u, 5u}, 20);
    grid.set({7u, 7u}, 30);

    SvoBuffer64 buf = svo_upload64(grid).value();
    EXPECT_EQ(buf.header().data_count, 3u);

    std::vector<std::array<uint32_t, 2>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        auto& p = positions[i];
        EXPECT_EQ(svo_cpu_lookup64(buf, {static_cast<int64_t>(p[0]), static_cast<int64_t>(p[1])}),
                  static_cast<int64_t>(i));
    }
}

// ============================================================
// SvoBuffer64 header integrity
// ============================================================

TEST(SvoHeader64, FieldEncoding) {
    tree_extendible_grid<2, int> grid;
    grid.set({0, 0}, 1);
    grid.set({1, 1}, 2);

    SvoBuffer64 buf = svo_upload64(grid).value();
    SvoHeader64 hdr = buf.header();

    EXPECT_EQ(hdr.dim, 2u);
    EXPECT_EQ(hdr.child_per_node, 4u);
    EXPECT_GT(hdr.depth, 0u);
    EXPECT_EQ(hdr.data_count, 2u);

    ASSERT_NE(buf.data(), nullptr);
    EXPECT_GT(buf.byte_size(), 0u);
    EXPECT_EQ(buf.byte_size(), buf.size() * sizeof(uint64_t));
}

// ============================================================
// SvoBuffer64 - 1D tree
// ============================================================

TEST(SvoUpload64_1D, BasicLookup) {
    tree_extendible_grid<1, int> grid;
    grid.set({0}, 10);
    grid.set({3}, 20);
    grid.set({7}, 30);

    SvoBuffer64 buf = svo_upload64(grid).value();
    SvoHeader64 hdr = buf.header();

    EXPECT_EQ(hdr.dim, 1u);
    EXPECT_EQ(hdr.data_count, 3u);
    EXPECT_EQ(hdr.child_per_node, 2u);

    std::vector<std::array<int32_t, 1>> positions;
    for (const auto& pos : grid.iter_pos()) positions.push_back(pos);

    for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
        EXPECT_EQ(svo_cpu_lookup64(buf, {positions[i][0]}), static_cast<int64_t>(i));
    }
}

// ============================================================
// SvoUploadError64 - invalid child_count
// ============================================================

TEST(SvoUploadError64Test, InvalidChildCount) {
    tree_extendible_grid<2, int> grid;
    grid.set({0, 0}, 1);

    auto result = svo_upload64(grid, SvoConfig64{.child_count = 99});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().is<SvoUploadError64::InvalidChildCount>());
    const auto* e = result.error().get<SvoUploadError64::InvalidChildCount>();
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->provided, 99u);
    EXPECT_EQ(e->dim, 2u);
}
