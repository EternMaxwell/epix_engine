#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <bit>
#include <cstdint>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.extension.grid;
import epix.extension.grid_gpu;

using namespace epix::ext::grid;
using namespace epix::ext::grid_gpu;

// ============================================================
// CPU lookup helper — mirrors GPU BrickmapGrid.probe()
// ============================================================
static int brickmap_cpu_lookup(const BrickmapBuffer& buf, const std::vector<int32_t>& pos, uint32_t brick_size = 8u) {
    const auto& w = buf.words;
    if (w.size() < 2) return -1;
    uint32_t h   = w[0];
    uint32_t dim = h & 0xFFu;
    uint32_t bs  = (h >> 8u) & 0xFFu;
    if (dim == 0 || dim != pos.size() || bs != brick_size) return -1;
    uint32_t data_count = w[1];
    if (data_count == 0) return -1;

    std::vector<uint32_t> extent(dim);
    std::vector<int32_t> origin(dim);
    for (uint32_t a = 0; a < dim; ++a) extent[a] = w[2 + a];
    for (uint32_t a = 0; a < dim; ++a) origin[a] = static_cast<int32_t>(w[2 + dim + a]);
    uint32_t brick_count     = w[2 + 2 * dim];
    uint32_t words_per_brick = w[2 + 2 * dim + 1];

    // Compute grid total
    uint32_t grid_total = 1;
    for (uint32_t a = 0; a < dim; ++a) grid_total *= extent[a];
    uint32_t grid_base = 2 + 2 * dim + 2;
    uint32_t pool_base = grid_base + grid_total;
    uint32_t occ_words = words_per_brick - 1;

    // Compute brick coord and local coord
    std::vector<uint32_t> bp(dim), local(dim);
    for (uint32_t a = 0; a < dim; ++a) {
        int32_t rel = pos[a] - origin[a];
        if (rel < 0 || static_cast<uint32_t>(rel) >= extent[a] * bs) return -1;
        bp[a]    = static_cast<uint32_t>(rel) / bs;
        local[a] = static_cast<uint32_t>(rel) % bs;
    }

    // Grid index (axis 0 fastest)
    uint32_t gi = 0, stride = 1;
    for (uint32_t a = 0; a < dim; ++a) {
        gi += bp[a] * stride;
        stride *= extent[a];
    }

    uint32_t bref = w[grid_base + gi];
    if (bref == 0) return -1;  // empty brick
    uint32_t bpi = bref - 1;

    // Flat local bit
    uint32_t flat = 0, s = 1;
    for (uint32_t a = 0; a < dim; ++a) {
        flat += local[a] * s;
        s *= bs;
    }

    // Test occupancy bit
    uint32_t occ_base = pool_base + bpi * words_per_brick + 1;
    uint32_t bit_word = flat / 32u, bit_pos = flat % 32u;
    if (!((w[occ_base + bit_word] >> bit_pos) & 1u)) return -1;

    // Popcount below
    uint32_t below = 0;
    for (uint32_t ww = 0; ww < bit_word; ++ww) below += static_cast<uint32_t>(std::popcount(w[occ_base + ww]));
    if (bit_pos > 0) below += static_cast<uint32_t>(std::popcount(w[occ_base + bit_word] & ((1u << bit_pos) - 1u)));

    uint32_t base_data_idx = w[pool_base + bpi * words_per_brick];
    uint32_t packed_idx = base_data_idx + below;

    // Resolve through data_index_map (appended after brick pool)
    uint32_t map_base = pool_base + brick_count * words_per_brick;
    if (map_base + packed_idx >= w.size()) return -1;
    return static_cast<int>(w[map_base + packed_idx]);
}

// ============================================================
// 2-D tree_extendible_grid tests (brick_size = 4)
// ============================================================

TEST(BrickmapUpload2D, EmptyGrid) {
    tree_extendible_grid<2, int32_t> grid;
    auto result = brickmap_upload(grid, {.brick_size = 4});
    ASSERT_TRUE(result.has_value());
    auto hdr = result->header();
    EXPECT_EQ(hdr.dim, 2u);
    EXPECT_EQ(hdr.brick_size, 4u);
    EXPECT_EQ(hdr.data_count, 0u);
    EXPECT_EQ(hdr.brick_count, 0u);
}

