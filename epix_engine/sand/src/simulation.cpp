#include <BS_thread_pool.hpp>
#include <numbers>

#include "epix/world/sand.h"

#define EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME 32i32

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
    : cells(width * height, Cell{}), width(width), height(height) {}
EPIX_API Simulation::Chunk::Chunk(const Chunk& other)
    : cells(other.cells), width(other.width), height(other.height) {}
EPIX_API Simulation::Chunk::Chunk(Chunk&& other)
    : cells(std::move(other.cells)), width(other.width), height(other.height) {}
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
    for (auto& each : cells) {
        each.updated = false;
    }
}
EPIX_API void Simulation::Chunk::count_time() { time_since_last_swap++; }
EPIX_API Cell& Simulation::Chunk::get(int x, int y) {
    return cells[y * width + x];
}
EPIX_API const Cell& Simulation::Chunk::get(int x, int y) const {
    return cells[y * width + x];
}
EPIX_API Cell& Simulation::Chunk::create(
    int x, int y, const CellDef& def, ElemRegistry& m_registry
) {
    Cell& cell = get(x, y);
    if (cell) return cell;
    cell.elem_id = def.identifier == CellDef::DefIdentifier::Name
                       ? m_registry.elem_id(def.elem_name)
                       : def.elem_id;
    if (cell.elem_id < 0) {
        return cell;
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
    return cell;
}
EPIX_API void Simulation::Chunk::swap_area() {
    std::swap(updating_area, updating_area_next);
    updating_area_next[0] = width;
    updating_area_next[1] = 0;
    updating_area_next[2] = height;
    updating_area_next[3] = 0;
}
EPIX_API void Simulation::Chunk::remove(int x, int y) {
    assert(x >= 0 && x < width && y >= 0 && y < height);
    get(x, y).elem_id = -1;
}
EPIX_API Simulation::Chunk::operator bool() const {
    return width && height && !cells.empty();
}
EPIX_API bool Simulation::Chunk::operator!() const {
    return !width || !height || cells.empty();
}
EPIX_API bool Simulation::Chunk::is_updated(int x, int y) const {
    return cells[y * width + x].updated;
}
EPIX_API void Simulation::Chunk::touch(int x, int y) {
    assert(x >= 0 && x < width && y >= 0 && y < height);
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
    return x >= 0 && x < width && y >= 0 && y < height &&
           cells[y * width + x].elem_id >= 0;
}

void offset_chunks(Simulation::ChunkMap& chunks, int x_offset, int y_offset) {
    assert(x_offset >= 0 && y_offset >= 0);
    auto& size = chunks.size;
    for (int x = size.x - 1; x >= 0; x--) {
        for (int y = size.y - 1; y >= 0; y--) {
            std::swap(
                chunks.chunks[x][y], chunks.chunks[x + x_offset][y + y_offset]
            );
        }
    }
}

EPIX_API Simulation::ChunkMap::ChunkMap(int chunk_size)
    : chunk_size(chunk_size) {}

EPIX_API void Simulation::ChunkMap::load_chunk(
    int x, int y, const Chunk& chunk
) {
    if (!contains(x, y)) m_chunk_count++;
    if (size == glm::ivec2(0)) {
        size.x = 1;
        size.y = 1;
        origin = glm::ivec2(x, y);
    }
    if (x < origin.x) {
        size.x += origin.x - x;
        origin.x = x;
        offset_chunks(*this, origin.x - x, 0);
    }
    if (y < origin.y) {
        size.y += origin.y - y;
        origin.y = y;
        offset_chunks(*this, 0, origin.y - y);
    }
    if (x >= origin.x + size.x) {
        size.x = x - origin.x + 1;
        chunks.resize(size.x);
    }
    if (y >= origin.y + size.y) {
        size.y = y - origin.y + 1;
        for (int x = 0; x < size.x; x++) {
            chunks[x].resize(size.y);
        }
    }
    chunks[x - origin.x][y - origin.y] =
        std::move(std::make_shared<Chunk>(chunk));
}
EPIX_API void Simulation::ChunkMap::load_chunk(int x, int y, Chunk&& chunk) {
    if (!contains(x, y)) m_chunk_count++;
    if (size == glm::ivec2(0)) {
        size.x = 1;
        size.y = 1;
        origin = glm::ivec2(x, y);
    }
    if (x < origin.x) {
        size.x += origin.x - x;
        origin.x = x;
        offset_chunks(*this, origin.x - x, 0);
    }
    if (y < origin.y) {
        size.y += origin.y - y;
        origin.y = y;
        offset_chunks(*this, 0, origin.y - y);
    }
    if (x >= origin.x + size.x) {
        size.x = x - origin.x + 1;
        chunks.resize(size.x);
    }
    if (y >= origin.y + size.y) {
        size.y = y - origin.y + 1;
        for (int x = 0; x < size.x; x++) {
            chunks[x].resize(size.y);
        }
    }
    chunks[x - origin.x][y - origin.y] =
        std::move(std::make_shared<Chunk>(std::forward<Chunk>(chunk)));
}
EPIX_API void Simulation::ChunkMap::load_chunk(int x, int y) {
    if (!contains(x, y)) m_chunk_count++;
    if (size == glm::ivec2(0)) {
        size.x = 1;
        size.y = 1;
        origin = glm::ivec2(x, y);
        chunks.resize(1);
        chunks[0].resize(1);
    }
    if (x < origin.x) {
        size.x += origin.x - x;
        origin.x = x;
        offset_chunks(*this, origin.x - x, 0);
    }
    if (y < origin.y) {
        size.y += origin.y - y;
        origin.y = y;
        offset_chunks(*this, 0, origin.y - y);
    }
    if (x >= origin.x + size.x) {
        size.x = x - origin.x + 1;
        chunks.resize(size.x);
        for (int x = 0; x < size.x; x++) {
            chunks[x].resize(size.y);
        }
    }
    if (y >= origin.y + size.y) {
        size.y = y - origin.y + 1;
        for (int x = 0; x < size.x; x++) {
            chunks[x].resize(size.y);
        }
    }
    chunks[x - origin.x][y - origin.y] =
        std::move(std::make_shared<Chunk>(chunk_size, chunk_size));
}
EPIX_API bool Simulation::ChunkMap::contains(int x, int y) const {
    return x >= origin.x && x < origin.x + size.x && y >= origin.y &&
           y < origin.y + size.y && chunks[x - origin.x][y - origin.y];
}
EPIX_API Simulation::Chunk& Simulation::ChunkMap::get_chunk(int x, int y) {
    return *chunks[x - origin.x][y - origin.y];
}
EPIX_API const Simulation::Chunk& Simulation::ChunkMap::get_chunk(int x, int y)
    const {
    return *chunks[x - origin.x][y - origin.y];
}
EPIX_API size_t Simulation::ChunkMap::chunk_count() const {
    return m_chunk_count;
}

EPIX_API Simulation::ChunkMap::iterator::iterator(
    ChunkMap* chunks, int x, int y
)
    : chunk_map(chunks), x(x), y(y) {}

EPIX_API Simulation::ChunkMap::iterator&
Simulation::ChunkMap::iterator::operator++() {
    if (x == chunk_map->origin.x + chunk_map->size.x &&
        y == chunk_map->origin.y + chunk_map->size.y) {
        return *this;
    }
    std::tuple<int, int, int> xbounds = {
        chunk_map->iterate_setting.xorder
            ? chunk_map->origin.x
            : chunk_map->origin.x + chunk_map->size.x - 1,
        chunk_map->iterate_setting.xorder
            ? chunk_map->origin.x + chunk_map->size.x
            : chunk_map->origin.x - 1,
        chunk_map->iterate_setting.xorder ? 1 : -1,
    };
    std::tuple<int, int, int> ybounds = {
        chunk_map->iterate_setting.yorder
            ? chunk_map->origin.y
            : chunk_map->origin.y + chunk_map->size.y - 1,
        chunk_map->iterate_setting.yorder
            ? chunk_map->origin.y + chunk_map->size.y
            : chunk_map->origin.y - 1,
        chunk_map->iterate_setting.yorder ? 1 : -1,
    };
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
    x = chunk_map->origin.x + chunk_map->size.x;
    y = chunk_map->origin.y + chunk_map->size.y;
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
    if (x == chunk_map->origin.x + chunk_map->size.x &&
        y == chunk_map->origin.y + chunk_map->size.y) {
        return *this;
    }
    std::tuple<int, int, int> xbounds = {
        chunk_map->iterate_setting.xorder
            ? chunk_map->origin.x
            : chunk_map->origin.x + chunk_map->size.x - 1,
        chunk_map->iterate_setting.xorder
            ? chunk_map->origin.x + chunk_map->size.x
            : chunk_map->origin.x - 1,
        chunk_map->iterate_setting.xorder ? 1 : -1,
    };
    std::tuple<int, int, int> ybounds = {
        chunk_map->iterate_setting.yorder
            ? chunk_map->origin.y
            : chunk_map->origin.y + chunk_map->size.y - 1,
        chunk_map->iterate_setting.yorder
            ? chunk_map->origin.y + chunk_map->size.y
            : chunk_map->origin.y - 1,
        chunk_map->iterate_setting.yorder ? 1 : -1,
    };
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
    x = chunk_map->origin.x + chunk_map->size.x;
    y = chunk_map->origin.y + chunk_map->size.y;
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
    std::tuple<int, int, int> xbounds = {
        iterate_setting.xorder ? origin.x : origin.x + size.x - 1,
        iterate_setting.xorder ? origin.x + size.x : origin.x - 1,
        iterate_setting.xorder ? 1 : -1,
    };
    std::tuple<int, int, int> ybounds = {
        iterate_setting.yorder ? origin.y : origin.y + size.y - 1,
        iterate_setting.yorder ? origin.y + size.y : origin.y - 1,
        iterate_setting.yorder ? 1 : -1,
    };
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
    std::tuple<int, int, int> xbounds = {
        iterate_setting.xorder ? origin.x : origin.x + size.x - 1,
        iterate_setting.xorder ? origin.x + size.x : origin.x - 1,
        iterate_setting.xorder ? 1 : -1,
    };
    std::tuple<int, int, int> ybounds = {
        iterate_setting.yorder ? origin.y : origin.y + size.y - 1,
        iterate_setting.yorder ? origin.y + size.y : origin.y - 1,
        iterate_setting.yorder ? 1 : -1,
    };
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
    return iterator(this, origin.x + size.x, origin.y + size.y);
}
EPIX_API Simulation::ChunkMap::const_iterator Simulation::ChunkMap::end(
) const {
    return const_iterator(this, origin.x + size.x, origin.y + size.y);
}

EPIX_API void Simulation::ChunkMap::reset_updated() {
    for (auto&& [pos, chunk] : *this) {
        chunk.reset_updated();
    }
}
EPIX_API void Simulation::ChunkMap::count_time() {
    for (auto&& [pos, chunk] : *this) {
        chunk.count_time();
        if (chunk.time_since_last_swap > chunk.time_threshold) {
            chunk.time_since_last_swap = 0;
            chunk.swap_area();
        }
        chunk.time_threshold = EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME;
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
      max_travel({chunk_size / 2, chunk_size / 2}),
      m_thread_pool(
          std::make_unique<BS::thread_pool>(std::thread::hardware_concurrency())
      ) {}
EPIX_API Simulation::Simulation(ElemRegistry&& registry, int chunk_size)
    : m_registry(std::move(registry)),
      m_chunk_size(chunk_size),
      m_chunk_map{chunk_size},
      max_travel({chunk_size / 2, chunk_size / 2}),
      m_thread_pool(
          std::make_unique<BS::thread_pool>(std::thread::hardware_concurrency())
      ) {}

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
EPIX_API const Element& Simulation::get_elem(int x, int y) const {
    assert(contain_cell(x, y));
    auto id = get_cell(x, y).elem_id;
    assert(id >= 0);
    return m_registry.get_elem(id);
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
    if (elem.is_liquid()) return;
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
    int final_x             = x_;
    int final_y             = y_;
    if (chunk.is_updated(cell_x, cell_y)) return;
    if (!sim.contain_cell(x_, y_)) return;
    auto [cell, elem] = sim.get(x_, y_);
    auto grav         = sim.get_grav(x_, y_);
    cell.updated      = true;
    if (elem.is_powder()) {
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
            if (!sim.valid(below_x, below_y)) return;
            auto& bcell = sim.get_cell(below_x, below_y);
            if (bcell) {
                auto& belem = sim.get_elem(below_x, below_y);
                if (belem.is_solid()) {
                    return;
                }
                if (belem.is_powder() && !bcell.freefall) {
                    return;
                }
            }
            cell.velocity = sim.get_default_vel(x_, y_);
            cell.freefall = true;
        }
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
                cell.velocity *= 0.9f;
            }
            cell.velocity += grav * delta;
            cell.velocity *= 0.99f;
            cell.inpos += cell.velocity * delta;
        }
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
        auto ncell          = &cell;
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
            auto& tcell =
                sim.get_cell(raycast_result.new_x, raycast_result.new_y);
            std::swap(tcell, *ncell);
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
                    std::swap(*ncell, tcell);
                    final_x = hit_x;
                    final_y = hit_y;
                    ncell   = &tcell;
                    moved   = true;
                }
            }
            if (!moved && glm::length(grav) > 0.0f) {
                if (sim.powder_always_slide ||
                    (sim.valid(hit_x, hit_y) &&
                     !sim.get_cell(hit_x, hit_y).freefall)) {
                    float grav_angle = std::atan2(grav.y, grav.x);
                    glm::ivec2 lb    = {
                        std::round(std::cos(grav_angle - std::numbers::pi / 4)),
                        std::round(std::sin(grav_angle - std::numbers::pi / 4))
                    };
                    glm::ivec2 rb = {
                        std::round(std::cos(grav_angle + std::numbers::pi / 4)),
                        std::round(std::sin(grav_angle + std::numbers::pi / 4))
                    };
                    // try go to left bottom and right bottom
                    float vel_angle =
                        std::atan2(cell.velocity.y, cell.velocity.x);
                    float angle_diff =
                        calculate_angle_diff(cell.velocity, grav);
                    if (std::abs(angle_diff) < std::numbers::pi / 2) {
                        glm::vec2 dirs[2];
                        static thread_local std::random_device rd;
                        static thread_local std::mt19937 gen(rd());
                        static thread_local std::uniform_real_distribution<
                            float>
                            dis(-0.3f, 0.3f);
                        bool lb_first = dis(gen) > 0;
                        if (lb_first) {
                            dirs[0] = lb;
                            dirs[1] = rb;
                        } else {
                            dirs[0] = rb;
                            dirs[1] = lb;
                        }
                        for (auto&& vel : dirs) {
                            int delta_x = vel.x;
                            int delta_y = vel.y;
                            if (delta_x == 0 && delta_y == 0) {
                                continue;
                            }
                            int tx = final_x + delta_x;
                            int ty = final_y + delta_y;
                            if (!sim.valid(tx, ty)) continue;
                            auto& tcell = sim.get_cell(tx, ty);
                            if (!tcell) {
                                std::swap(*ncell, tcell);
                                ncell   = &tcell;
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
                ncell->freefall = false;
                ncell->velocity = {0.0f, 0.0f};
            }
        }
        if (moved) {
            ncell->not_move_count = 0;
            sim.touch(x_ - 1, y_);
            sim.touch(x_ + 1, y_);
            sim.touch(x_, y_ - 1);
            sim.touch(x_, y_ + 1);
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
                auto& bcell             = sim.get_cell(below_x, below_y);
                bool shouldnot_freefall = false;
                if (bcell) {
                    auto& belem = sim.get_elem(below_x, below_y);
                    if (belem.is_solid()) {
                        shouldnot_freefall = true;
                    }
                    if ((belem.is_powder() && !bcell.freefall) ||
                        belem.is_liquid()) {
                        shouldnot_freefall = true;
                    }
                }
                should_freefall |= !shouldnot_freefall;
            }
            if (sim.valid(lb_x, lb_y)) {
                auto& lbcell            = sim.get_cell(lb_x, lb_y);
                bool shouldnot_freefall = false;
                if (lbcell) {
                    auto& lbelem = sim.get_elem(lb_x, lb_y);
                    if (lbelem.is_solid()) {
                        shouldnot_freefall = true;
                    }
                    if ((lbelem.is_powder() && !lbcell.freefall) ||
                        lbelem.is_liquid()) {
                        shouldnot_freefall = true;
                    }
                }
                should_freefall |= !shouldnot_freefall;
            }
            if (sim.valid(rb_x, rb_y)) {
                auto& rbcell            = sim.get_cell(rb_x, rb_y);
                bool shouldnot_freefall = false;
                if (rbcell) {
                    auto& rbelem = sim.get_elem(rb_x, rb_y);
                    if (rbelem.is_solid()) {
                        shouldnot_freefall = true;
                    }
                    if ((rbelem.is_powder() && !rbcell.freefall) ||
                        rbelem.is_liquid()) {
                        shouldnot_freefall = true;
                    }
                }
                should_freefall |= !shouldnot_freefall;
            }
            if (!should_freefall) return;

            cell.velocity = sim.get_default_vel(x_, y_);
            cell.freefall = true;
        }
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
                cell.velocity *= 0.9f;
            }
        }
        cell.velocity += grav * delta;
        cell.velocity += cell.impact;
        cell.impact = {0.0f, 0.0f};
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
        auto ncell = &sim.get_cell(raycast_result.new_x, raycast_result.new_y);
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
            std::swap(cell, *ncell);
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
                    (telem.is_liquid() && telem.density > elem.density) ||
                    telem == elem) {
                    collided = sim.collide(
                        raycast_result.new_x, raycast_result.new_y, hit_x, hit_y
                    );
                } else {
                    std::swap(*ncell, tcell);
                    final_x = hit_x;
                    final_y = hit_y;
                    ncell   = &tcell;
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
                        auto& tcell = sim.get_cell(tx, ty);
                        if (!tcell) {
                            std::swap(*ncell, tcell);
                            ncell = &tcell;
                            ncell->velocity +=
                                idirs[i] * sim.liquid_spread_setting.prefix /
                                delta;
                            final_x = tx;
                            final_y = ty;
                            moved   = true;
                            break;
                        } else {
                            auto& telem = sim.get_elem(tx, ty);
                            if (telem.is_liquid() &&
                                telem.density < elem.density) {
                                std::swap(*ncell, tcell);
                                ncell = &tcell;
                                ncell->velocity +=
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
                            if (sim.valid(
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
                            if (dis(gen) > 0.08f &&
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
                                auto& tcell =
                                    sim.get_cell(res_1.new_x, res_1.new_y);
                                std::swap(*ncell, tcell);
                                final_x = res_1.new_x;
                                final_y = res_1.new_y;
                                ncell   = &tcell;
                                ncell->velocity +=
                                    (inverse_pressure ? -0.3f : 1.0f) *
                                    glm::vec2(idirs[0]) *
                                    sim.liquid_spread_setting.prefix / delta;
                                moved = true;
                            }
                        } else if (res_2.steps) {
                            auto& tcell =
                                sim.get_cell(res_2.new_x, res_2.new_y);
                            std::swap(*ncell, tcell);
                            final_x = res_2.new_x;
                            final_y = res_2.new_y;
                            ncell   = &tcell;
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
            ncell->not_move_count = 0;
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
            sim.touch(final_x - 1, final_y - 1);
            sim.touch(final_x + 1, final_y - 1);
            sim.touch(final_x - 1, final_y + 1);
            sim.touch(final_x + 1, final_y + 1);
            apply_viscosity(sim, cell, final_x, final_y, x_ - 1, y_);
            apply_viscosity(sim, cell, final_x, final_y, x_ + 1, y_);
            apply_viscosity(sim, cell, final_x, final_y, x_, y_ - 1);
            apply_viscosity(sim, cell, final_x, final_y, x_, y_ + 1);
        } else {
            cell.not_move_count++;
            if (cell.not_move_count >= sim.not_moving_threshold(grav) / 5) {
                cell.not_move_count = 0;
                cell.freefall       = false;
                cell.velocity       = {0.0f, 0.0f};
            }
        }
    } else if (elem.is_gas()) {
        grav *= (elem.density - sim.air_density(x_, y_)) / elem.density;
        cell.velocity += grav * delta;
        // grav /= 4.0f; // this is for larger chunk reset time
        cell.velocity -=
            0.1f * glm::length(cell.velocity) * cell.velocity / 20.0f;
        cell.inpos += cell.velocity * delta;
        auto ncell  = &cell;
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
            auto& tcell =
                sim.get_cell(raycast_result.new_x, raycast_result.new_y);
            std::swap(tcell, *ncell);
            ncell   = &tcell;
            final_x = raycast_result.new_x;
            final_y = raycast_result.new_y;
            moved   = true;
        }
        float prefix_vdotgf =
            ncell->velocity.x * grav.x + ncell->velocity.y * grav.y;
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
                    std::swap(*ncell, tcell);
                    ncell   = &tcell;
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
                    auto [tcell, telem] = sim.get(tx, ty);
                    if (!tcell || telem.is_gas()) {
                        std::swap(*ncell, tcell);
                        ncell   = &tcell;
                        final_x = tx;
                        final_y = ty;
                        moved   = true;
                        break;
                    }
                }
            }
        }
        if (moved) {
            ncell->not_move_count = 0;
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
    std::vector<std::pair<int, int>> modres = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    reset_updated();
    update_state.next();
    m_chunk_map.count_time();
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
    std::vector<glm::ivec2> chunks_to_update;
    chunks_to_update.reserve(m_chunk_map.chunk_count());
    m_chunk_map.set_iterate_setting(xorder, yorder, x_outer);
    for (auto&& [pos, chunk] : m_chunk_map) {
        chunks_to_update.push_back(pos);
    }
    for (auto&& [xmod, ymod] : modres) {
        for (auto pos : chunks_to_update) {
            if ((pos.x + xmod) % 2 == 0 && (pos.y + ymod) % 2 == 0) {
                m_thread_pool->submit_task([=]() {
                    auto& chunk = m_chunk_map.get_chunk(pos.x, pos.y);
                    if (!chunk.should_update()) return;
                    std::tuple<int, int, int> xbounds, ybounds;
                    xbounds = {
                        xorder ? chunk.updating_area[0]
                               : chunk.updating_area[1],
                        xorder ? chunk.updating_area[1] + 1
                               : chunk.updating_area[0] - 1,
                        xorder ? 1 : -1
                    };
                    ybounds = {
                        yorder ? chunk.updating_area[2]
                               : chunk.updating_area[3],
                        yorder ? chunk.updating_area[3] + 1
                               : chunk.updating_area[2] - 1,
                        yorder ? 1 : -1
                    };
                    std::tuple<int, int, int> bounds[2] = {xbounds, ybounds};
                    if (!x_outer) {
                        std::swap(bounds[0], bounds[1]);
                    }
                    for (int index1 = std::get<0>(bounds[0]);
                         index1 != std::get<1>(bounds[0]);
                         index1 += std::get<2>(bounds[0])) {
                        for (int index2 = std::get<0>(bounds[1]);
                             index2 != std::get<1>(bounds[1]);
                             index2 += std::get<2>(bounds[1])) {
                            auto x = pos.x * m_chunk_size +
                                     (x_outer ? index1 : index2);
                            auto y = pos.y * m_chunk_size +
                                     (x_outer ? index2 : index1);
                            epix::world::sand::components::update_cell(
                                *this, x, y, delta
                            );
                        }
                    }
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
    return true;
}

EPIX_API void Simulation::touch(int x, int y) {
    if (!valid(x, y)) return;
    auto [chunk_x, chunk_y] = to_chunk_pos(x, y);
    auto [cell_x, cell_y]   = to_in_chunk_pos(x, y);
    auto& chunk             = m_chunk_map.get_chunk(chunk_x, chunk_y);
    chunk.touch(cell_x, cell_y);
    auto& cell = chunk.get(cell_x, cell_y);
    if (!cell) return;
    auto& elem = get_elem(x, y);
    if (elem.is_solid()) return;
    cell.freefall = true;
}