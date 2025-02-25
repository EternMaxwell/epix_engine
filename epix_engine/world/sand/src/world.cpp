#include "epix/world/sand.h"

using namespace epix::world::sand;

EPIX_API World::World(const Registry* registry, int chunk_size)
    : m_chunk_size(chunk_size), m_registry(registry) {
    m_thread_pool = std::make_unique<BS::thread_pool<BS::tp::none>>(
        std::thread::hardware_concurrency()
    );
}

EPIX_API World* World::create(const Registry* registry, int chunk_size) {
    return new World(registry, chunk_size);
}
EPIX_API std::unique_ptr<World> World::create_unique(
    const Registry* registry, int chunk_size
) {
    return std::make_unique<World>(registry, chunk_size);
}
EPIX_API std::shared_ptr<World> World::create_shared(
    const Registry* registry, int chunk_size
) {
    return std::make_shared<World>(registry, chunk_size);
}

EPIX_API int World::chunk_size() const { return m_chunk_size; }
EPIX_API const Registry& World::registry() const { return *m_registry; }

EPIX_API std::pair<int, int> World::to_chunk_pos(int x, int y) const {
    return {
        x < 0 ? (x + 1) / m_chunk_size - 1 : x / m_chunk_size,
        y < 0 ? (y + 1) / m_chunk_size - 1 : y / m_chunk_size
    };
}
EPIX_API std::pair<int, int> World::in_chunk_pos(int x, int y) const {
    std::pair<int, int> res = {x % m_chunk_size, y % m_chunk_size};
    if (res.first < 0) res.first += m_chunk_size;
    if (res.second < 0) res.second += m_chunk_size;
    return res;
}
EPIX_API std::pair<std::pair<int, int>, std::pair<int, int>> World::decode_pos(
    int x, int y
) const {
    return {to_chunk_pos(x, y), in_chunk_pos(x, y)};
}

EPIX_API void World::insert_chunk(int x, int y, Chunk&& chunk) {
    m_chunks.emplace(x, y, std::move(chunk));
}
EPIX_API void World::insert_chunk(int x, int y) {
    m_chunks.emplace(x, y, Chunk(m_chunk_size, m_chunk_size));
}
EPIX_API void World::remove_chunk(int x, int y) { m_chunks.remove(x, y); }
EPIX_API void World::shrink_to_fit() { m_chunks.shrink(); }

EPIX_API glm::vec2 World::gravity_at(int x, int y) const {
    return {0.0f, -9.8f};
}
EPIX_API glm::vec2 World::default_velocity_at(int x, int y) const {
    return {0.0f, 0.0f};
}
EPIX_API float World::air_density_at(int x, int y) const { return 0.001225f; }

EPIX_API int World::not_moving_threshold(const glm::vec2& grav) const {
    // the larger the gravity, the smaller the threshold
    auto len = std::sqrt(grav.x * grav.x + grav.y * grav.y);
    if (len == 0) return std::numeric_limits<int>::max();
    return not_moving_threshold_setting.numerator /
           std::pow(len, not_moving_threshold_setting.power);
}

EPIX_API bool World::valid(int x, int y) const {
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    return m_chunks.contains(chunk_x, chunk_y);
}
EPIX_API bool World::contains(int x, int y) const {
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    return m_chunks.contains(chunk_x, chunk_y) &&
           m_chunks.get(chunk_x, chunk_y).contains(cell_x, cell_y);
}

EPIX_API Particle& World::particle_at(int x, int y) {
    assert(valid(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    return m_chunks.get(chunk_x, chunk_y).get(cell_x, cell_y);
}
EPIX_API const Particle& World::particle_at(int x, int y) const {
    assert(valid(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    return m_chunks.get(chunk_x, chunk_y).get(cell_x, cell_y);
}
EPIX_API const Element& World::elem_at(int x, int y) const {
    return m_registry->get_elem(particle_at(x, y).elem_id);
}
EPIX_API std::tuple<Particle&, const Element&> World::get(int x, int y) {
    assert(contains(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    Particle& cell      = m_chunks.get(chunk_x, chunk_y).get(cell_x, cell_y);
    const Element& elem = m_registry->get_elem(cell.elem_id);
    return {cell, elem};
}
EPIX_API std::tuple<const Particle&, const Element&> World::get(int x, int y)
    const {
    assert(contains(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    const Particle& cell = m_chunks.get(chunk_x, chunk_y).get(cell_x, cell_y);
    const Element& elem  = m_registry->get_elem(cell.elem_id);
    return {cell, elem};
}

EPIX_API void World::swap(int x, int y, int tx, int ty) {
    assert(valid(x, y) && valid(tx, ty));
    auto&& [chunk_x, chunk_y]   = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]     = in_chunk_pos(x, y);
    auto&& [tchunk_x, tchunk_y] = to_chunk_pos(tx, ty);
    auto&& [tcell_x, tcell_y]   = in_chunk_pos(tx, ty);
    auto& chunk                 = m_chunks.get(chunk_x, chunk_y);
    auto& tchunk                = m_chunks.get(tchunk_x, tchunk_y);
    if (!chunk.contains(cell_x, cell_y) && tchunk.contains(tcell_x, tcell_y)) {
        auto& cell = tchunk.get(tcell_x, tcell_y);
        chunk.insert(cell_x, cell_y, std::move(cell));
        tchunk.remove(tcell_x, tcell_y);
        return;
    }
    if (chunk.contains(cell_x, cell_y) && !tchunk.contains(tcell_x, tcell_y)) {
        auto& cell = chunk.get(cell_x, cell_y);
        tchunk.insert(tcell_x, tcell_y, std::move(cell));
        chunk.remove(cell_x, cell_y);
        return;
    }
    if (chunk.contains(cell_x, cell_y) && tchunk.contains(tcell_x, tcell_y)) {
        auto& cell  = chunk.get(cell_x, cell_y);
        auto& tcell = tchunk.get(tcell_x, tcell_y);
        std::swap(cell, tcell);
        return;
    }
}
EPIX_API void World::insert(int x, int y, Particle&& cell) {
    assert(valid(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    m_chunks.get(chunk_x, chunk_y).insert(cell_x, cell_y, std::move(cell));
}
EPIX_API void World::remove(int x, int y) {
    assert(valid(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    m_chunks.get(chunk_x, chunk_y).remove(cell_x, cell_y);
}

EPIX_API World::grid_t::view_t World::view() { return m_chunks.view(); }
EPIX_API World::grid_t::const_view_t World::view() const {
    return m_chunks.view();
}