TEST(BrickmapUpload2D, SingleCell) {
    tree_extendible_grid<2, int32_t> grid;
    grid.set({3, 5});
    auto result = brickmap_upload(grid, {.brick_size = 4});
    ASSERT_TRUE(result.has_value());
    auto hdr = result->header();
    EXPECT_EQ(hdr.dim, 2u);
    EXPECT_EQ(hdr.data_count, 1u);
    EXPECT_EQ(hdr.brick_count, 1u);
    int idx = brickmap_cpu_lookup(*result, {3, 5}, 4);
    EXPECT_EQ(idx, 0);
    // Absent cell
    EXPECT_EQ(brickmap_cpu_lookup(*result, {0, 0}, 4), -1);
}

TEST(BrickmapUpload2D, MultipleCells) {
    tree_extendible_grid<2, int32_t> grid;
    grid.set({0, 0});
    grid.set({1, 0});
    grid.set({0, 1});
    grid.set({5, 5});
    auto result = brickmap_upload(grid, {.brick_size = 4});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().data_count, 4u);

    // All occupied cells must be present (non-negative)
    EXPECT_GE(brickmap_cpu_lookup(*result, {0, 0}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {1, 0}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {0, 1}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {5, 5}, 4), 0);
    // Absent
    EXPECT_EQ(brickmap_cpu_lookup(*result, {2, 2}, 4), -1);
}

TEST(BrickmapUpload2D, DataIndexMatchesIterOrdinal) {
    tree_extendible_grid<2, int32_t> grid;
    grid.set({0, 0});
    grid.set({1, 0});
    grid.set({0, 1});
    grid.set({3, 3});
    auto result = brickmap_upload(grid, {.brick_size = 4});
    ASSERT_TRUE(result.has_value());

    uint32_t ordinal = 0;
    for (const auto& pos : grid.iter_pos()) {
        int idx = brickmap_cpu_lookup(*result, {pos[0], pos[1]}, 4);
        ASSERT_GE(idx, 0) << "pos=(" << pos[0] << "," << pos[1] << ")";
        EXPECT_EQ(static_cast<uint32_t>(idx), ordinal)
            << "pos=(" << pos[0] << "," << pos[1] << ") idx=" << idx << " ordinal=" << ordinal;
        ++ordinal;
    }
}

TEST(BrickmapUpload2D, NegativeCoords) {
    tree_extendible_grid<2, int32_t> grid;
    grid.set({-3, -5});
    grid.set({-2, -4});
    grid.set({1, 2});
    auto result = brickmap_upload(grid, {.brick_size = 4});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().data_count, 3u);
    EXPECT_GE(brickmap_cpu_lookup(*result, {-3, -5}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {-2, -4}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {1, 2}, 4), 0);
    EXPECT_EQ(brickmap_cpu_lookup(*result, {0, 0}, 4), -1);
}

// ============================================================
// 1-D tests
// ============================================================

TEST(BrickmapUpload1D, BasicLookup) {
    tree_extendible_grid<1, int32_t> grid;
    grid.set({0});
    grid.set({3});
    grid.set({7});
    auto result = brickmap_upload(grid, {.brick_size = 4});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().data_count, 3u);
    EXPECT_GE(brickmap_cpu_lookup(*result, {0}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {3}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {7}, 4), 0);
    EXPECT_EQ(brickmap_cpu_lookup(*result, {1}, 4), -1);
}

// ============================================================
// 3-D tests
// ============================================================

TEST(BrickmapUpload3D, BasicLookup) {
    tree_extendible_grid<3, int32_t> grid;
    grid.set({0, 0, 0});
    grid.set({1, 2, 3});
    grid.set({7, 7, 7});
    auto result = brickmap_upload(grid, {.brick_size = 8});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().data_count, 3u);
    EXPECT_GE(brickmap_cpu_lookup(*result, {0, 0, 0}, 8), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {1, 2, 3}, 8), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {7, 7, 7}, 8), 0);
    EXPECT_EQ(brickmap_cpu_lookup(*result, {4, 4, 4}, 8), -1);
}

TEST(BrickmapUpload3D, DataIndexMatchesIterOrdinal) {
    tree_extendible_grid<3, int32_t> grid;
    grid.set({0, 0, 0});
    grid.set({1, 0, 0});
    grid.set({0, 1, 0});
    grid.set({0, 0, 1});
    auto result = brickmap_upload(grid, {.brick_size = 4});
    ASSERT_TRUE(result.has_value());

    uint32_t ordinal = 0;
    for (const auto& pos : grid.iter_pos()) {
        int idx = brickmap_cpu_lookup(*result, {pos[0], pos[1], pos[2]}, 4);
        ASSERT_GE(idx, 0);
        EXPECT_EQ(static_cast<uint32_t>(idx), ordinal);
        ++ordinal;
    }
}

