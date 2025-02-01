#include <BS_thread_pool.hpp>
#include <numbers>
#include <ranges>

#include "epix/world/sand.h"

#define EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME 12i32

using namespace epix::world::sand::components;

EPIX_API CellDef::CellDef(const CellDef& other) : identifier(other.identifier) {
    if (identifier == DefIdentifier::Name) {
        new (&elem_name) std::string(other.elem_name);
    } else {
        elem_id = other.elem_id;
    }
}
EPIX_API CellDef::CellDef(CellDef&& other) : identifier(other.identifier) {
    if (identifier == DefIdentifier::Name) {
        new (&elem_name) std::string(std::move(other.elem_name));
    } else {
        elem_id = other.elem_id;
    }
}
EPIX_API CellDef& CellDef::operator=(const CellDef& other) {
    if (identifier == DefIdentifier::Name) {
        elem_name.~basic_string();
    }
    identifier = other.identifier;
    if (identifier == DefIdentifier::Name) {
        new (&elem_name) std::string(other.elem_name);
    } else {
        elem_id = other.elem_id;
    }
    return *this;
}
EPIX_API CellDef& CellDef::operator=(CellDef&& other) {
    if (identifier == DefIdentifier::Name) {
        elem_name.~basic_string();
    }
    identifier = other.identifier;
    if (identifier == DefIdentifier::Name) {
        new (&elem_name) std::string(std::move(other.elem_name));
    } else {
        elem_id = other.elem_id;
    }
    return *this;
}
EPIX_API CellDef::CellDef(const std::string& name)
    : identifier(DefIdentifier::Name), elem_name(name) {}
EPIX_API CellDef::CellDef(int id)
    : identifier(DefIdentifier::Id), elem_id(id) {}
EPIX_API CellDef::~CellDef() {
    if (identifier == DefIdentifier::Name) {
        elem_name.~basic_string();
    }
}

EPIX_API bool Cell::valid() const { return elem_id >= 0; }
EPIX_API Cell::operator bool() const { return valid(); }
EPIX_API bool Cell::operator!() const { return !valid(); }

EPIX_API Simulation::Chunk::Chunk(int width, int height)
    : cells({width, height}),
      width(width),
      height(height),
      time_since_last_swap(0),
      time_threshold(EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME),
      updating_area{width, 0, height, 0},
      updating_area_next{width, 0, height, 0} {}
EPIX_API Simulation::Chunk::Chunk(const Chunk& other)
    : cells(other.cells),
      width(other.width),
      height(other.height),
      time_since_last_swap(0),
      time_threshold(EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME),
      updating_area{width, 0, height, 0},
      updating_area_next{width, 0, height, 0} {}
EPIX_API Simulation::Chunk::Chunk(Chunk&& other)
    : cells(std::move(other.cells)),
      width(other.width),
      height(other.height),
      time_since_last_swap(0),
      time_threshold(EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME),
      updating_area{width, 0, height, 0},
      updating_area_next{width, 0, height, 0} {}
