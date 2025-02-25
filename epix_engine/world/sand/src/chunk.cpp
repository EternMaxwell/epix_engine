#include "epix/world/sand.h"

#define EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME 12i32

using namespace epix::world::sand;

EPIX_API Chunk::Chunk(int width, int height)
    : grid({width, height}),
      width(width),
      height(height),
      time_since_last_swap(0),
      time_threshold(EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME),
      updating_area{width, 0, height, 0},
      updating_area_next{width, 0, height, 0} {}
EPIX_API Chunk::Chunk(const Chunk& other)
    : grid(other.grid),
      width(other.width),
      height(other.height),
      time_since_last_swap(0),
      time_threshold(EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME),
      updating_area{width, 0, height, 0},
      updating_area_next{width, 0, height, 0} {}
EPIX_API Chunk::Chunk(Chunk&& other)
    : grid(std::move(other.grid)),
      width(other.width),
      height(other.height),
      time_since_last_swap(0),
      time_threshold(EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME),
      updating_area{width, 0, height, 0},
      updating_area_next{width, 0, height, 0} {}
EPIX_API Chunk& Chunk::operator=(const Chunk& other) {
    assert(width == other.width && height == other.height);
    grid = other.grid;
    return *this;
}
EPIX_API Chunk& Chunk::operator=(Chunk&& other) {
    assert(width == other.width && height == other.height);
    grid = std::move(other.grid);
    return *this;
}
EPIX_API void Chunk::reset_updated() {
    for (auto& each : grid.data()) {
        each.set_updated(false);
    }
}
EPIX_API void Chunk::step_time() {
    time_since_last_swap++;
    if (time_since_last_swap >= time_threshold) {
        time_since_last_swap = 0;
        swap_area();
    }
    time_threshold = EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME;
}
EPIX_API Particle& Chunk::get(int x, int y) { return grid.get(x, y); }
EPIX_API const Particle& Chunk::get(int x, int y) const {
    return grid.get(x, y);
}
EPIX_API void Chunk::insert(int x, int y, const Particle& cell) {
    grid.emplace(x, y, cell);
}
EPIX_API void Chunk::insert(int x, int y, Particle&& cell) {
    grid.emplace(x, y, std::move(cell));
}
EPIX_API void Chunk::swap_area() {
    updating_area[0]      = updating_area_next[0];
    updating_area[1]      = updating_area_next[1];
    updating_area[2]      = updating_area_next[2];
    updating_area[3]      = updating_area_next[3];
    updating_area_next[0] = width;
    updating_area_next[1] = 0;
    updating_area_next[2] = height;
    updating_area_next[3] = 0;
    if (!should_update()) {
        for (auto& each : grid.data()) {
            each.set_freefall(false);
            each.velocity = {0.0f, 0.0f};
        }
    }
}
EPIX_API void Chunk::remove(int x, int y) {
    assert(x >= 0 && x < width && y >= 0 && y < height);
    grid.remove(x, y);
}
EPIX_API void Chunk::touch(int x, int y) {
    assert(x >= 0 && x < width && y >= 0 && y < height);
    if (!should_update()) {
        updating_area[0] = 0;
        updating_area[1] = width - 1;
        updating_area[2] = 0;
        updating_area[3] = height - 1;
    }
    if (x < updating_area[0]) updating_area[0] = x;
    if (x > updating_area[1]) updating_area[1] = x;
    if (y < updating_area[2]) updating_area[2] = y;
    if (y > updating_area[3]) updating_area[3] = y;
    if (x < updating_area_next[0]) updating_area_next[0] = x;
    if (x > updating_area_next[1]) updating_area_next[1] = x;
    if (y < updating_area_next[2]) updating_area_next[2] = y;
    if (y > updating_area_next[3]) updating_area_next[3] = y;
}
EPIX_API bool Chunk::should_update() const {
    return updating_area[0] <= updating_area[1] &&
           updating_area[2] <= updating_area[3];
}
EPIX_API int Chunk::size(int dim) const {
    if (dim == 0) {
        return width;
    } else if (dim == 1) {
        return height;
    } else {
        return 0;
    }
}
EPIX_API bool Chunk::contains(int x, int y) const {
    return grid.contains(x, y);
}
