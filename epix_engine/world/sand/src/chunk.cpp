#include "epix/world/sand.h"

using namespace epix::world::sand;

EPIX_API Chunk::Chunk(int width, int height)
    : grid({width, height}), width(width), height(height) {}
EPIX_API Chunk::Chunk(const Chunk& other)
    : grid(other.grid), width(other.width), height(other.height) {}
EPIX_API Chunk::Chunk(Chunk&& other)
    : grid(std::move(other.grid)), width(other.width), height(other.height) {}
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
    _updated = false;
    for (auto& each : grid.data()) {
        each.set_updated(false);
    }
}
EPIX_API Particle& Chunk::get(int x, int y) {
    assert(x >= 0 && x < width && y >= 0 && y < height);
    return grid.get(x, y);
}
EPIX_API const Particle& Chunk::get(int x, int y) const {
    assert(x >= 0 && x < width && y >= 0 && y < height);
    return grid.get(x, y);
}
EPIX_API void Chunk::insert(int x, int y, const Particle& cell) {
    assert(x >= 0 && x < width && y >= 0 && y < height);
    grid.emplace(x, y, cell);
    _updated = true;
}
EPIX_API void Chunk::insert(int x, int y, Particle&& cell) {
    assert(x >= 0 && x < width && y >= 0 && y < height);
    grid.emplace(x, y, std::move(cell));
    _updated = true;
}
EPIX_API void Chunk::remove(int x, int y) {
    assert(x >= 0 && x < width && y >= 0 && y < height);
    grid.remove(x, y);
    _updated = true;
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

EPIX_API Chunk::grid_t::view_t Chunk::view() { return grid.view(); }
EPIX_API Chunk::grid_t::const_view_t Chunk::view() const { return grid.view(); }

EPIX_API bool Chunk::updated() const { return _updated; }
EPIX_API size_t Chunk::count() const { return grid.count(); }