// ============================================================
// Different grid types
// ============================================================

TEST(BrickmapUploadDenseGrid2D, MultipleCells) {
    dense_grid<2, uint32_t> grid({8u, 8u});
    grid.set({0u, 0u});
    grid.set({3u, 4u});
    grid.set({7u, 7u});
    auto result = brickmap_upload(grid, {.brick_size = 4});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().data_count, 3u);
    EXPECT_GE(brickmap_cpu_lookup(*result, {0, 0}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {3, 4}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {7, 7}, 4), 0);
    EXPECT_EQ(brickmap_cpu_lookup(*result, {1, 1}, 4), -1);
}

TEST(BrickmapUploadTreeGrid2D, MultipleCells) {
    tree_grid<2, uint32_t> grid({8u, 8u});
    grid.set({0u, 0u});
    grid.set({2u, 3u});
    auto result = brickmap_upload(grid, {.brick_size = 4});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().data_count, 2u);
    EXPECT_GE(brickmap_cpu_lookup(*result, {0, 0}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {2, 3}, 4), 0);
}

TEST(BrickmapUploadSparseGrid2D, InsertRemoveInsert) {
    sparse_grid<2, uint32_t> grid({8u, 8u});
    grid.set({1u, 1u});
    grid.set({2u, 2u});
    grid.remove({1u, 1u});
    grid.set({3u, 3u});
    auto result = brickmap_upload(grid, {.brick_size = 4});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().data_count, 2u);
    EXPECT_GE(brickmap_cpu_lookup(*result, {2, 2}, 4), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {3, 3}, 4), 0);
    EXPECT_EQ(brickmap_cpu_lookup(*result, {1, 1}, 4), -1);
}

// ============================================================
// Different brick sizes
// ============================================================

TEST(BrickmapUploadBrickSize2, Basic2D) {
    tree_extendible_grid<2, int32_t> grid;
    grid.set({0, 0});
    grid.set({1, 1});
    grid.set({3, 3});
    auto result = brickmap_upload(grid, {.brick_size = 2});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().brick_size, 2u);
    EXPECT_EQ(result->header().data_count, 3u);
    EXPECT_GE(brickmap_cpu_lookup(*result, {0, 0}, 2), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {1, 1}, 2), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {3, 3}, 2), 0);
}

TEST(BrickmapUploadBrickSize16, Basic2D) {
    tree_extendible_grid<2, int32_t> grid;
    grid.set({0, 0});
    grid.set({15, 15});
    grid.set({16, 0});
    auto result = brickmap_upload(grid, {.brick_size = 16});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().brick_size, 16u);
    EXPECT_EQ(result->header().data_count, 3u);
    EXPECT_GE(brickmap_cpu_lookup(*result, {0, 0}, 16), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {15, 15}, 16), 0);
    EXPECT_GE(brickmap_cpu_lookup(*result, {16, 0}, 16), 0);
}

// ============================================================
// Header encoding
// ============================================================

TEST(BrickmapHeader, FieldEncoding) {
    tree_extendible_grid<3, int32_t> grid;
    grid.set({0, 0, 0});
    auto result = brickmap_upload(grid, {.brick_size = 8});
    ASSERT_TRUE(result.has_value());
    auto hdr = result->header();
    EXPECT_EQ(hdr.dim, 3u);
    EXPECT_EQ(hdr.brick_size, 8u);
    EXPECT_EQ(hdr.data_count, 1u);
    EXPECT_EQ(hdr.brick_count, 1u);
    EXPECT_GT(hdr.words_per_brick, 0u);
    EXPECT_GT(result->byte_size(), 0u);
}

// ============================================================
// Error handling
// ============================================================

TEST(BrickmapUploadError, InvalidBrickSize) {
    tree_extendible_grid<2, int32_t> grid;
    grid.set({0, 0});
    auto result = brickmap_upload(grid, {.brick_size = 5});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().is<BrickmapUploadError::InvalidBrickSize>());
}

TEST(BrickmapUploadError, InvalidBrickSizeZero) {
    tree_extendible_grid<2, int32_t> grid;
    grid.set({0, 0});
    auto result = brickmap_upload(grid, {.brick_size = 0});
    ASSERT_FALSE(result.has_value());
}