EPIX_API Simulation::Chunk& Simulation::Chunk::operator=(const Chunk& other) {
    assert(width == other.width && height == other.height);
    cells = other.cells;
    return *this;
}
EPIX_API Simulation::Chunk& Simulation::Chunk::operator=(Chunk&& other) {
    assert(width == other.width && height == other.height);
    cells = std::move(other.cells);
    return *this;
}
EPIX_API void Simulation::Chunk::reset_updated() {
    for (auto& each : cells.data()) {
        each.updated = false;
    }
}
EPIX_API void Simulation::Chunk::count_time() {
    time_since_last_swap++;
    if (time_since_last_swap >= time_threshold) {
        time_since_last_swap = 0;
        swap_area();
    }
    time_threshold = EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME;
}
EPIX_API Cell& Simulation::Chunk::get(int x, int y) { return cells.get(x, y); }
EPIX_API const Cell& Simulation::Chunk::get(int x, int y) const {
    return cells.get(x, y);
}
EPIX_API Cell& Simulation::Chunk::create(
    int x, int y, const CellDef& def, ElemRegistry& m_registry
) {
    if (cells.contains(x, y)) return cells.get(x, y);
    Cell cell;
    cell.elem_id = def.identifier == CellDef::DefIdentifier::Name
                       ? m_registry.elem_id(def.elem_name)
                       : def.elem_id;
    if (cell.elem_id < 0) {
        return cells.get(x, y);
    }
    cell.color = m_registry.get_elem(cell.elem_id).gen_color();
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_real_distribution<float> dis(-0.4f, 0.4f);
    cell.inpos = {dis(gen), dis(gen)};
    cell.velocity =
        def.default_vel + glm::vec2{dis(gen) * 0.1f, dis(gen) * 0.1f};
    if (!m_registry.get_elem(cell.elem_id).is_solid()) {
        cell.freefall = true;
    } else {
        cell.velocity = {0.0f, 0.0f};
        cell.freefall = false;
    }
    cells.emplace(x, y, cell);
    return cells.get(x, y);
}
EPIX_API void Simulation::Chunk::insert(int x, int y, const Cell& cell) {
    cells.emplace(x, y, cell);
}
EPIX_API void Simulation::Chunk::insert(int x, int y, Cell&& cell) {
    cells.emplace(x, y, std::move(cell));
}
EPIX_API void Simulation::Chunk::swap_area() {
    updating_area[0]      = updating_area_next[0];
    updating_area[1]      = updating_area_next[1];
    updating_area[2]      = updating_area_next[2];
    updating_area[3]      = updating_area_next[3];
    updating_area_next[0] = width;
    updating_area_next[1] = 0;
    updating_area_next[2] = height;
    updating_area_next[3] = 0;
    if (!should_update()) {
        for (auto& each : cells.data()) {
            if (each) {
                each.freefall = false;
                each.velocity = {0.0f, 0.0f};
            }
        }
    }
}
EPIX_API bool Simulation::Chunk::in_area(int x, int y) const {
    return x >= updating_area_next[0] && x <= updating_area_next[1] &&
           y >= updating_area_next[2] && y <= updating_area_next[3];
}
EPIX_API void Simulation::Chunk::remove(int x, int y) {
    assert(x >= 0 && x < width && y >= 0 && y < height);
    cells.remove(x, y);
}
EPIX_API bool Simulation::Chunk::is_updated(int x, int y) const {
    return cells.get(x, y).updated;
}
EPIX_API void Simulation::Chunk::touch(int x, int y) {
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
EPIX_API bool Simulation::Chunk::should_update() const {
    return updating_area[0] <= updating_area[1] &&
           updating_area[2] <= updating_area[3];
}
EPIX_API glm::ivec2 Simulation::Chunk::size() const {
    return glm::ivec2(width, height);
}
EPIX_API bool Simulation::Chunk::contains(int x, int y) const {
    return cells.contains(x, y);
}

EPIX_API Simulation::ChunkMap::ChunkMap(int chunk_size)
    : chunk_size(chunk_size) {}

EPIX_API void Simulation::ChunkMap::load_chunk(
    int x, int y, const Chunk& chunk
) {
    chunks.emplace(x, y, chunk);
}
EPIX_API void Simulation::ChunkMap::load_chunk(int x, int y, Chunk&& chunk) {
    chunks.emplace(x, y, std::move(chunk));
}
EPIX_API void Simulation::ChunkMap::load_chunk(int x, int y) {
    chunks.emplace(x, y, Chunk(chunk_size, chunk_size));
}
EPIX_API bool Simulation::ChunkMap::contains(int x, int y) const {
    return chunks.contains(x, y);
}
EPIX_API Simulation::Chunk& Simulation::ChunkMap::get_chunk(int x, int y) {
    return chunks.get(x, y);
}
EPIX_API const Simulation::Chunk& Simulation::ChunkMap::get_chunk(int x, int y)
    const {
    return chunks.get(x, y);
}
EPIX_API size_t Simulation::ChunkMap::chunk_count() const {
    return chunks.count();
}

EPIX_API Simulation::ChunkMap::iterator::iterator(
    ChunkMap* chunks, int x, int y
)
    : chunk_map(chunks), x(x), y(y) {}

std::tuple<int, int, int> chunk_map_xbounds(
    const Simulation::ChunkMap& chunk_map, bool increasing
) {
    auto origin = chunk_map.chunks.origin();
    auto size   = chunk_map.chunks.size();
    return {
        increasing ? origin[0] : origin[0] + size[0] - 1,
        increasing ? origin[0] + size[0] : origin[0] - 1,
        increasing ? 1 : -1,
    };
}

std::tuple<int, int, int> chunk_map_ybounds(
    const Simulation::ChunkMap& chunk_map, bool increasing
) {
    auto origin = chunk_map.chunks.origin();
    auto size   = chunk_map.chunks.size();
    return {
        increasing ? origin[1] : origin[1] + size[1] - 1,
        increasing ? origin[1] + size[1] : origin[1] - 1,
        increasing ? 1 : -1,
    };
}

EPIX_API Simulation::ChunkMap::iterator&
Simulation::ChunkMap::iterator::operator++() {
    auto origin = chunk_map->chunks.origin();
    auto size   = chunk_map->chunks.size();
    if (x == origin[0] + size[0] && y == origin[1] + size[1]) {
        return *this;
    }
    std::tuple<int, int, int> xbounds =
        chunk_map_xbounds(*chunk_map, chunk_map->iterate_setting.xorder);
    std::tuple<int, int, int> ybounds =
        chunk_map_ybounds(*chunk_map, chunk_map->iterate_setting.yorder);
    if (chunk_map->iterate_setting.x_outer) {
        int start_y = y + std::get<2>(ybounds);
        for (int tx = x; tx != std::get<1>(xbounds);
             tx += std::get<2>(xbounds)) {
            for (int ty = start_y; ty != std::get<1>(ybounds);
                 ty += std::get<2>(ybounds)) {
                if (chunk_map->contains(tx, ty)) {
                    x = tx;
                    y = ty;
                    return *this;
                }
            }
            start_y = std::get<0>(ybounds);
        }
    } else {
        int start_x = x + std::get<2>(xbounds);
        for (int ty = y; ty != std::get<1>(ybounds);
             ty += std::get<2>(ybounds)) {
            for (int tx = start_x; tx != std::get<1>(xbounds);
                 tx += std::get<2>(xbounds)) {
                if (chunk_map->contains(tx, ty)) {
                    x = tx;
                    y = ty;
                    return *this;
                }
            }
            start_x = std::get<0>(xbounds);
        }
    }
    x = origin[0] + size[0];
    y = origin[1] + size[1];
    return *this;
}

EPIX_API bool Simulation::ChunkMap::iterator::operator==(const iterator& other
) const {
    return x == other.x && y == other.y;
}

EPIX_API bool Simulation::ChunkMap::iterator::operator!=(const iterator& other
) const {
    return !(x == other.x && y == other.y);
}

EPIX_API std::pair<glm::ivec2, Simulation::Chunk&>
Simulation::ChunkMap::iterator::operator*() {
    return {{x, y}, chunk_map->get_chunk(x, y)};
}

EPIX_API Simulation::ChunkMap::const_iterator::const_iterator(
    const ChunkMap* chunks, int x, int y
)
    : chunk_map(chunks), x(x), y(y) {}

EPIX_API Simulation::ChunkMap::const_iterator&
Simulation::ChunkMap::const_iterator::operator++() {
    auto origin = chunk_map->chunks.origin();
    auto size   = chunk_map->chunks.size();
    if (x == origin[0] + size[0] && y == origin[1] + size[1]) {
        return *this;
    }
    std::tuple<int, int, int> xbounds =
        chunk_map_xbounds(*chunk_map, chunk_map->iterate_setting.xorder);
    std::tuple<int, int, int> ybounds =
        chunk_map_ybounds(*chunk_map, chunk_map->iterate_setting.yorder);
    if (chunk_map->iterate_setting.x_outer) {
        int start_y = y + std::get<2>(ybounds);
        for (int tx = x; tx != std::get<1>(xbounds);
             tx += std::get<2>(xbounds)) {
            for (int ty = start_y; ty != std::get<1>(ybounds);
                 ty += std::get<2>(ybounds)) {
                if (chunk_map->contains(tx, ty)) {
                    x = tx;
                    y = ty;
                    return *this;
                }
            }
            start_y = std::get<0>(ybounds);
        }
    } else {
        int start_x = x + std::get<2>(xbounds);
        for (int ty = y; ty != std::get<1>(ybounds);
             ty += std::get<2>(ybounds)) {
            for (int tx = start_x; tx != std::get<1>(xbounds);
                 tx += std::get<2>(xbounds)) {
                if (chunk_map->contains(tx, ty)) {
                    x = tx;
                    y = ty;
                    return *this;
                }
            }
            start_x = std::get<0>(xbounds);
        }
    }
    x = origin[0] + size[0];
    y = origin[1] + size[1];
    return *this;
}

EPIX_API bool Simulation::ChunkMap::const_iterator::operator==(
    const const_iterator& other
) const {
    return x == other.x && y == other.y;
}

EPIX_API bool Simulation::ChunkMap::const_iterator::operator!=(
    const const_iterator& other
) const {
    return !(x == other.x && y == other.y);
}

EPIX_API std::pair<glm::ivec2, const Simulation::Chunk&>
Simulation::ChunkMap::const_iterator::operator*() {
    return {{x, y}, chunk_map->get_chunk(x, y)};
}

EPIX_API void Simulation::ChunkMap::set_iterate_setting(
    bool xorder, bool yorder, bool x_outer
) {
    iterate_setting.xorder  = xorder;
    iterate_setting.yorder  = yorder;
    iterate_setting.x_outer = x_outer;
}

EPIX_API Simulation::ChunkMap::iterator Simulation::ChunkMap::begin() {
    std::tuple<int, int, int> xbounds =
        chunk_map_xbounds(*this, iterate_setting.xorder);
    std::tuple<int, int, int> ybounds =
        chunk_map_ybounds(*this, iterate_setting.yorder);
    if (iterate_setting.x_outer) {
        for (int tx = std::get<0>(xbounds); tx != std::get<1>(xbounds);
             tx += std::get<2>(xbounds)) {
            for (int ty = std::get<0>(ybounds); ty != std::get<1>(ybounds);
                 ty += std::get<2>(ybounds)) {
                if (contains(tx, ty)) {
                    return iterator(this, tx, ty);
                }
            }
        }
    } else {
        for (int ty = std::get<0>(ybounds); ty != std::get<1>(ybounds);
             ty += std::get<2>(ybounds)) {
            for (int tx = std::get<0>(xbounds); tx != std::get<1>(xbounds);
                 tx += std::get<2>(xbounds)) {
                if (contains(tx, ty)) {
                    return iterator(this, tx, ty);
                }
            }
        }
    }
    return end();
}
EPIX_API Simulation::ChunkMap::const_iterator Simulation::ChunkMap::begin(
) const {
    std::tuple<int, int, int> xbounds =
        chunk_map_xbounds(*this, iterate_setting.xorder);
    std::tuple<int, int, int> ybounds =
        chunk_map_ybounds(*this, iterate_setting.yorder);
    if (iterate_setting.x_outer) {
        for (int tx = std::get<0>(xbounds); tx != std::get<1>(xbounds);
             tx += std::get<2>(xbounds)) {
            for (int ty = std::get<0>(ybounds); ty != std::get<1>(ybounds);
                 ty += std::get<2>(ybounds)) {
                if (contains(tx, ty)) {
                    return const_iterator(this, tx, ty);
                }
            }
        }
    } else {
        for (int ty = std::get<0>(ybounds); ty != std::get<1>(ybounds);
             ty += std::get<2>(ybounds)) {
            for (int tx = std::get<0>(xbounds); tx != std::get<1>(xbounds);
                 tx += std::get<2>(xbounds)) {
                if (contains(tx, ty)) {
                    return const_iterator(this, tx, ty);
                }
            }
        }
    }
    return end();
}
EPIX_API Simulation::ChunkMap::iterator Simulation::ChunkMap::end() {
    auto origin = chunks.origin();
    auto size   = chunks.size();
    return iterator(this, origin[0] + size[0], origin[1] + size[1]);
}
EPIX_API Simulation::ChunkMap::const_iterator Simulation::ChunkMap::end(
) const {
    auto origin = chunks.origin();
    auto size   = chunks.size();
    return const_iterator(this, origin[0] + size[0], origin[1] + size[1]);
}

EPIX_API void Simulation::ChunkMap::reset_updated() {
    for (auto&& [pos, chunk] : *this) {
        chunk.reset_updated();
    }
}
EPIX_API void Simulation::ChunkMap::count_time() {
    for (auto&& [pos, chunk] : *this) {
        chunk.count_time();
    }
}

EPIX_API Cell& Simulation::create_def(int x, int y, const CellDef& def) {
    assert(valid(x, y));
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto [cell_x, cell_y]   = to_in_chunk_pos(x, y);
    touch(x, y);
    touch(x - 1, y);
    touch(x + 1, y);
    touch(x, y - 1);
    touch(x, y + 1);
    return m_chunk_map.get_chunk(chunk_x, chunk_y)
        .create(cell_x, cell_y, def, m_registry);
}
EPIX_API Simulation::Simulation(const ElemRegistry& registry, int chunk_size)
    : m_registry(registry),
      m_chunk_size(chunk_size),
      m_chunk_map{chunk_size},
      max_travel({chunk_size, chunk_size}),
      m_thread_pool(std::make_unique<BS::thread_pool<BS::tp::none>>(
          std::thread::hardware_concurrency()
      )) {}
EPIX_API Simulation::Simulation(ElemRegistry&& registry, int chunk_size)
    : m_registry(std::move(registry)),
      m_chunk_size(chunk_size),
      m_chunk_map{chunk_size},
      max_travel({chunk_size, chunk_size}),
      m_thread_pool(std::make_unique<BS::thread_pool<BS::tp::none>>(
          std::thread::hardware_concurrency()
      )) {}

EPIX_API Simulation::UpdatingState& Simulation::updating_state() {
    return m_updating_state;
}
EPIX_API const Simulation::UpdatingState& Simulation::updating_state() const {
    return m_updating_state;
}

EPIX_API int Simulation::chunk_size() const { return m_chunk_size; }
EPIX_API ElemRegistry& Simulation::registry() { return m_registry; }
EPIX_API const ElemRegistry& Simulation::registry() const { return m_registry; }
EPIX_API Simulation::ChunkMap& Simulation::chunk_map() { return m_chunk_map; }
EPIX_API const Simulation::ChunkMap& Simulation::chunk_map() const {
    return m_chunk_map;
}
EPIX_API void Simulation::reset_updated() { m_chunk_map.reset_updated(); }
EPIX_API bool Simulation::is_updated(int x, int y) const {
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto [cell_x, cell_y]   = to_in_chunk_pos(x, y);
    return m_chunk_map.get_chunk(chunk_x, chunk_y).is_updated(cell_x, cell_y);
}
EPIX_API void Simulation::load_chunk(int x, int y, const Chunk& chunk) {
    m_chunk_map.load_chunk(x, y, chunk);
}
EPIX_API void Simulation::load_chunk(int x, int y, Chunk&& chunk) {
    m_chunk_map.load_chunk(x, y, std::move(chunk));
}
EPIX_API void Simulation::load_chunk(int x, int y) {
    m_chunk_map.load_chunk(x, y);
}
EPIX_API std::pair<int, int> Simulation::to_chunk_pos(int x, int y) const {
    std::pair<int, int> pos;
    if (x < 0) {
        pos.first = (x + 1) / m_chunk_size - 1;
    } else {
        pos.first = x / m_chunk_size;
    }
    if (y < 0) {
        pos.second = (y + 1) / m_chunk_size - 1;
    } else {
        pos.second = y / m_chunk_size;
    }
    return pos;
}
EPIX_API std::pair<int, int> Simulation::to_in_chunk_pos(int x, int y) const {
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    return {x - chunk_x * m_chunk_size, y - chunk_y * m_chunk_size};
}
EPIX_API bool Simulation::contain_cell(int x, int y) const {
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto [cell_x, cell_y]   = to_in_chunk_pos(x, y);
    return m_chunk_map.contains(chunk_x, chunk_y) &&
           m_chunk_map.get_chunk(chunk_x, chunk_y).contains(cell_x, cell_y);
}
EPIX_API bool Simulation::valid(int x, int y) const {
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    return m_chunk_map.contains(chunk_x, chunk_y);
}
EPIX_API std::tuple<Cell&, const Element&> Simulation::get(int x, int y) {
    assert(contain_cell(x, y));
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto [cell_x, cell_y]   = to_in_chunk_pos(x, y);
    Cell& cell = m_chunk_map.get_chunk(chunk_x, chunk_y).get(cell_x, cell_y);
    const Element& elem = m_registry.get_elem(cell.elem_id);
    return {cell, elem};
}
EPIX_API std::tuple<const Cell&, const Element&> Simulation::get(int x, int y)
    const {
    assert(contain_cell(x, y));
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto [cell_x, cell_y]   = to_in_chunk_pos(x, y);
    const Cell& cell =
        m_chunk_map.get_chunk(chunk_x, chunk_y).get(cell_x, cell_y);
    const Element& elem = m_registry.get_elem(cell.elem_id);
    return {cell, elem};
}
EPIX_API Cell& Simulation::get_cell(int x, int y) {
    assert(valid(x, y));
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto [cell_x, cell_y]   = to_in_chunk_pos(x, y);
    return m_chunk_map.get_chunk(chunk_x, chunk_y).get(cell_x, cell_y);
}
EPIX_API const Cell& Simulation::get_cell(int x, int y) const {
    assert(valid(x, y));
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto [cell_x, cell_y]   = to_in_chunk_pos(x, y);
    return m_chunk_map.get_chunk(chunk_x, chunk_y).get(cell_x, cell_y);
}
EPIX_API void Simulation::swap(int x, int y, int tx, int ty) {
    auto [chunk_x, chunk_y]   = to_chunk_pos(x, y);
    auto [cell_x, cell_y]     = to_in_chunk_pos(x, y);
    auto [tchunk_x, tchunk_y] = to_chunk_pos(tx, ty);
    auto [tcell_x, tcell_y]   = to_in_chunk_pos(tx, ty);
    if (!m_chunk_map.contains(chunk_x, chunk_y) ||
        !m_chunk_map.contains(tchunk_x, tchunk_y)) {
        return;
    }
    auto& chunk  = m_chunk_map.get_chunk(chunk_x, chunk_y);
    auto& tchunk = m_chunk_map.get_chunk(tchunk_x, tchunk_y);
    if (!chunk.contains(cell_x, cell_y) && !tchunk.contains(tcell_x, tcell_y)) {
        return;
    }
    if (chunk.contains(cell_x, cell_y) && tchunk.contains(tcell_x, tcell_y)) {
        auto& cell  = chunk.get(cell_x, cell_y);
        auto& tcell = tchunk.get(tcell_x, tcell_y);
        std::swap(cell, tcell);
        return;
    }
    if (chunk.contains(cell_x, cell_y)) {
        auto& cell = chunk.get(cell_x, cell_y);
        tchunk.insert(tcell_x, tcell_y, std::move(cell));
        chunk.remove(cell_x, cell_y);
        return;
    }
    if (tchunk.contains(tcell_x, tcell_y)) {
        auto& tcell = tchunk.get(tcell_x, tcell_y);
        chunk.insert(cell_x, cell_y, std::move(tcell));
        tchunk.remove(tcell_x, tcell_y);
        return;
    }
}
EPIX_API const Element& Simulation::get_elem(int x, int y) const {
    assert(contain_cell(x, y));
    return m_registry.get_elem(get_cell(x, y).elem_id);
}
EPIX_API void Simulation::remove(int x, int y) {
    assert(valid(x, y));
    touch(x, y);
    touch(x - 1, y);
    touch(x + 1, y);
    touch(x, y - 1);
    touch(x, y + 1);
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto [cell_x, cell_y]   = to_in_chunk_pos(x, y);
    m_chunk_map.get_chunk(chunk_x, chunk_y).remove(cell_x, cell_y);
}
EPIX_API glm::vec2 Simulation::get_grav(int x, int y) {
    assert(valid(x, y));
    glm::vec2 grav = {0.0f, -98.0f};
    // float dist     = std::sqrt(x * x + y * y);
    // if (dist == 0) return grav;
    // glm::vec2 addition = {
    //     (float)x / dist / dist * 100000, (float)y / dist / dist * 100000
    // };
    // float len_addition =
    //     std::sqrt(addition.x * addition.x + addition.y * addition.y);
    // if (len_addition >= 200) {
    //     addition *= 200 / len_addition;
    // }
    // grav -= addition;
    return grav;
}
static float calculate_angle_diff(const glm::vec2& a, const glm::vec2& b) {
    float dot = a.x * b.x + a.y * b.y;
    float det = a.x * b.y - a.y * b.x;
    return std::atan2(det, dot);
}
EPIX_API glm::vec2 Simulation::get_default_vel(int x, int y) {
    return {0.0f, 0.0f};
    assert(valid(x, y));
    auto grav = get_grav(x, y);
    return {grav.x * 0.4f, grav.y * 0.4f};
}
EPIX_API int Simulation::not_moving_threshold(glm::vec2 grav) {
    // the larger the gravity, the smaller the threshold
    auto len = std::sqrt(grav.x * grav.x + grav.y * grav.y);
    if (len == 0) return std::numeric_limits<int>::max();
    return not_moving_threshold_setting.numerator /
           std::pow(len, not_moving_threshold_setting.power);
}
EPIX_API void Simulation::UpdateState::next() {
    uint8_t state = 0;
    state |= xorder ? 1 : 0;
    state |= yorder ? 2 : 0;
    state |= x_outer ? 4 : 0;
    state--;
    xorder  = state & 1;
    yorder  = state & 2;
    x_outer = state & 4;
}
EPIX_API std::optional<glm::ivec2> Simulation::UpdatingState::current_chunk(
) const {
    if (!is_updating) return std::nullopt;
    if (updating_index < 0 || updating_index >= updating_chunks.size()) {
        return std::nullopt;
    }
    return updating_chunks[updating_index];
}
EPIX_API std::optional<glm::ivec2> Simulation::UpdatingState::current_cell(
) const {
    if (!is_updating) return std::nullopt;
    if (!current_chunk()) return std::nullopt;
    return in_chunk_pos;
}
void apply_viscosity(
    Simulation& sim, Cell& cell, int x, int y, int tx, int ty
) {
    if (!sim.valid(tx, ty)) return;
    if (!sim.contain_cell(tx, ty)) return;
    auto [tcell, elem] = sim.get(tx, ty);
    if (!elem.is_liquid()) return;
    tcell.velocity += 0.003f * cell.velocity - 0.003f * tcell.velocity;
}
EPIX_API void Simulation::update(float delta) {
    init_update_state();
    // update cells possibly remained in chunk that hasn't finished updating
    while (next_cell()) {
        update_cell(delta);
    }
    while (next_chunk()) {
        update_chunk(delta);
    }
    deinit_update_state();
}
EPIX_API float Simulation::air_density(int x, int y) { return 0.001225f; }
void epix::world::sand::components::update_cell(
    Simulation& sim, const int x_, const int y_, float delta
) {
    auto [chunk_x, chunk_y] = sim.to_chunk_pos(x_, y_);
    auto [cell_x, cell_y]   = sim.to_in_chunk_pos(x_, y_);
    auto& chunk             = sim.chunk_map().get_chunk(chunk_x, chunk_y);
    if (!chunk.contains(cell_x, cell_y)) return;
    auto& cell = chunk.get(cell_x, cell_y);
    if (cell.updated) return;
    auto& elem = sim.registry().get_elem(cell.elem_id);
    if (elem.is_solid()) return;
    int final_x  = x_;
    int final_y  = y_;
    auto grav    = sim.get_grav(x_, y_);
    cell.updated = true;
    if (elem.is_powder()) {
        {
            int liquid_count         = 0;
            int empty_count          = 0;
            float liquid_density     = 0.0f;
            int b_lb_rb_not_freefall = 0;
            if (grav != glm::vec2(0.0f, 0.0f)) {
                float grav_angle = std::atan2(grav.y, grav.x);
                glm::ivec2 below = {
                    std::round(std::cos(grav_angle)),
                    std::round(std::sin(grav_angle))
                };
                glm::ivec2 above = {
                    std::round(std::cos(grav_angle + std::numbers::pi)),
                    std::round(std::sin(grav_angle + std::numbers::pi))
                };
                glm::ivec2 l = {
                    std::round(std::cos(grav_angle - std::numbers::pi / 2)),
                    std::round(std::sin(grav_angle - std::numbers::pi / 2))
                };
                glm::ivec2 r = {
                    std::round(std::cos(grav_angle + std::numbers::pi / 2)),
                    std::round(std::sin(grav_angle + std::numbers::pi / 2))
                };
                if (sim.valid(x_ + below.x, y_ + below.y) &&
                    sim.contain_cell(x_ + below.x, y_ + below.y)) {
                    auto [tcell, telem] = sim.get(x_ + below.x, y_ + below.y);
                    if (telem.is_liquid()) {
                        liquid_count++;
                        liquid_density += telem.density;
                    }
                    if (telem.is_powder() && !tcell.freefall) {
                        b_lb_rb_not_freefall++;
                    }
                } else {
                    empty_count++;
                }
                if (sim.valid(x_ + above.x, y_ + above.y) &&
                    sim.contain_cell(x_ + above.x, y_ + above.y)) {
                    auto [tcell, telem] = sim.get(x_ + above.x, y_ + above.y);
                    if (telem.is_liquid()) {
                        liquid_count++;
                        liquid_density += telem.density;
                    }
                } else {
                    empty_count++;
                }
                float liquid_drag   = 0.4f;
                float vertical_rate = 0.0f;
                if (sim.valid(x_ + l.x, y_ + l.y) &&
                    sim.contain_cell(x_ + l.x, y_ + l.y)) {
                    auto [tcell, telem] = sim.get(x_ + l.x, y_ + l.y);
                    if (telem.is_liquid() && telem.density > elem.density) {
                        liquid_count++;
                        liquid_density += telem.density;
                        glm::vec2 vel_hori =
                            glm::normalize(glm::vec2(l)) *
                            glm::dot(tcell.velocity, glm::vec2(l));
                        vel_hori = (1 - vertical_rate) * vel_hori +
                                   vertical_rate * tcell.velocity;
                        if (glm::length(vel_hori) > glm::length(cell.velocity))
                            cell.velocity =
                                liquid_drag * vel_hori +
                                (1.0f - liquid_drag) * cell.velocity;
                    }
                    if (telem.is_powder() && !tcell.freefall) {
                        b_lb_rb_not_freefall++;
                    }
                } else {
                    empty_count++;
                }
                if (sim.valid(x_ + r.x, y_ + r.y) &&
                    sim.contain_cell(x_ + r.x, y_ + r.y)) {
                    auto [tcell, telem] = sim.get(x_ + r.x, y_ + r.y);
                    if (telem.is_liquid() && telem.density > elem.density) {
                        liquid_count++;
                        liquid_density += telem.density;
                        glm::vec2 vel_hori =
                            glm::normalize(glm::vec2(r)) *
                            glm::dot(tcell.velocity, glm::vec2(r));
                        vel_hori = (1 - vertical_rate) * vel_hori +
                                   vertical_rate * tcell.velocity;
                        if (glm::length(vel_hori) > glm::length(cell.velocity))
                            cell.velocity =
                                liquid_drag * vel_hori +
                                (1.0f - liquid_drag) * cell.velocity;
                    }
                    if (telem.is_powder() && !tcell.freefall) {
                        b_lb_rb_not_freefall++;
                    }
                } else {
                    empty_count++;
                }
            }
            if (b_lb_rb_not_freefall == 3) {
                cell.freefall = false;
                cell.velocity = {0.0f, 0.0f};
            }
            if (liquid_count > empty_count) {
                liquid_density /= liquid_count;
                grav *= (elem.density - liquid_density) / elem.density;
                cell.velocity *= 0.9f;
            }
        }
        if (!cell.freefall) {
            float angle = std::atan2(grav.y, grav.x);
            // into a 8 direction
            angle = std::round(angle / (std::numbers::pi / 4)) *
                    (std::numbers::pi / 4);
            glm::ivec2 dir = {
                std::round(std::cos(angle)), std::round(std::sin(angle))
            };
            int below_x = x_ + dir.x;
            int below_y = y_ + dir.y;
            int above_x = x_ - dir.x;
            int above_y = y_ - dir.y;
            if (!sim.valid(below_x, below_y)) return;
            if (sim.contain_cell(below_x, below_y)) {
                auto [bcell, belem] = sim.get(below_x, below_y);
                if (belem.is_solid()) {
                    return;
                }
                if (belem.is_powder() && !bcell.freefall) {
                    return;
                }
            }
            if (sim.valid(above_x, above_y) &&
                sim.contain_cell(above_x, above_y)) {
                auto&& [acell, aelem] = sim.get(above_x, above_y);
                if (aelem.is_powder() && !acell.freefall) {
                    acell.freefall = true;
                    acell.velocity = sim.get_default_vel(above_x, above_y);
                    sim.touch(above_x, above_y);
                    sim.touch(above_x - 1, above_y);
                    sim.touch(above_x + 1, above_y);
                    sim.touch(above_x, above_y - 1);
                    sim.touch(above_x, above_y + 1);
                }
            }
            cell.velocity = sim.get_default_vel(x_, y_);
            cell.freefall = true;
        }
        sim.touch(x_, y_);
        cell.velocity += grav * delta;
        cell.velocity *= 0.99f;
        cell.inpos += cell.velocity * delta;
        int delta_x = std::round(cell.inpos.x);
        int delta_y = std::round(cell.inpos.y);
        if (delta_x == 0 && delta_y == 0) {
            return;
        }
        cell.inpos.x -= delta_x;
        cell.inpos.y -= delta_y;
        if (sim.max_travel) {
            delta_x =
                std::clamp(delta_x, -sim.max_travel->x, sim.max_travel->y);
            delta_y =
                std::clamp(delta_y, -sim.max_travel->x, sim.max_travel->y);
        }
        int tx              = x_ + delta_x;
        int ty              = y_ + delta_y;
        bool moved          = false;
        auto raycast_result = sim.raycast_to(x_, y_, tx, ty);
        // if (raycast_result.hit) {
        //     auto [hit_x, hit_y]       = raycast_result.hit.value();
        //     auto [tchunk_x, tchunk_y] = sim.to_chunk_pos(hit_x, hit_y);
        //     if (tchunk_x == chunk_x && tchunk_y == chunk_y &&
        //         sim.valid(hit_x, hit_y) && sim.contain_cell(hit_x, hit_y)) {
        //         update_cell(sim, hit_x, hit_y, delta);
        //         raycast_result = sim.raycast_to(x_, y_, hit_x, hit_y);
        //     }
        // }
        if (raycast_result.steps) {
            sim.swap(x_, y_, raycast_result.new_x, raycast_result.new_y);
            final_x = raycast_result.new_x;
            final_y = raycast_result.new_y;
            moved   = true;
        }
        if (raycast_result.hit) {
            auto [hit_x, hit_y]    = raycast_result.hit.value();
            bool blocking_freefall = false;
            bool collided          = false;
            if (sim.valid(hit_x, hit_y)) {
                auto [tcell, telem] = sim.get(hit_x, hit_y);
                if (telem.is_solid() || telem.is_powder()) {
                    collided = sim.collide(
                        raycast_result.new_x, raycast_result.new_y, hit_x, hit_y
                    );
                    blocking_freefall = tcell.freefall;
                } else {
                    sim.swap(final_x, final_y, hit_x, hit_y);
                    final_x = hit_x;
                    final_y = hit_y;
                    moved   = true;
                }
            }
            if (!moved && glm::length(grav) > 0.0f) {
                if (sim.powder_slide_setting.always_slide ||
                    (sim.valid(hit_x, hit_y) &&
                     !sim.get_cell(hit_x, hit_y).freefall)) {
                    float grav_angle = std::atan2(grav.y, grav.x);
                    glm::vec2 lb     = {
                        std::cos(grav_angle - std::numbers::pi / 4),
                        std::sin(grav_angle - std::numbers::pi / 4)
                    };
                    glm::vec2 rb = {
                        std::cos(grav_angle + std::numbers::pi / 4),
                        std::sin(grav_angle + std::numbers::pi / 4)
                    };
                    glm::vec2 l = {
                        std::cos(grav_angle - std::numbers::pi / 2),
                        std::sin(grav_angle - std::numbers::pi / 2)
                    };
                    glm::vec2 r = {
                        std::cos(grav_angle + std::numbers::pi / 2),
                        std::sin(grav_angle + std::numbers::pi / 2)
                    };
                    // try go to left bottom and right bottom
                    float vel_angle =
                        std::atan2(cell.velocity.y, cell.velocity.x);
                    float angle_diff =
                        calculate_angle_diff(cell.velocity, grav);
                    if (std::abs(angle_diff) < std::numbers::pi / 2) {
                        glm::vec2 dirs[2];
                        glm::vec2 idirs[2];
                        static thread_local std::random_device rd;
                        static thread_local std::mt19937 gen(rd());
                        static thread_local std::uniform_real_distribution<
                            float>
                            dis(-0.3f, 0.3f);
                        bool lb_first = dis(gen) > 0;
                        if (lb_first) {
                            dirs[0]  = lb;
                            dirs[1]  = rb;
                            idirs[0] = l;
                            idirs[1] = r;
                        } else {
                            dirs[0]  = rb;
                            dirs[1]  = lb;
                            idirs[0] = r;
                            idirs[1] = l;
                        }
                        for (int i = 0; i < 2; i++) {
                            auto& vel   = dirs[i];
                            int delta_x = std::round(vel.x);
                            int delta_y = std::round(vel.y);
                            if (delta_x == 0 && delta_y == 0) {
                                continue;
                            }
                            int tx = final_x + delta_x;
                            int ty = final_y + delta_y;
                            if (!sim.valid(tx, ty)) continue;
                            if (!sim.contain_cell(tx, ty) ||
                                sim.registry()
                                    .get_elem(sim.get_cell(tx, ty).elem_id)
                                    .is_liquid()) {
                                sim.swap(final_x, final_y, tx, ty);
                                auto& ncell = sim.get_cell(tx, ty);
                                ncell.velocity +=
                                    glm::vec2(idirs[i]) *
                                    sim.powder_slide_setting.prefix / delta;
                                final_x = tx;
                                final_y = ty;
                                moved   = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (!blocking_freefall && !moved) {
                auto& ncell    = sim.get_cell(final_x, final_y);
                ncell.freefall = false;
                ncell.velocity = {0.0f, 0.0f};
            }
        }
        if (moved) {
            auto& ncell          = sim.get_cell(final_x, final_y);
            ncell.not_move_count = 0;
            sim.touch(x_ - 1, y_);
            sim.touch(x_ + 1, y_);
            sim.touch(x_, y_ - 1);
            sim.touch(x_, y_ + 1);
            sim.touch(x_ - 1, y_ - 1);
            sim.touch(x_ + 1, y_ - 1);
            sim.touch(x_ - 1, y_ + 1);
            sim.touch(x_ + 1, y_ + 1);
            sim.touch(final_x - 1, final_y);
            sim.touch(final_x + 1, final_y);
            sim.touch(final_x, final_y - 1);
            sim.touch(final_x, final_y + 1);
        } else {
            cell.not_move_count++;
            if (cell.not_move_count >= sim.not_moving_threshold(grav)) {
                cell.not_move_count = 0;
                cell.freefall       = false;
                cell.velocity       = {0.0f, 0.0f};
            }
        }
    } else if (elem.is_liquid()) {
        cell.freefall = true;
        if (!cell.freefall) {
            float angle = std::atan2(grav.y, grav.x);
            // into a 8 direction
            angle = std::round(angle / (std::numbers::pi / 4)) *
                    (std::numbers::pi / 4);
            glm::ivec2 dir = {
                std::round(std::cos(angle)), std::round(std::sin(angle))
            };
            glm::ivec2 lb = {
                std::round(std::cos(angle - std::numbers::pi / 4)),
                std::round(std::sin(angle - std::numbers::pi / 4))
            };
            glm::ivec2 rb = {
                std::round(std::cos(angle + std::numbers::pi / 4)),
                std::round(std::sin(angle + std::numbers::pi / 4))
            };
            int below_x = x_ + dir.x;
            int below_y = y_ + dir.y;
            int lb_x    = x_ + lb.x;
            int lb_y    = y_ + lb.y;
            int rb_x    = x_ + rb.x;
            int rb_y    = y_ + rb.y;
            if (!sim.valid(below_x, below_y) && !sim.valid(lb_x, lb_y) &&
                !sim.valid(rb_x, rb_y)) {
                return;
            }
            bool should_freefall = false;
            if (sim.valid(below_x, below_y)) {
                bool shouldnot_freefall = false;
                if (sim.contain_cell(below_x, below_y)) {
                    auto&& [bcell, belem] = sim.get(below_x, below_y);
                    if (belem.is_solid()) {
                        shouldnot_freefall = true;
                    }
                    if ((belem.is_powder() || belem.is_liquid()) &&
                        !bcell.freefall) {
                        shouldnot_freefall = true;
                    }
                }
                should_freefall |= !shouldnot_freefall;
            }
            if (sim.valid(lb_x, lb_y)) {
                bool shouldnot_freefall = false;
                if (sim.contain_cell(lb_x, lb_y)) {
                    auto&& [lbcell, lbelem] = sim.get(lb_x, lb_y);
                    if (lbelem.is_solid()) {
                        shouldnot_freefall = true;
                    }
                    if ((lbelem.is_powder() || lbelem.is_liquid()) &&
                        !lbcell.freefall) {
                        shouldnot_freefall = true;
                    }
                }
                should_freefall |= !shouldnot_freefall;
            }
            if (sim.valid(rb_x, rb_y)) {
                bool shouldnot_freefall = false;
                if (sim.contain_cell(rb_x, rb_y)) {
                    auto&& [rbcell, rbelem] = sim.get(rb_x, rb_y);
                    if (rbelem.is_solid()) {
                        shouldnot_freefall = true;
                    }
                    if ((rbelem.is_powder() || rbelem.is_liquid()) &&
                        !rbcell.freefall) {
                        shouldnot_freefall = true;
                    }
                }
                should_freefall |= !shouldnot_freefall;
            }
            if (!should_freefall) return;

            cell.velocity = sim.get_default_vel(x_, y_);
            cell.freefall = true;
        }
        // sim.touch(x_, y_);
        apply_viscosity(sim, cell, final_x, final_y, final_x - 1, final_y);
        apply_viscosity(sim, cell, final_x, final_y, final_x + 1, final_y);
        apply_viscosity(sim, cell, final_x, final_y, final_x, final_y - 1);
        apply_viscosity(sim, cell, final_x, final_y, final_x, final_y + 1);

        {
            int liquid_count     = 0;
            int empty_count      = 0;
            float liquid_density = 0.0f;
            if (grav != glm::vec2(0.0f, 0.0f)) {
                float grav_angle = std::atan2(grav.y, grav.x);
                glm::ivec2 below = {
                    std::round(std::cos(grav_angle)),
                    std::round(std::sin(grav_angle))
                };
                glm::ivec2 above = {
                    std::round(std::cos(grav_angle + std::numbers::pi)),
                    std::round(std::sin(grav_angle + std::numbers::pi))
                };
                glm::ivec2 la = {
                    std::round(std::cos(grav_angle - std::numbers::pi / 4)),
                    std::round(std::sin(grav_angle - std::numbers::pi / 4))
                };
                glm::ivec2 ra = {
                    std::round(std::cos(grav_angle + std::numbers::pi / 4)),
                    std::round(std::sin(grav_angle + std::numbers::pi / 4))
                };
                glm::ivec2 l = {
                    std::round(std::cos(grav_angle - std::numbers::pi / 2)),
                    std::round(std::sin(grav_angle - std::numbers::pi / 2))
                };
                glm::ivec2 r = {
                    std::round(std::cos(grav_angle + std::numbers::pi / 2)),
                    std::round(std::sin(grav_angle + std::numbers::pi / 2))
                };
                if (sim.valid(x_ + below.x, y_ + below.y) &&
                    sim.contain_cell(x_ + below.x, y_ + below.y)) {
                    auto [tcell, telem] = sim.get(x_ + below.x, y_ + below.y);
                    if (telem.is_liquid() && telem != elem) {
                        liquid_count++;
                        liquid_density += telem.density;
                    }
                } else {
                    empty_count++;
                }
                if (sim.valid(x_ + above.x, y_ + above.y) &&
                    sim.contain_cell(x_ + above.x, y_ + above.y)) {
                    auto [tcell, telem] = sim.get(x_ + above.x, y_ + above.y);
                    if (telem.is_liquid() && telem.density > elem.density) {
                        liquid_count++;
                        liquid_density += telem.density;
                    }
                    // if (telem.is_liquid()) {
                    //     sim.touch(x_ + above.x, y_ + above.y);
                    //     // sim.touch(x_ + la.x, y_ + la.y);
                    //     // sim.touch(x_ + ra.x, y_ + ra.y);
                    // }
                } else {
                    empty_count++;
                }
                float liquid_drag   = 0.4f;
                float vertical_rate = 0.0f;
                if (sim.valid(x_ + l.x, y_ + l.y) &&
                    sim.contain_cell(x_ + l.x, y_ + l.y)) {
                    auto [tcell, telem] = sim.get(x_ + l.x, y_ + l.y);
                    if (telem.is_liquid() && telem.density > elem.density) {
                        liquid_count++;
                        liquid_density += telem.density;
                        glm::vec2 vel_hori =
                            glm::normalize(glm::vec2(l)) *
                            glm::dot(tcell.velocity, glm::vec2(l));
                        vel_hori = (1 - vertical_rate) * vel_hori +
                                   vertical_rate * tcell.velocity;
                        if (glm::length(vel_hori) > glm::length(cell.velocity))
                            cell.velocity =
                                liquid_drag * vel_hori +
                                (1.0f - liquid_drag) * cell.velocity;
                    }
                } else {
                    empty_count++;
                }
                if (sim.valid(x_ + r.x, y_ + r.y) &&
                    sim.contain_cell(x_ + r.x, y_ + r.y)) {
                    auto [tcell, telem] = sim.get(x_ + r.x, y_ + r.y);
                    if (telem.is_liquid() && telem.density > elem.density) {
                        liquid_count++;
                        liquid_density += telem.density;
                        glm::vec2 vel_hori =
                            glm::normalize(glm::vec2(r)) *
                            glm::dot(tcell.velocity, glm::vec2(r));
                        vel_hori = (1 - vertical_rate) * vel_hori +
                                   vertical_rate * tcell.velocity;
                        if (glm::length(vel_hori) > glm::length(cell.velocity))
                            cell.velocity =
                                liquid_drag * vel_hori +
                                (1.0f - liquid_drag) * cell.velocity;
                    }
                } else {
                    empty_count++;
                }
            }
            if (liquid_count > empty_count) {
                liquid_density /= liquid_count;
                grav *= (elem.density - liquid_density) / elem.density;
                cell.velocity *= 0.95f;
            }
        }
        cell.velocity += grav * delta;
        cell.velocity *= 0.99f;
        cell.inpos += cell.velocity * delta;
        int delta_x = std::round(cell.inpos.x);
        int delta_y = std::round(cell.inpos.y);
        if (delta_x == 0 && delta_y == 0) {
            return;
        }
        cell.inpos.x -= delta_x;
        cell.inpos.y -= delta_y;
        if (sim.max_travel) {
            delta_x =
                std::clamp(delta_x, -sim.max_travel->x, sim.max_travel->y);
            delta_y =
                std::clamp(delta_y, -sim.max_travel->x, sim.max_travel->y);
        }
        int tx              = x_ + delta_x;
        int ty              = y_ + delta_y;
        bool moved          = false;
        auto raycast_result = sim.raycast_to(x_, y_, tx, ty);
        // if (raycast_result.hit) {
        //     auto [tchunk_x, tchunk_y] = sim.to_chunk_pos(
        //         raycast_result.hit->first, raycast_result.hit->second
        //     );
        //     if (tchunk_x == chunk_x && tchunk_y == chunk_y &&
        //         sim.valid(
        //             raycast_result.hit->first, raycast_result.hit->second
        //         ) &&
        //         sim.contain_cell(
        //             raycast_result.hit->first, raycast_result.hit->second
        //         )) {
        //         update_cell(
        //             sim, raycast_result.hit->first,
        //             raycast_result.hit->second, delta
        //         );
        //         raycast_result = sim.raycast_to(x_, y_, tx, ty);
        //     }
        // }
        if (raycast_result.steps) {
            sim.swap(x_, y_, raycast_result.new_x, raycast_result.new_y);
            final_x = raycast_result.new_x;
            final_y = raycast_result.new_y;
            moved   = true;
        }
        if (raycast_result.hit) {
            auto [hit_x, hit_y]    = raycast_result.hit.value();
            bool blocking_freefall = false;
            bool collided          = false;
            if (sim.valid(hit_x, hit_y)) {
                auto [tcell, telem] = sim.get(hit_x, hit_y);
                if (telem.is_solid() || telem.is_powder() ||
                    (telem.is_liquid() &&
                     (telem.density > elem.density || telem == elem))) {
                    collided = sim.collide(
                        raycast_result.new_x, raycast_result.new_y, hit_x, hit_y
                    );
                } else {
                    sim.swap(final_x, final_y, hit_x, hit_y);
                    final_x = hit_x;
                    final_y = hit_y;
                    moved   = true;
                }
            }
            if (!moved) {
                float vel_angle  = std::atan2(cell.velocity.y, cell.velocity.x);
                float grav_angle = std::atan2(grav.y, grav.x);
                float angle_diff = calculate_angle_diff(cell.velocity, grav);
                if (std::abs(angle_diff) < std::numbers::pi / 2) {
                    // try go to left bottom and right bottom
                    glm::vec2 lb = {
                        std::cos(grav_angle - std::numbers::pi / 4),
                        std::sin(grav_angle - std::numbers::pi / 4)
                    };
                    glm::vec2 rb = {
                        std::cos(grav_angle + std::numbers::pi / 4),
                        std::sin(grav_angle + std::numbers::pi / 4)
                    };
                    glm::vec2 left = {
                        std::cos(grav_angle - std::numbers::pi / 2) *
                            sim.liquid_spread_setting.spread_len,
                        std::sin(grav_angle - std::numbers::pi / 2) *
                            sim.liquid_spread_setting.spread_len
                    };
                    glm::vec2 right = {
                        std::cos(grav_angle + std::numbers::pi / 2) *
                            sim.liquid_spread_setting.spread_len,
                        std::sin(grav_angle + std::numbers::pi / 2) *
                            sim.liquid_spread_setting.spread_len
                    };
                    glm::vec2 dirs[2];
                    glm::vec2 idirs[2];
                    static thread_local std::random_device rd;
                    static thread_local std::mt19937 gen(rd());
                    static thread_local std::uniform_real_distribution<float>
                        dis(-0.1f, 0.1f);
                    bool l_first = angle_diff > dis(gen);
                    if (l_first) {
                        dirs[0]  = lb;
                        dirs[1]  = rb;
                        idirs[0] = left;
                        idirs[1] = right;
                    } else {
                        dirs[0]  = rb;
                        dirs[1]  = lb;
                        idirs[0] = right;
                        idirs[1] = left;
                    }
                    for (int i = 0; i < 2; i++) {
                        auto vel    = dirs[i];
                        int delta_x = std::round(vel.x);
                        int delta_y = std::round(vel.y);
                        if (delta_x == 0 && delta_y == 0) {
                            continue;
                        }
                        int tx = final_x + delta_x;
                        int ty = final_y + delta_y;
                        if (!sim.valid(tx, ty)) continue;
                        if (!sim.contain_cell(tx, ty)) {
                            sim.swap(final_x, final_y, tx, ty);
                            auto& ncell = sim.get_cell(tx, ty);
                            ncell.velocity += idirs[i] *
                                              sim.liquid_spread_setting.prefix /
                                              delta;
                            final_x = tx;
                            final_y = ty;
                            moved   = true;
                            break;
                        } else {
                            auto& telem = sim.get_elem(tx, ty);
                            if (telem.is_liquid() &&
                                telem.density < elem.density) {
                                sim.swap(final_x, final_y, tx, ty);
                                auto& ncell = sim.get_cell(tx, ty);
                                ncell.velocity +=
                                    idirs[i] *
                                    sim.liquid_spread_setting.prefix / delta;
                                final_x = tx;
                                final_y = ty;
                                moved   = true;
                                break;
                            }
                        }
                    }
                    if (!moved) {
                        // dirs[1] *= 0.5f;
                        int tx_1 = final_x + std::round(idirs[0].x);
                        int ty_1 = final_y + std::round(idirs[0].y);
                        int tx_2 = final_x + std::round(idirs[1].x);
                        int ty_2 = final_y + std::round(idirs[1].y);
                        auto res_1 =
                            sim.raycast_to(final_x, final_y, tx_1, ty_1);
                        auto res_2 =
                            sim.raycast_to(final_x, final_y, tx_2, ty_2);
                        bool inverse_pressure = false;
                        if (res_1.hit) {
                            auto [tchunk_x, tchunk_y] = sim.to_chunk_pos(
                                res_1.hit->first, res_1.hit->second
                            );
                            if (tchunk_x == chunk_x && tchunk_y == chunk_y &&
                                sim.valid(
                                    res_1.hit->first, res_1.hit->second
                                ) &&
                                sim.contain_cell(
                                    res_1.hit->first, res_1.hit->second
                                )) {
                                update_cell(
                                    sim, res_1.hit->first, res_1.hit->second,
                                    delta
                                );
                                res_1 = sim.raycast_to(
                                    final_x, final_y, tx_1, ty_1
                                );
                            }
                            if (res_1.hit &&
                                sim.valid(
                                    res_1.hit->first, res_1.hit->second
                                ) &&
                                sim.contain_cell(
                                    res_1.hit->first, res_1.hit->second
                                )) {
                                auto [tcell, telem] = sim.get(
                                    res_1.hit->first, res_1.hit->second
                                );
                                if (telem.is_liquid() &&
                                    telem.density < elem.density) {
                                    res_1.new_x = res_1.hit->first;
                                    res_1.new_y = res_1.hit->second;
                                    res_1.steps += 1;
                                    res_1.hit = std::nullopt;
                                }
                            }
                            if (res_1.hit && dis(gen) > 0.07f &&
                                sim.valid(
                                    res_1.hit->first, res_1.hit->second
                                ) &&
                                sim.contain_cell(
                                    res_1.hit->first, res_1.hit->second
                                )) {
                                auto [tcell, telem] = sim.get(
                                    res_1.hit->first, res_1.hit->second
                                );
                                if (telem.is_powder() &&
                                    telem.density < elem.density) {
                                    res_1.new_x = res_1.hit->first;
                                    res_1.new_y = res_1.hit->second;
                                    res_1.steps += 1;
                                    res_1.hit        = std::nullopt;
                                    inverse_pressure = true;
                                }
                            }
                        }
                        if (res_1.steps >= res_2.steps) {
                            if (res_1.steps) {
                                sim.swap(
                                    final_x, final_y, res_1.new_x, res_1.new_y
                                );
                                final_x = res_1.new_x;
                                final_y = res_1.new_y;
                                auto& ncell =
                                    sim.get_cell(res_1.new_x, res_1.new_y);
                                ncell.velocity +=
                                    (inverse_pressure ? -0.3f : 1.0f) *
                                    glm::vec2(idirs[0]) *
                                    sim.liquid_spread_setting.prefix / delta;
                                moved = true;
                            }
                        } else if (res_2.steps) {
                            sim.swap(
                                final_x, final_y, res_2.new_x, res_2.new_y
                            );
                            final_x = res_2.new_x;
                            final_y = res_2.new_y;
                            // ncell->velocity -=
                            //     glm::vec2(idirs[1]) *
                            //     sim.liquid_spread_setting.prefix / delta;
                            moved = true;
                        }
                    }
                }
            }
            // if (!blocking_freefall && !moved && !collided) {
            //     ncell->freefall = false;
            //     ncell->velocity = {0.0f, 0.0f};
            // }
        }
        if (moved) {
            auto& ncell          = sim.get_cell(final_x, final_y);
            ncell.not_move_count = 0;
            sim.touch(x_ - 1, y_);
            sim.touch(x_ + 1, y_);
            sim.touch(x_, y_ - 1);
            sim.touch(x_, y_ + 1);
            // sim.touch(x_ - 1, y_ - 1);
            // sim.touch(x_ + 1, y_ - 1);
            // sim.touch(x_ - 1, y_ + 1);
            // sim.touch(x_ + 1, y_ + 1);
            sim.touch(final_x - 1, final_y);
            sim.touch(final_x + 1, final_y);
            sim.touch(final_x, final_y - 1);
            sim.touch(final_x, final_y + 1);
            // sim.touch(final_x - 1, final_y - 1);
            // sim.touch(final_x + 1, final_y - 1);
            // sim.touch(final_x - 1, final_y + 1);
            // sim.touch(final_x + 1, final_y + 1);
            // apply_viscosity(sim, cell, final_x, final_y, x_ - 1, y_);
            // apply_viscosity(sim, cell, final_x, final_y, x_ + 1, y_);
            // apply_viscosity(sim, cell, final_x, final_y, x_, y_ - 1);
            // apply_viscosity(sim, cell, final_x, final_y, x_, y_ + 1);
        } else {
            cell.not_move_count++;
            if (cell.not_move_count >= sim.not_moving_threshold(grav) / 15) {
                cell.not_move_count = 0;
                cell.freefall       = false;
                cell.velocity       = {0.0f, 0.0f};
            }
        }
    } else if (elem.is_gas()) {
        grav *= (elem.density - sim.air_density(x_, y_)) / elem.density;
        {
            glm::vec2 hori_vel(grav.y, -grav.x);
            static thread_local std::random_device rd;
            static thread_local std::mt19937 gen(rd());
            static thread_local std::uniform_real_distribution<float> dis(
                -1.0f, 1.0f
            );
            cell.velocity += (grav + hori_vel * dis(gen)) * delta;
        }
        // grav /= 4.0f; // this is for larger chunk reset time
        cell.velocity -=
            0.1f * glm::length(cell.velocity) * cell.velocity / 20.0f;
        cell.inpos += cell.velocity * delta;
        int delta_x = std::round(cell.inpos.x);
        int delta_y = std::round(cell.inpos.y);
        if (delta_x == 0 && delta_y == 0) {
            return;
        }
        cell.inpos.x -= delta_x;
        cell.inpos.y -= delta_y;
        if (sim.max_travel) {
            delta_x =
                std::clamp(delta_x, -sim.max_travel->x, sim.max_travel->y);
            delta_y =
                std::clamp(delta_y, -sim.max_travel->x, sim.max_travel->y);
        }
        int tx              = x_ + delta_x;
        int ty              = y_ + delta_y;
        bool moved          = false;
        auto raycast_result = sim.raycast_to(x_, y_, tx, ty);
        if (raycast_result.steps) {
            sim.swap(x_, y_, raycast_result.new_x, raycast_result.new_y);
            final_x = raycast_result.new_x;
            final_y = raycast_result.new_y;
            moved   = true;
        }
        auto& ncell = sim.get_cell(final_x, final_y);
        float prefix_vdotgf =
            ncell.velocity.x * grav.x + ncell.velocity.y * grav.y;
        int prefix = prefix_vdotgf > 0 ? 1 : (prefix_vdotgf < 0 ? -1 : 0);
        if (elem.density > sim.air_density(x_, y_)) {
            prefix *= -1;
        }
        if (raycast_result.hit) {
            if (sim.valid(
                    raycast_result.hit->first, raycast_result.hit->second
                ) &&
                sim.contain_cell(
                    raycast_result.hit->first, raycast_result.hit->second
                )) {
                auto [tcell, telem] = sim.get(
                    raycast_result.hit->first, raycast_result.hit->second
                );
                if (telem.is_gas() &&
                    telem.density * prefix > elem.density * prefix) {
                    sim.swap(
                        final_x, final_y, raycast_result.hit->first,
                        raycast_result.hit->second
                    );
                    final_x = raycast_result.hit->first;
                    final_y = raycast_result.hit->second;
                    moved   = true;
                }
            }
        }
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        static thread_local std::uniform_real_distribution<float> dis(
            -0.1f, 0.1f
        );
        if (((!raycast_result.steps && raycast_result.hit) || dis(gen) > 0.05f
            ) &&
            glm::length(grav) > 0.0f && !moved) {
            // try left and right
            float grav_angle = std::atan2(grav.y, grav.x);
            float vel_angle  = std::atan2(cell.velocity.y, cell.velocity.x);
            float angle_diff = calculate_angle_diff(cell.velocity, grav);
            if (std::abs(angle_diff) < std::numbers::pi / 2) {
                glm::vec2 lb = {
                    std::cos(grav_angle - std::numbers::pi / 2),
                    std::sin(grav_angle - std::numbers::pi / 2)
                };
                glm::vec2 rb = {
                    std::cos(grav_angle + std::numbers::pi / 2),
                    std::sin(grav_angle + std::numbers::pi / 2)
                };
                glm::vec2 dirs[2];
                bool lb_first = dis(gen) > 0;
                if (lb_first) {
                    dirs[0] = lb;
                    dirs[1] = rb;
                } else {
                    dirs[0] = rb;
                    dirs[1] = lb;
                }
                for (auto&& vel : dirs) {
                    int delta_x = std::round(vel.x);
                    int delta_y = std::round(vel.y);
                    if (delta_x == 0 && delta_y == 0) {
                        continue;
                    }
                    int tx = final_x + delta_x;
                    int ty = final_y + delta_y;
                    if (!sim.valid(tx, ty)) continue;
                    if (!sim.contain_cell(tx, ty)) {
                        sim.swap(final_x, final_y, tx, ty);
                        final_x = tx;
                        final_y = ty;
                        moved   = true;
                        break;
                    } else {
                        auto [tcell, telem] = sim.get(tx, ty);
                        if (telem.is_gas() && telem != elem) {
                            sim.swap(final_x, final_y, tx, ty);
                            final_x = tx;
                            final_y = ty;
                            moved   = true;
                            break;
                        }
                    }
                }
            }
        }
        if (moved) {
            auto& ncell          = sim.get_cell(final_x, final_y);
            ncell.not_move_count = 0;
            sim.touch(x_ - 1, y_);
            sim.touch(x_ + 1, y_);
            sim.touch(x_, y_ - 1);
            sim.touch(x_, y_ + 1);
            sim.touch(final_x - 1, final_y);
            sim.touch(final_x + 1, final_y);
            sim.touch(final_x, final_y - 1);
            sim.touch(final_x, final_y + 1);
        }
    }
    float grav_len_s = grav.x * grav.x + grav.y * grav.y;
    if (grav_len_s == 0) {
        chunk.time_threshold = std::numeric_limits<int>::max();
    } else {
        chunk.time_threshold = std::max(
            (int)(EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME * 10000 / grav_len_s
            ),
            chunk.time_threshold
        );
    }
}
EPIX_API void Simulation::update_multithread(float delta) {
    std::vector<std::pair<int, int>> modres;
    int mod = 3;
    modres.reserve(mod * mod);
    for (int i = 0; i < mod; i++) {
        for (int j = 0; j < mod; j++) {
            modres.push_back({i, j});
        }
    }
    // reset_updated();
    update_state.next();
    // m_chunk_map.count_time();
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    std::shuffle(modres.begin(), modres.end(), gen);
    static thread_local std::uniform_real_distribution<float> dis(-0.3f, 0.3f);
    bool xorder  = update_state.xorder;
    bool yorder  = update_state.yorder;
    bool x_outer = update_state.x_outer;
    if (update_state.random_state) {
        xorder  = dis(gen) > 0;
        yorder  = dis(gen) > 0;
        x_outer = dis(gen) > 0;
    }
    m_chunk_map.set_iterate_setting(xorder, yorder, x_outer);
    for (auto&& [xmod, ymod] : modres) {
        for (auto&& [pos, chunk] : m_chunk_map) {
            if ((pos.x + xmod) % mod == 0 && (pos.y + ymod) % mod == 0) {
                if (!chunk.should_update()) continue;
                m_thread_pool->submit_task([=, &chunk]() {
                    chunk.reset_updated();
                    // std::tuple<int, int, int> xbounds, ybounds;
                    // xbounds = {
                    //     xorder ? chunk.updating_area[0]
                    //            : chunk.updating_area[1],
                    //     xorder ? chunk.updating_area[1] + 1
                    //            : chunk.updating_area[0] - 1,
                    //     xorder ? 1 : -1
                    // };
                    // ybounds = {
                    //     yorder ? chunk.updating_area[2]
                    //            : chunk.updating_area[3],
                    //     yorder ? chunk.updating_area[3] + 1
                    //            : chunk.updating_area[2] - 1,
                    //     yorder ? 1 : -1
                    // };
                    // std::tuple<int, int, int> bounds[2] = {xbounds, ybounds};
                    // if (!x_outer) {
                    //     std::swap(bounds[0], bounds[1]);
                    // }
                    // for (int index1 = std::get<0>(bounds[0]);
                    //      index1 != std::get<1>(bounds[0]);
                    //      index1 += std::get<2>(bounds[0])) {
                    //     for (int index2 = std::get<0>(bounds[1]);
                    //          index2 != std::get<1>(bounds[1]);
                    //          index2 += std::get<2>(bounds[1])) {
                    //         auto x = pos.x * m_chunk_size +
                    //                  (x_outer ? index1 : index2);
                    //         auto y = pos.y * m_chunk_size +
                    //                  (x_outer ? index2 : index1);
                    //         epix::world::sand::components::update_cell(
                    //             *this, x, y, delta
                    //         );
                    //     }
                    // }
                    int xmin = chunk.updating_area[0];
                    int xmax = chunk.updating_area[1];
                    int ymin = chunk.updating_area[2];
                    int ymax = chunk.updating_area[3];
                    std::vector<std::pair<int, int>> cells;
                    cells.reserve(chunk.cells.data().size());
                    for (auto&& [cell_pos, cell] : chunk.cells.view()) {
                        cells.emplace_back(cell_pos[0], cell_pos[1]);
                    }
                    for (auto&& [cx, cy] :
                         std::views::all(cells) |
                             std::views::filter([&](auto&& cell_pos) {
                                 return cell_pos.first >= xmin &&
                                        cell_pos.first <= xmax &&
                                        cell_pos.second >= ymin &&
                                        cell_pos.second <= ymax;
                             })) {
                        auto x = pos.x * m_chunk_size + cx;
                        auto y = pos.y * m_chunk_size + cy;
                        epix::world::sand::components::update_cell(
                            *this, x, y, delta
                        );
                    }
                    chunk.count_time();
                });
            }
        }
        m_thread_pool->wait();
    }
}
EPIX_API bool Simulation::init_update_state() {
    if (m_updating_state.is_updating) return false;
    m_updating_state.is_updating    = true;
    m_updating_state.updating_index = -1;
    auto& chunks_to_update          = m_updating_state.updating_chunks;
    chunks_to_update.clear();
    for (auto&& [pos, chunk] : m_chunk_map) {
        chunks_to_update.push_back(pos);
    }
    reset_updated();
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_real_distribution<float> dis(-0.3f, 0.3f);
    update_state.next();
    bool xorder  = update_state.xorder;
    bool yorder  = update_state.yorder;
    bool x_outer = update_state.x_outer;
    if (update_state.random_state) {
        xorder  = dis(gen) > 0;
        yorder  = dis(gen) > 0;
        x_outer = dis(gen) > 0;
    }
    std::sort(
        chunks_to_update.begin(), chunks_to_update.end(),
        [&](auto& a, auto& b) {
            if (x_outer) {
                return (xorder ? a.x < b.x : a.x > b.x) ||
                       (a.x == b.x && (yorder ? a.y < b.y : a.y > b.y));
            } else {
                return (yorder ? a.y < b.y : a.y > b.y) ||
                       (a.y == b.y && (xorder ? a.x < b.x : a.x > b.x));
            }
        }
    );
    return true;
}
EPIX_API bool Simulation::deinit_update_state() {
    if (!m_updating_state.is_updating) return false;
    m_chunk_map.count_time();
    m_updating_state.is_updating = false;
    return true;
}
EPIX_API bool Simulation::next_chunk() {
    m_updating_state.updating_index++;
    while (m_updating_state.updating_index <
           m_updating_state.updating_chunks.size()) {
        auto& pos =
            m_updating_state.updating_chunks[m_updating_state.updating_index];
        auto& chunk = m_chunk_map.get_chunk(pos.x, pos.y);
        if (!chunk.should_update()) {
            m_updating_state.updating_index++;
            continue;
        }
        auto& [xbounds, ybounds] = m_updating_state.bounds;
        xbounds                  = {
            update_state.xorder ? chunk.updating_area[0]
                                                 : chunk.updating_area[1],
            update_state.xorder ? chunk.updating_area[1] + 1
                                : chunk.updating_area[0] - 1,
            update_state.xorder ? 1 : -1
        };
        ybounds = {
            update_state.yorder ? chunk.updating_area[2]
                                : chunk.updating_area[3],
            update_state.yorder ? chunk.updating_area[3] + 1
                                : chunk.updating_area[2] - 1,
            update_state.yorder ? 1 : -1
        };
        m_updating_state.in_chunk_pos.reset();
        return true;
    }
    return false;
}
EPIX_API bool Simulation::next_cell() {
    if (!m_updating_state.is_updating) return false;
    if (m_updating_state.updating_index >=
        m_updating_state.updating_chunks.size()) {
        return false;
    }
    if (!m_updating_state.in_chunk_pos) {
        m_updating_state.in_chunk_pos = {
            std::get<0>(m_updating_state.bounds.first),
            std::get<0>(m_updating_state.bounds.second)
        };
        return true;
    }
    if (m_updating_state.in_chunk_pos->x ==
            std::get<1>(m_updating_state.bounds.first) ||
        m_updating_state.in_chunk_pos->y ==
            std::get<1>(m_updating_state.bounds.second)) {
        return false;
    }
    if (update_state.x_outer) {
        m_updating_state.in_chunk_pos->y +=
            std::get<2>(m_updating_state.bounds.second);
        if (m_updating_state.in_chunk_pos->y ==
            std::get<1>(m_updating_state.bounds.second)) {
            m_updating_state.in_chunk_pos->y =
                std::get<0>(m_updating_state.bounds.second);
            m_updating_state.in_chunk_pos->x +=
                std::get<2>(m_updating_state.bounds.first);
            if (m_updating_state.in_chunk_pos->x ==
                std::get<1>(m_updating_state.bounds.first)) {
                return false;
            }
        }
    } else {
        m_updating_state.in_chunk_pos->x +=
            std::get<2>(m_updating_state.bounds.first);
        if (m_updating_state.in_chunk_pos->x ==
            std::get<1>(m_updating_state.bounds.first)) {
            m_updating_state.in_chunk_pos->x =
                std::get<0>(m_updating_state.bounds.first);
            m_updating_state.in_chunk_pos->y +=
                std::get<2>(m_updating_state.bounds.second);
            if (m_updating_state.in_chunk_pos->y ==
                std::get<1>(m_updating_state.bounds.second)) {
                return false;
            }
        }
    }
    return true;
}
EPIX_API void Simulation::update_chunk(float delta) {
    if (!m_updating_state.is_updating) return;
    if (m_updating_state.updating_index >=
            m_updating_state.updating_chunks.size() ||
        m_updating_state.updating_index < 0) {
        return;
    }
    while (next_cell()) {
        update_cell(delta);
    }
}
EPIX_API void Simulation::update_cell(float delta) {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_real_distribution<float> dis(-0.4f, 0.4f);
    if (!m_updating_state.is_updating) return;
    if (m_updating_state.updating_index >=
            m_updating_state.updating_chunks.size() ||
        m_updating_state.updating_index < 0) {
        return;
    }
    if (!m_updating_state.in_chunk_pos) return;
    const auto& pos =
        m_updating_state.updating_chunks[m_updating_state.updating_index];
    auto& chunk  = m_chunk_map.get_chunk(pos.x, pos.y);
    const int x_ = pos.x * m_chunk_size + m_updating_state.in_chunk_pos->x;
    const int y_ = pos.y * m_chunk_size + m_updating_state.in_chunk_pos->y;
    epix::world::sand::components::update_cell(*this, x_, y_, delta);
}
EPIX_API Simulation::RaycastResult Simulation::raycast_to(
    int x, int y, int tx, int ty
) {
    if (!valid(x, y)) {
        return RaycastResult{0, x, y, std::nullopt};
    }
    if (x == tx && y == ty) {
        return RaycastResult{0, x, y, std::nullopt};
    }
    int w          = tx - x;
    int h          = ty - y;
    int max        = std::max(std::abs(w), std::abs(h));
    float dx       = (float)w / max;
    float dy       = (float)h / max;
    int last_x     = x;
    int last_y     = y;
    int step_count = 0;
    for (int i = 1; i <= max; i++) {
        int new_x = x + std::round(dx * i);
        int new_y = y + std::round(dy * i);
        if (new_x == last_x && new_y == last_y) {
            continue;
        }
        if (!valid(new_x, new_y) || contain_cell(new_x, new_y)) {
            return RaycastResult{
                step_count, last_x, last_y, std::make_pair(new_x, new_y)
            };
        }
        step_count++;
        last_x = new_x;
        last_y = new_y;
    }
    return RaycastResult{step_count, last_x, last_y, std::nullopt};
}
EPIX_API bool Simulation::collide(int x, int y, int tx, int ty) {
    // if (!valid(x, y) || !valid(tx, ty)) return false;
    // if (!contain_cell(x, y) || !contain_cell(tx, ty)) return false;
    auto [cell, elem]   = get(x, y);
    auto [tcell, telem] = get(tx, ty);
    float dx            = (float)(tx - x) + tcell.inpos.x - cell.inpos.x;
    float dy            = (float)(ty - y) + tcell.inpos.y - cell.inpos.y;
    float dist          = glm::length(glm::vec2(dx, dy));
    float dv_x          = cell.velocity.x - tcell.velocity.x;
    float dv_y          = cell.velocity.y - tcell.velocity.y;
    float v_dot_d       = dv_x * dx + dv_y * dy;
    if (v_dot_d <= 0) return false;
    float m1 = elem.density;
    float m2 = telem.density;
    if (telem.is_solid()) {
        m1 = 0;
    }
    if (telem.is_powder() && !tcell.freefall) {
        m1 *= 0.5f;
    }
    if (telem.is_powder() && elem.is_liquid() && telem.density < elem.density) {
        return true;
    }
    if (elem.is_solid()) {
        m2 = 0;
    }
    if (elem.is_liquid()/*  &&
        telem.grav_type == Element::GravType::SOLID */) {
        dx         = (float)(tx - x);
        dy         = (float)(ty - y);
        bool dir_x = std::abs(dx) > std::abs(dy);
        dx         = dir_x ? (float)(tx - x) : 0;
        dy         = dir_x ? 0 : (float)(ty - y);
    }
    if (m1 == 0 && m2 == 0) return false;
    float restitution = std::max(elem.bouncing, telem.bouncing);
    float j  = -(1 + restitution) * v_dot_d / (m1 + m2);  // impulse scalar
    float jx = j * dx / dist;
    float jy = j * dy / dist;
    cell.velocity.x += jx * m2;
    cell.velocity.y += jy * m2;
    tcell.velocity.x -= jx * m1;
    tcell.velocity.y -= jy * m1;
    float friction = std::sqrt(elem.friction * telem.friction);
    float dot2     = (dv_x * dy - dv_y * dx) / dist;
    float jf       = dot2 / (m1 + m2);
    float jfabs    = std::min(friction * std::abs(j), std::fabs(jf));
    float jfx_mod  = dot2 > 0 ? dy / dist : -dy / dist;
    float jfy_mod  = dot2 > 0 ? -dx / dist : dx / dist;
    float jfx      = jfabs * jfx_mod * 2.0f / 3.0f;
    float jfy      = jfabs * jfy_mod * 2.0f / 3.0f;
    cell.velocity.x -= jfx * m2;
    cell.velocity.y -= jfy * m2;
    tcell.velocity.x += jfx * m1;
    tcell.velocity.y += jfy * m1;
    if (elem.is_liquid() && telem.is_liquid()) {
        auto new_cell_vel  = cell.velocity * 0.55f + tcell.velocity * 0.45f;
        auto new_tcell_vel = cell.velocity * 0.45f + tcell.velocity * 0.55f;
        cell.velocity      = new_cell_vel;
        tcell.velocity     = new_tcell_vel;
    }
    if (!tcell.freefall) {
        tcell.velocity = {0.0f, 0.0f};
    }
    return true;
}

EPIX_API void Simulation::touch(int x, int y) {
    if (!valid(x, y)) return;
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto [cell_x, cell_y]   = to_in_chunk_pos(x, y);
    auto& chunk             = m_chunk_map.get_chunk(chunk_x, chunk_y);
    chunk.touch(cell_x, cell_y);
    if (!chunk.contains(cell_x, cell_y)) return;
    auto& cell = chunk.get(cell_x, cell_y);
    auto& elem = get_elem(x, y);
    if (elem.is_solid()) return;
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    cell.freefall |= dis(gen) <= (elem.awake_rate * elem.awake_rate);
}