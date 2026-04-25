#ifndef EPIX_IMPORT_STD
#include <cstdint>
#include <print>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.extension.grid;
import epix.extension.grid_gpu;

using namespace epix::ext::grid;
using namespace epix::ext::grid_gpu;
using std::int32_t;

int main() {
    // Build a small 3D scene using tree_extendible_grid
    tree_extendible_grid<3, int32_t> grid;

    // Place a 4x4x4 solid cube at origin
    for (int32_t x = 0; x < 4; ++x)
        for (int32_t y = 0; y < 4; ++y)
            for (int32_t z = 0; z < 4; ++z) grid.set({x, y, z});

    // Place a few scattered voxels
    grid.set({10, 10, 10});
    grid.set({-5, -5, -5});

    // Upload to BrickmapBuffer with brick_size = 4
    auto result = brickmap_upload(grid, {.brick_size = 4});
    if (!result) {
        std::println("Upload failed: {}", result.error().message());
        return 1;
    }

    auto hdr = result->header();
    std::println("BrickmapBuffer built successfully:");
    std::println("  dim            = {}", hdr.dim);
    std::println("  brick_size     = {}", hdr.brick_size);
    std::println("  data_count     = {}", hdr.data_count);
    std::println("  brick_count    = {}", hdr.brick_count);
    std::println("  words_per_brick= {}", hdr.words_per_brick);
    std::println("  buffer words   = {}", result->size());
    std::println("  buffer bytes   = {}", result->byte_size());

    // Verify a few lookups via the Slang shader source availability
    std::println("\nSlang shader source available: {} chars", kBrickmapGridSlangSource.size());

    std::println("\nAll done.");
    return 0;
}
