#include "epix/world/sand.h"

using namespace epix::world::sand;

EPIX_API World_T::World_T(const Registry_T* registry, int chunk_size)
    : m_chunk_size(chunk_size), m_registry(registry) {
    m_thread_pool = std::make_unique<BS::thread_pool<BS::tp::none>>(
        std::thread::hardware_concurrency()
    );
}

EPIX_API World_T* World_T::create(const Registry_T* registry, int chunk_size) {
    return new World_T(registry, chunk_size);
}
EPIX_API std::unique_ptr<World_T> World_T::create_unique(
    const Registry_T* registry, int chunk_size
) {
    return std::make_unique<World_T>(registry, chunk_size);
}
EPIX_API std::shared_ptr<World_T> World_T::create_shared(
    const Registry_T* registry, int chunk_size
) {
    return std::make_shared<World_T>(registry, chunk_size);
}

EPIX_API int World_T::chunk_size() const { return m_chunk_size; }
EPIX_API const Registry_T& World_T::registry() const { return *m_registry; }

EPIX_API std::pair<int, int> World_T::to_chunk_pos(int x, int y) const {
    return {
        x < 0 ? (x + 1) / m_chunk_size - 1 : x / m_chunk_size,
        y < 0 ? (y + 1) / m_chunk_size - 1 : y / m_chunk_size
    };
}
EPIX_API std::pair<int, int> World_T::in_chunk_pos(int x, int y) const {
    std::pair<int, int> res = {x % m_chunk_size, y % m_chunk_size};
    if (res.first < 0) res.first += m_chunk_size;
    if (res.second < 0) res.second += m_chunk_size;
    return res;
}
EPIX_API std::pair<std::pair<int, int>, std::pair<int, int>>
World_T::decode_pos(int x, int y) const {
    return {to_chunk_pos(x, y), in_chunk_pos(x, y)};
}

EPIX_API void World_T::insert_chunk(int x, int y, Chunk&& chunk) {
    m_chunks.emplace(x, y, std::move(chunk));
}
EPIX_API void World_T::insert_chunk(int x, int y) {
    m_chunks.emplace(x, y, Chunk(m_chunk_size, m_chunk_size));
}
EPIX_API void World_T::remove_chunk(int x, int y) { m_chunks.remove(x, y); }
EPIX_API void World_T::shrink_to_fit() { m_chunks.shrink(); }

EPIX_API glm::vec2 World_T::gravity_at(int x, int y) const {
    return {0.0f, -98.0f};
}
EPIX_API glm::vec2 World_T::default_velocity_at(int x, int y) const {
    return {0.0f, 0.0f};
}
EPIX_API float World_T::air_density_at(int x, int y) const { return 0.001225f; }

EPIX_API int World_T::not_moving_threshold(const glm::vec2& grav) const {
    // the larger the gravity, the smaller the threshold
    auto len = std::sqrt(grav.x * grav.x + grav.y * grav.y);
    if (len == 0) return std::numeric_limits<int>::max();
    return not_moving_threshold_setting.numerator /
           std::pow(len, not_moving_threshold_setting.power);
}

EPIX_API bool World_T::valid(int x, int y) const {
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    return m_chunks.contains(chunk_x, chunk_y);
}
EPIX_API bool World_T::contains(int x, int y) const {
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    return m_chunks.contains(chunk_x, chunk_y) &&
           m_chunks.get(chunk_x, chunk_y).contains(cell_x, cell_y);
}

EPIX_API Particle& World_T::particle_at(int x, int y) {
    assert(valid(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    return m_chunks.get(chunk_x, chunk_y).get(cell_x, cell_y);
}
EPIX_API const Particle& World_T::particle_at(int x, int y) const {
    assert(valid(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    return m_chunks.get(chunk_x, chunk_y).get(cell_x, cell_y);
}
EPIX_API const Element& World_T::elem_at(int x, int y) const {
    return m_registry->get_elem(particle_at(x, y).elem_id);
}
EPIX_API std::tuple<Particle&, const Element&> World_T::get(int x, int y) {
    assert(contains(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    Particle& cell      = m_chunks.get(chunk_x, chunk_y).get(cell_x, cell_y);
    const Element& elem = m_registry->get_elem(cell.elem_id);
    return {cell, elem};
}
EPIX_API std::tuple<const Particle&, const Element&> World_T::get(int x, int y)
    const {
    assert(contains(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    const Particle& cell = m_chunks.get(chunk_x, chunk_y).get(cell_x, cell_y);
    const Element& elem  = m_registry->get_elem(cell.elem_id);
    return {cell, elem};
}

EPIX_API void World_T::touch(int x, int y) {
    if (!valid(x, y)) return;
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    m_chunks.get(chunk_x, chunk_y).touch(cell_x, cell_y);
}

EPIX_API void World_T::swap(int x, int y, int tx, int ty) {
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
EPIX_API void World_T::insert(int x, int y, Particle&& cell) {
    assert(valid(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    auto& chunk               = m_chunks.get(chunk_x, chunk_y);
    if (chunk.contains(cell_x, cell_y)) return;
    chunk.insert(cell_x, cell_y, std::move(cell));
    touch(x - 1, y);
    touch(x + 1, y);
    touch(x, y - 1);
    touch(x, y + 1);
}
EPIX_API void World_T::remove(int x, int y) {
    assert(valid(x, y));
    auto&& [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = in_chunk_pos(x, y);
    m_chunks.get(chunk_x, chunk_y).remove(cell_x, cell_y);
    touch(x - 1, y);
    touch(x + 1, y);
    touch(x, y - 1);
    touch(x, y + 1);
}

EPIX_API World_T::grid_t::view_t World_T::view() { return m_chunks.view(); }
EPIX_API World_T::grid_t::const_view_t World_T::view() const {
    return m_chunks.view();
}