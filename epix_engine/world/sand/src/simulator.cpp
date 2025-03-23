#include <numbers>
#include <ranges>

#include "epix/world/sand.h"

#define EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME 12i32

using namespace epix::world::sand;

EPIX_API Simulator_T::Simulator_T()
    : liquid_spread_setting{.spread_len = 3.0f, .prefix = 0.1f},
      update_state{true, true, true, true},
      max_travel(std::nullopt),
      powder_slide_setting{.always_slide = true, .prefix = 1.0f},
      m_chunk_data() {}

EPIX_API void Simulator_T::assure_chunk(const World_T* world, int x, int y) {
    if (!world->m_chunks.contains(x, y)) return;
    if (!m_chunk_data.contains(x, y)) {
        m_chunk_data.try_emplace(
            x, y, world->m_chunk_size, world->m_chunk_size
        );
    }
}
EPIX_API const epix::utils::grid::extendable_grid<Simulator_T::SimChunkData, 2>&
Simulator_T::chunk_data() const {
    return m_chunk_data;
}

EPIX_API void Simulator_T::UpdateState::next() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<int> dis(0, 1);
    if (random_state) {
        xorder  = dis(gen);
        yorder  = dis(gen);
        x_outer = dis(gen);
    } else {
        uint8_t state = (static_cast<uint8_t>(xorder) << 2) |
                        (static_cast<uint8_t>(yorder) << 1) |
                        static_cast<uint8_t>(x_outer);
        state++;
        xorder  = state & 0b100;
        yorder  = state & 0b010;
        x_outer = state & 0b001;
    }
}

EPIX_API Simulator_T::SimChunkData::SimChunkData(int width, int height)
    : width(width),
      height(height),
      active_area{width, 0, height, 0},
      next_active_area{width, 0, height, 0},
      time_threshold(EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME),
      velocity({width, height}),
      velocity_back({width, height}),
      pressure_back({width, height}),
      temperature_back({width, height}),
      time_since_last_swap(0) {}
EPIX_API Simulator_T::SimChunkData::SimChunkData(const SimChunkData& other)
    : width(other.width),
      height(other.height),
      time_threshold(other.time_threshold),
      velocity(other.velocity),
      velocity_back(other.velocity_back),
      pressure_back(other.pressure_back),
      temperature_back(other.temperature_back),
      time_since_last_swap(other.time_since_last_swap) {
    std::memcpy(active_area, other.active_area, sizeof(active_area));
    std::memcpy(
        next_active_area, other.next_active_area, sizeof(next_active_area)
    );
}
EPIX_API Simulator_T::SimChunkData::SimChunkData(SimChunkData&& other)
    : width(other.width),
      height(other.height),
      time_threshold(other.time_threshold),
      velocity(std::move(other.velocity)),
      velocity_back(std::move(other.velocity_back)),
      pressure_back(std::move(other.pressure_back)),
      temperature_back(std::move(other.temperature_back)),
      time_since_last_swap(other.time_since_last_swap) {
    std::memcpy(active_area, other.active_area, sizeof(active_area));
    std::memcpy(
        next_active_area, other.next_active_area, sizeof(next_active_area)
    );
}
EPIX_API Simulator_T::SimChunkData& Simulator_T::SimChunkData::operator=(
    const SimChunkData& other
) {
    if (this == &other) return *this;
    width                = other.width;
    height               = other.height;
    time_threshold       = other.time_threshold;
    time_since_last_swap = other.time_since_last_swap;
    std::memcpy(active_area, other.active_area, sizeof(active_area));
    std::memcpy(
        next_active_area, other.next_active_area, sizeof(next_active_area)
    );
    velocity         = other.velocity;
    velocity_back    = other.velocity_back;
    pressure_back    = other.pressure_back;
    temperature_back = other.temperature_back;
    return *this;
}
EPIX_API Simulator_T::SimChunkData& Simulator_T::SimChunkData::operator=(
    SimChunkData&& other
) {
    if (this == &other) return *this;
    width                = other.width;
    height               = other.height;
    time_threshold       = other.time_threshold;
    time_since_last_swap = other.time_since_last_swap;
    std::memcpy(active_area, other.active_area, sizeof(active_area));
    std::memcpy(
        next_active_area, other.next_active_area, sizeof(next_active_area)
    );
    velocity         = std::move(other.velocity);
    velocity_back    = std::move(other.velocity_back);
    pressure_back    = std::move(other.pressure_back);
    temperature_back = std::move(other.temperature_back);
    return *this;
}

EPIX_API void Simulator_T::SimChunkData::touch(int x, int y) {
    active_area[0]      = std::min(active_area[0], x);
    active_area[1]      = std::max(active_area[1], x);
    active_area[2]      = std::min(active_area[2], y);
    active_area[3]      = std::max(active_area[3], y);
    next_active_area[0] = std::min(next_active_area[0], x);
    next_active_area[1] = std::max(next_active_area[1], x);
    next_active_area[2] = std::min(next_active_area[2], y);
    next_active_area[3] = std::max(next_active_area[3], y);
}
EPIX_API void Simulator_T::SimChunkData::swap(int chunk_size) {
    time_since_last_swap = 0;
    std::memcpy(active_area, next_active_area, sizeof(active_area));
    next_active_area[0] = chunk_size;
    next_active_area[1] = 0;
    next_active_area[2] = chunk_size;
    next_active_area[3] = 0;
}
EPIX_API void Simulator_T::SimChunkData::swap_maps() {
    std::swap(velocity, velocity_back);
    // velocity_back.fill(glm::vec2{0.0f, 0.0f});
    // pressure_back.fill(0.0f);
    // temperature_back.fill(0.0f);
    velocity_back = velocity;
}
EPIX_API void Simulator_T::SimChunkData::step_time(int chunk_size) {
    time_since_last_swap++;
    if (time_since_last_swap >= time_threshold) {
        swap(chunk_size);
    }
    time_threshold = EPIX_WORLD_SAND_DEFAULT_CHUNK_RESET_TIME;
}
EPIX_API bool Simulator_T::SimChunkData::active() const {
    return active_area[0] <= active_area[1] && active_area[2] <= active_area[3];
}

EPIX_API Simulator_T* Simulator_T::create() { return new Simulator_T(); }
EPIX_API std::unique_ptr<Simulator_T> Simulator_T::create_unique() {
    return std::make_unique<Simulator_T>();
}
EPIX_API std::shared_ptr<Simulator_T> Simulator_T::create_shared() {
    return std::make_shared<Simulator_T>();
}

EPIX_API Simulator_T::RaycastResult Simulator_T::raycast_to(
    const World_T* m_world, int x, int y, int tx, int ty
) {
    if (!m_world->valid(x, y)) {
        return RaycastResult{0, x, y, std::nullopt};
    }
    if (x == tx && y == ty) {
        return RaycastResult{0, x, y, std::nullopt};
    }
    int w          = tx - x;
    int h          = ty - y;
    int max        = std::max(std::abs(w), std::abs(h));
    float dx       = static_cast<float>(w) / max;
    float dy       = static_cast<float>(h) / max;
    int last_x     = x;
    int last_y     = y;
    int step_count = 0;
    for (int i = 1; i <= max; i++) {
        int new_x = x + std::round(dx * i);
        int new_y = y + std::round(dy * i);
        if (new_x == last_x && new_y == last_y) {
            continue;
        }
        if (!m_world->valid(new_x, new_y) || m_world->contains(new_x, new_y)) {
            return RaycastResult{
                step_count, last_x, last_y, std::make_pair(new_x, new_y)
            };
        }
        last_x = new_x;
        last_y = new_y;
        step_count++;
    }
    return RaycastResult{step_count, last_x, last_y, std::nullopt};
}
EPIX_API Simulator_T::RaycastResult Simulator_T::raycast_to(
    const World_T* m_world, int x, int y, float dx, float dy
) {
    if (!m_world->valid(x, y)) {
        return RaycastResult{0, x, y, std::nullopt};
    }
    int max = std::round(std::max(std::abs(dx), std::abs(dy)));
    dx /= max;
    dy /= max;
    int last_x     = x;
    int last_y     = y;
    int step_count = 0;
    for (int i = 1; i <= max; i++) {
        int new_x = x + std::round(dx * i);
        int new_y = y + std::round(dy * i);
        if (new_x == last_x && new_y == last_y) {
            continue;
        }
        if (!m_world->valid(new_x, new_y) || m_world->contains(new_x, new_y)) {
            return RaycastResult{
                step_count, last_x, last_y, std::make_pair(new_x, new_y)
            };
        }
        last_x = new_x;
        last_y = new_y;
        step_count++;
    }
    return RaycastResult{step_count, last_x, last_y, std::nullopt};
}

EPIX_API bool Simulator_T::collide(
    World_T* m_world, int x, int y, int tx, int ty
) {
    auto&& [cell, elem]   = m_world->get(x, y);
    auto&& [tcell, telem] = m_world->get(tx, ty);
    float dx              = (float)(tx - x) + tcell.inpos.x - cell.inpos.x;
    float dy              = (float)(ty - y) + tcell.inpos.y - cell.inpos.y;
    float dist            = glm::length(glm::vec2(dx, dy));
    float dv_x            = cell.velocity.x - tcell.velocity.x;
    float dv_y            = cell.velocity.y - tcell.velocity.y;
    float v_dot_d         = dv_x * dx + dv_y * dy;
    if (v_dot_d <= 0) return false;
    float m1 = elem.density;
    float m2 = telem.density;
    if (telem.is_solid()) {
        m1 = 0;
    }
    if (telem.is_powder() && !tcell.freefall()) {
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
        bool dir_x = std::fabsf(dx) > std::fabsf(dy);
        dx         = dir_x ? (float)(tx - x) : 0;
        dy         = dir_x ? 0 : (float)(ty - y);
    }
    if (m1 == 0 && m2 == 0) return false;
    float restitution = std::max(elem.restitution, telem.restitution);
    float j  = -(1 + restitution) * v_dot_d / (m1 + m2);  // impulse scalar
    float jx = j * dx / dist;
    float jy = j * dy / dist;
    cell.velocity.x += jx * m2;
    cell.velocity.y += jy * m2;
    tcell.velocity.x -= jx * m1;
    tcell.velocity.y -= jy * m1;
    float friction = std::sqrtf(elem.friction * telem.friction);
    float dot2     = (dv_x * dy - dv_y * dx) / dist;
    float jf       = dot2 / (m1 + m2);
    float jfabs    = std::min(friction * std::fabsf(j), std::fabsf(jf));
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
    if (!tcell.freefall()) {
        tcell.velocity = {0.0f, 0.0f};
    }
    return true;
}
EPIX_API bool Simulator_T::collide(
    World_T* m_world, Particle& part1, Particle& part2, const glm::vec2& dir
) {
    auto& elem1   = m_world->registry().get_elem(part1.elem_id);
    auto& elem2   = m_world->registry().get_elem(part2.elem_id);
    float dx      = dir.x;
    float dy      = dir.y;
    float dist    = glm::length(dir);
    float dv_x    = part1.velocity.x - part2.velocity.x;
    float dv_y    = part1.velocity.y - part2.velocity.y;
    float v_dot_d = dv_x * dx + dv_y * dy;
    if (v_dot_d <= 0) return false;
    float m1 = elem1.density;
    float m2 = elem2.density;
    if (elem2.is_solid()) {
        m1 = 0;
    }
    if (elem1.is_solid()) {
        m2 = 0;
    }
    if (elem2.is_powder() && !part2.freefall()) {
        m1 *= 0.5f;
    }
    if (elem2.is_powder() && elem1.is_liquid() &&
        elem2.density < elem1.density) {
        return true;
    }
    if (elem1.is_liquid()/*  &&
        elem2.grav_type == Element::GravType::SOLID */) {
        dx         = dir.x;
        dy         = dir.y;
        bool dir_x = std::fabsf(dx) > std::fabsf(dy);
        dx         = dir_x ? dir.x : 0;
        dy         = dir_x ? 0 : dir.y;
    }
    if (m1 == 0 && m2 == 0) return false;
    float restitution = std::max(elem1.restitution, elem2.restitution);
    float j  = -(1 + restitution) * v_dot_d / (m1 + m2);  // impulse scalar
    float jx = j * dx / dist;
    float jy = j * dy / dist;
    part1.velocity.x += jx * m2;
    part1.velocity.y += jy * m2;
    part2.velocity.x -= jx * m1;
    part2.velocity.y -= jy * m1;
    float friction = std::sqrtf(elem1.friction * elem2.friction);
    float dot2     = (dv_x * dy - dv_y * dx) / dist;
    float jf       = dot2 / (m1 + m2);
    float jfabs    = std::min(friction * std::fabsf(j), std::fabsf(jf));
    float jfx_mod  = dot2 > 0 ? dy / dist : -dy / dist;
    float jfy_mod  = dot2 > 0 ? -dx / dist : dx / dist;
    float jfx      = jfabs * jfx_mod * 2.0f / 3.0f;
    float jfy      = jfabs * jfy_mod * 2.0f / 3.0f;
    part1.velocity.x -= jfx * m2;
    part1.velocity.y -= jfy * m2;
    part2.velocity.x += jfx * m1;
    part2.velocity.y += jfy * m1;
    if (!part2.freefall()) {
        part2.velocity = {0.0f, 0.0f};
    }
    return true;
}
EPIX_API void Simulator_T::touch(World_T* m_world, int x, int y) {
    auto&& [chunk_x, chunk_y] = m_world->to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = m_world->in_chunk_pos(x, y);
    if (!m_world->m_chunks.contains(chunk_x, chunk_y)) return;
    assure_chunk(m_world, chunk_x, chunk_y);
    auto& chunk      = m_world->m_chunks.get(chunk_x, chunk_y);
    auto& chunk_data = m_chunk_data.get(chunk_x, chunk_y);
    chunk_data.touch(cell_x, cell_y);
    if (!chunk.contains(cell_x, cell_y)) return;
    auto& cell = chunk.get(cell_x, cell_y);
    auto& elem = m_world->m_registry->get_elem(cell.elem_id);
    if (elem.is_solid()) return;
    if (cell.freefall()) return;
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    cell.set_freefall(dis(gen) <= (elem.awake_rate * elem.awake_rate));
}

EPIX_API void Simulator_T::insert(
    World_T* m_world, int x, int y, Particle&& cell
) {
    auto&& [chunk_x, chunk_y] = m_world->to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = m_world->in_chunk_pos(x, y);
    if (!m_world->m_chunks.contains(chunk_x, chunk_y)) return;
    assure_chunk(m_world, chunk_x, chunk_y);
    auto& chunk = m_world->m_chunks.get(chunk_x, chunk_y);
    if (chunk.contains(cell_x, cell_y)) return;
    chunk.insert(cell_x, cell_y, std::move(cell));
    touch(m_world, x, y);
    touch(m_world, x + 1, y);
    touch(m_world, x - 1, y);
    touch(m_world, x, y + 1);
    touch(m_world, x, y - 1);
}
EPIX_API void Simulator_T::remove(World_T* m_world, int x, int y) {
    auto&& [chunk_x, chunk_y] = m_world->to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = m_world->in_chunk_pos(x, y);
    if (!m_world->m_chunks.contains(chunk_x, chunk_y)) return;
    assure_chunk(m_world, chunk_x, chunk_y);
    auto& chunk = m_world->m_chunks.get(chunk_x, chunk_y);
    if (!chunk.contains(cell_x, cell_y)) return;
    chunk.remove(cell_x, cell_y);
    touch(m_world, x, y);
    touch(m_world, x + 1, y);
    touch(m_world, x - 1, y);
    touch(m_world, x, y + 1);
    touch(m_world, x, y - 1);
}

EPIX_API void Simulator_T::read_velocity(
    const World_T* m_world, int x, int y, glm::vec2* velocity
) const {
    auto&& [chunk_x, chunk_y] = m_world->to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = m_world->in_chunk_pos(x, y);
    if (!m_chunk_data.contains(chunk_x, chunk_y)) return;
    auto& chunk_data = m_chunk_data.get(chunk_x, chunk_y);
    *velocity        = chunk_data.velocity.get(cell_x, cell_y);
}
EPIX_API void Simulator_T::write_velocity(
    World_T* m_world, int x, int y, const glm::vec2& velocity
) {
    auto&& [chunk_x, chunk_y] = m_world->to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = m_world->in_chunk_pos(x, y);
    if (!m_chunk_data.contains(chunk_x, chunk_y)) return;
    assure_chunk(m_world, chunk_x, chunk_y);
    auto& chunk_data = m_chunk_data.get(chunk_x, chunk_y);
    chunk_data.velocity_back.get(cell_x, cell_y) = velocity;
}

EPIX_API void Simulator_T::read_pressure(
    const World_T* m_world, int x, int y, float* pressure
) const {
    auto&& [chunk_x, chunk_y] = m_world->to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = m_world->in_chunk_pos(x, y);
    if (!m_world->m_chunks.contains(chunk_x, chunk_y)) return;
    auto& chunk = m_world->m_chunks.get(chunk_x, chunk_y);
    *pressure   = chunk.pressure().get(cell_x, cell_y);
}
EPIX_API void Simulator_T::write_pressure(
    World_T* m_world, int x, int y, float pressure
) {
    auto&& [chunk_x, chunk_y] = m_world->to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = m_world->in_chunk_pos(x, y);
    if (!m_chunk_data.contains(chunk_x, chunk_y)) return;
    assure_chunk(m_world, chunk_x, chunk_y);
    auto& chunk_data = m_chunk_data.get(chunk_x, chunk_y);
    chunk_data.pressure_back.get(cell_x, cell_y) = pressure;
}

EPIX_API void Simulator_T::read_temperature(
    const World_T* m_world, int x, int y, float* temperature
) const {
    auto&& [chunk_x, chunk_y] = m_world->to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = m_world->in_chunk_pos(x, y);
    if (!m_world->m_chunks.contains(chunk_x, chunk_y)) return;
    auto& chunk  = m_world->m_chunks.get(chunk_x, chunk_y);
    *temperature = chunk.temperature().get(cell_x, cell_y);
}
EPIX_API void Simulator_T::write_temperature(
    World_T* m_world, int x, int y, float temperature
) {
    auto&& [chunk_x, chunk_y] = m_world->to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = m_world->in_chunk_pos(x, y);
    if (!m_chunk_data.contains(chunk_x, chunk_y)) return;
    assure_chunk(m_world, chunk_x, chunk_y);
    auto& chunk_data = m_chunk_data.get(chunk_x, chunk_y);
    chunk_data.temperature_back.get(cell_x, cell_y) = temperature;
}

EPIX_API void Simulator_T::apply_viscosity(
    World_T* m_world, Particle& cell, int x, int y, int tx, int ty
) {
    if (!m_world->valid(tx, ty) || !m_world->contains(tx, ty)) return;
    auto&& [tcell, telem] = m_world->get(tx, ty);
    if (!telem.is_liquid()) return;
    static constexpr float factor = 0.003f;
    tcell.velocity += factor * cell.velocity - factor * tcell.velocity;
}
EPIX_API void Simulator_T::step_particle(
    World_T* m_world, int x_, int y_, float delta
) {
    // it should be guaranteed that the world contains this particle before
    // calling this function
    auto [chunk_x, chunk_y] = m_world->to_chunk_pos(x_, y_);
    auto [cell_x, cell_y]   = m_world->in_chunk_pos(x_, y_);
    auto& chunk             = m_world->m_chunks.get(chunk_x, chunk_y);
    if (!chunk.contains(cell_x, cell_y)) return;
    auto* cell = &chunk.get(cell_x, cell_y);
    if (cell->updated()) return;
    auto& elem = m_world->m_registry->get_elem(cell->elem_id);
    if (elem.is_place_holder() || elem.is_solid()) return;
    int final_x = x_;
    int final_y = y_;
    auto grav   = m_world->gravity_at(x_, y_);
    {
        // set gravity value to effective gravity
        if (elem.is_powder()) {
            const auto checks = std::array<glm::ivec2, 8>{
                {glm::ivec2{0, 1}, glm::ivec2{0, -1}, glm::ivec2{1, 0},
                 glm::ivec2{-1, 0}, glm::ivec2{1, 1}, glm::ivec2{-1, 1},
                 glm::ivec2{1, -1}, glm::ivec2{-1, -1}}
            };
            int empty_count      = 0;
            int liquid_count     = 0;
            float liquid_density = 0.0f;
            for (const auto& check : checks) {
                if (!m_world->valid(x_ + check.x, y_ + check.y)) {
                    continue;
                }
                if (!m_world->contains(x_ + check.x, y_ + check.y)) {
                    empty_count++;
                    continue;
                }
                auto&& [check_cell, check_elem] =
                    m_world->get(x_ + check.x, y_ + check.y);
                if (check_elem.is_liquid()) {
                    liquid_count++;
                    liquid_density += check_elem.density;
                }
            }
            liquid_density /= liquid_count;
            if (liquid_count > empty_count) {
                grav = m_world->gravity_at(x_, y_) *
                       (1 - elem.density / liquid_density);
            }
        }
    }
    float grav_len_s      = grav.x * grav.x + grav.y * grav.y;
    float grav_len        = std::sqrtf(grav_len_s);
    float grav_angle      = std::atan2f(grav.y, grav.x);
    static auto cal_angle = [](const glm::vec2& a,
                               const glm::vec2& b) -> float {
        float dot = a.x * b.x + a.y * b.y;
        float det = a.x * b.y - a.y * b.x;
        return std::atan2f(det, dot);
    };
    glm::vec2 below_d, above_d, lb_d, rb_d, left_d, right_d, ra_d, la_d;
    glm::ivec2 ibelow_d, iabove_d, ilb_d, irb_d, ileft_d, iright_d, ira_d,
        ila_d;
    {
        static const float sqrt2 = std::sqrtf(2.0f);

        float gs = std::sinf(grav_angle);
        float gc = std::cosf(grav_angle);
        below_d  = {gc, gs};
        above_d  = {-gc, -gs};
        left_d   = {gs, -gc};
        right_d  = {-gs, gc};
        lb_d     = below_d + left_d;
        rb_d     = below_d + right_d;
        la_d     = above_d + left_d;
        ra_d     = above_d + right_d;
        ibelow_d = {
            static_cast<int>(std::roundf(below_d.x)),
            static_cast<int>(std::roundf(below_d.y))
        };
        iabove_d = {
            static_cast<int>(std::roundf(above_d.x)),
            static_cast<int>(std::roundf(above_d.y))
        };
        ileft_d = {
            static_cast<int>(std::roundf(left_d.x)),
            static_cast<int>(std::roundf(left_d.y))
        };
        iright_d = {
            static_cast<int>(std::roundf(right_d.x)),
            static_cast<int>(std::roundf(right_d.y))
        };
        ilb_d = {
            static_cast<int>(std::roundf(lb_d.x)),
            static_cast<int>(std::roundf(lb_d.y))
        };
        irb_d = {
            static_cast<int>(std::roundf(rb_d.x)),
            static_cast<int>(std::roundf(rb_d.y))
        };
        ira_d = {
            static_cast<int>(std::roundf(ra_d.x)),
            static_cast<int>(std::roundf(ra_d.y))
        };
        ila_d = {
            static_cast<int>(std::roundf(la_d.x)),
            static_cast<int>(std::roundf(la_d.y))
        };
    }
    cell->set_updated(true);
    if (elem.is_powder()) {
        if (!cell->freefall()) {
            if (m_world->valid(ibelow_d.x + x_, ibelow_d.y + y_)) {
                if (!m_world->contains(ibelow_d.x + x_, ibelow_d.y + y_)) {
                    cell->set_freefall(true);
                } else {
                    auto&& [below_cell, below_elem] =
                        m_world->get(ibelow_d.x + x_, ibelow_d.y + y_);
                    if (!(below_elem.is_solid() ||
                          (below_elem.is_powder() && !below_cell.freefall()))) {
                        cell->set_freefall(true);
                    }
                }
            }
            if (cell->freefall()) {
                // this cell is now set to freefall, try set the above one as
                // well.
                if (m_world->valid(iabove_d.x + x_, iabove_d.y + y_) &&
                    m_world->contains(iabove_d.x + x_, iabove_d.y + y_)) {
                    {
                        auto&& [above_cell, above_elem] =
                            m_world->get(iabove_d.x + x_, iabove_d.y + y_);
                        if (above_elem.is_liquid() || above_elem.is_gas() ||
                            above_elem.is_powder()) {
                            above_cell.set_freefall(true);
                            touch(m_world, iabove_d.x + x_, iabove_d.y + y_);
                            touch(
                                m_world, x_ + iabove_d.x + iabove_d.x,
                                y_ + iabove_d.y + iabove_d.y
                            );
                        }
                    }
                }
                // diagonal above
                if (m_world->valid(ila_d.x + x_, ila_d.y + y_) &&
                    m_world->contains(ila_d.x + x_, ila_d.y + y_)) {
                    auto&& [above_cell, above_elem] =
                        m_world->get(ila_d.x + x_, ila_d.y + y_);
                    if (above_elem.is_liquid() || above_elem.is_gas() ||
                        above_elem.is_powder()) {
                        touch(m_world, ila_d.x + x_, ila_d.y + y_);
                    }
                }
                if (m_world->valid(ira_d.x + x_, ira_d.y + y_) &&
                    m_world->contains(ira_d.x + x_, ira_d.y + y_)) {
                    auto&& [above_cell, above_elem] =
                        m_world->get(ira_d.x + x_, ira_d.y + y_);
                    if (above_elem.is_liquid() || above_elem.is_gas() ||
                        above_elem.is_powder()) {
                        touch(m_world, ira_d.x + x_, ira_d.y + y_);
                    }
                }
            }
        }
        if (cell->freefall()) {
            touch(m_world, x_, y_);
            float angle_diff = cal_angle(cell->velocity, grav);
            cell->inpos += cell->velocity * delta + 0.5f * grav * delta * delta;
            cell->velocity += grav * delta;
            cell->velocity *= 0.99f;
            int delta_x = static_cast<int>((cell->inpos.x));
            int delta_y = static_cast<int>((cell->inpos.y));
            cell->inpos.x -= delta_x;
            cell->inpos.y -= delta_y;
            if (max_travel.has_value()) {
                delta_x = std::clamp(delta_x, -max_travel->x, max_travel->x);
                delta_y = std::clamp(delta_y, -max_travel->y, max_travel->y);
            }
            auto raycast_result =
                raycast_to(m_world, x_, y_, x_ + delta_x, y_ + delta_y);
            if (raycast_result.steps) {
                m_world->swap(
                    x_, y_, raycast_result.new_x, raycast_result.new_y
                );
                final_x = raycast_result.new_x;
                final_y = raycast_result.new_y;
                cell    = &m_world->particle_at(final_x, final_y);
            }
            // if (raycast_result.hit) {
            //     auto [hit_x, hit_y] = *raycast_result.hit;
            //     if (m_world->valid(hit_x, hit_y)) {
            //         auto [hit_chunk_x, hit_chunk_y] =
            //             m_world->to_chunk_pos(hit_x, hit_y);
            //         if (hit_chunk_x == chunk_x && hit_chunk_y == chunk_y) {
            //             step_particle(m_world, hit_x, hit_y, delta);
            //             raycast_result = raycast_to(
            //                 m_world, x_, y_, x_ + delta_x, y_ + delta_y
            //             );
            //         }
            //     }
            // }
            if (raycast_result.hit) {
                if (!m_world->valid(
                        raycast_result.hit->first, raycast_result.hit->second
                    )) {
                    cell->velocity = {0.0f, 0.0f};
                    cell->inpos *= 0.3f;
                } else if (m_world->contains(
                               raycast_result.hit->first,
                               raycast_result.hit->second
                           )) {
                    auto&& [tcell, telem] = m_world->get(
                        raycast_result.hit->first, raycast_result.hit->second
                    );
                    if (!telem.is_gas() && !telem.is_liquid()) {
                        collide(
                            m_world, *cell, tcell,
                            glm::vec2(
                                raycast_result.hit->first - final_x,
                                raycast_result.hit->second - final_y
                            ) + tcell.inpos -
                                cell->inpos
                        );
                        // collide(
                        //     m_world, final_x, final_y,
                        //     raycast_result.hit->first,
                        //     raycast_result.hit->second
                        // );
                    } else {
                        m_world->swap(
                            final_x, final_y, raycast_result.hit->first,
                            raycast_result.hit->second
                        );
                        final_x = raycast_result.hit->first;
                        final_y = raycast_result.hit->second;
                        cell    = &m_world->particle_at(final_x, final_y);
                    }
                }
                if (final_x == x_ && final_y == y_ &&
                    std::fabsf(angle_diff) < std::numbers::pi / 2) {
                    {
                        static thread_local std::random_device rd;
                        static thread_local std::mt19937 gen(rd());
                        static thread_local std::uniform_real_distribution<
                            float>
                            dis(-0.3f, 0.3f);
                        float angle     = angle_diff + dis(gen);
                        const auto dirs = std::array<glm::ivec2, 2>{
                            angle >= 0 ? ilb_d : irb_d,
                            angle >= 0 ? irb_d : ilb_d
                        };
                        const auto dirfs = std::array<glm::vec2, 2>{
                            angle >= 0 ? lb_d : rb_d, angle >= 0 ? rb_d : lb_d
                        };
                        const auto dirf_fixs = std::array<glm::vec2, 2>{
                            angle >= 0 ? left_d : right_d,
                            angle >= 0 ? right_d : left_d
                        };
                        for (auto i = 0; i < 2; i++) {
                            auto& dir      = dirs[i];
                            auto& dirf     = dirfs[i];
                            auto& dirf_fix = dirf_fixs[i];
                            if (m_world->valid(x_ + dir.x, y_ + dir.y)) {
                                if (!m_world->contains(
                                        x_ + dir.x, y_ + dir.y
                                    )) {
                                    m_world->swap(
                                        x_, y_, x_ + dir.x, y_ + dir.y
                                    );
                                    final_x = x_ + dir.x;
                                    final_y = y_ + dir.y;
                                    cell =
                                        &m_world->particle_at(final_x, final_y);
                                } else {
                                    auto&& [tcell, telem] =
                                        m_world->get(x_ + dir.x, y_ + dir.y);
                                    if (!telem.is_solid() &&
                                        !telem.is_powder()) {
                                        m_world->swap(
                                            x_, y_, x_ + dir.x, y_ + dir.y
                                        );
                                        final_x = x_ + dir.x;
                                        final_y = y_ + dir.y;
                                        cell    = &m_world->particle_at(
                                            final_x, final_y
                                        );
                                    }
                                }
                            }
                            if (final_x != x_ || final_y != y_) {
                                cell->velocity += (dirf + dirf_fix) * delta *
                                                  grav_len *
                                                  powder_slide_setting.prefix;
                                break;
                            }
                        }
                    }
                    if (final_x == x_ && final_y == y_) {  // still not moved
                        bool set_not_freefall = false;
                        if (!m_world->valid(
                                final_x + ibelow_d.x, final_y + ibelow_d.y
                            )) {
                            set_not_freefall = true;
                        } else if (m_world->contains(
                                       final_x + ibelow_d.x,
                                       final_y + ibelow_d.y
                                   )) {
                            auto&& [below_cell, below_elem] = m_world->get(
                                final_x + ibelow_d.x, final_y + ibelow_d.y
                            );
                            if (below_elem.is_solid() ||
                                (below_elem.is_powder() &&
                                 !below_cell.freefall())) {
                                set_not_freefall = true;
                            }
                        }
                        if (set_not_freefall) {
                            cell->velocity = {0.0f, 0.0f};
                            cell->inpos *= 0.3f;
                            cell->set_freefall(false);
                        }
                    }
                }
            }
        }
        if (final_x != x_ || final_y != y_) {
            cell->not_move_count = 0;
            touch(m_world, x_ + 1, y_);
            touch(m_world, x_ - 1, y_);
            touch(m_world, x_, y_ + 1);
            touch(m_world, x_, y_ - 1);
            touch(m_world, x_ + 1, y_ + 1);
            touch(m_world, x_ - 1, y_ + 1);
            touch(m_world, x_ + 1, y_ - 1);
            touch(m_world, x_ - 1, y_ - 1);
            touch(m_world, final_x + 1, final_y);
            touch(m_world, final_x - 1, final_y);
            touch(m_world, final_x, final_y + 1);
            touch(m_world, final_x, final_y - 1);
        } else {
            cell->not_move_count++;
        }
    } else if (elem.is_liquid()) {
    } else if (elem.is_gas()) {
    }
    if (grav_len_s > 0.0f) {
        auto& chunk = m_chunk_data.get(chunk_x, chunk_y);
        chunk.time_threshold =
            std::max(chunk.time_threshold, (uint32_t)(12 * 10000 / grav_len_s));
    } else {
        auto& chunk          = m_chunk_data.get(chunk_x, chunk_y);
        chunk.time_threshold = std::numeric_limits<uint32_t>::max();
    }
}
EPIX_API void Simulator_T::step(World_T* m_world, float delta) {
    std::vector<std::pair<int, int>> modres;
    int mod = 3;
    modres.reserve(mod * mod);
    update_state.next();
    if (update_state.x_outer) {
        for (int ix = update_state.xorder ? 0 : mod - 1;
             ix != (update_state.xorder ? mod : -1);
             ix += update_state.xorder ? 1 : -1) {
            for (int iy = update_state.yorder ? 0 : mod - 1;
                 iy != (update_state.yorder ? mod : -1);
                 iy += update_state.yorder ? 1 : -1) {
                modres.emplace_back(ix, iy);
            }
        }
    } else {
        for (int iy = update_state.yorder ? 0 : mod - 1;
             iy != (update_state.yorder ? mod : -1);
             iy += update_state.yorder ? 1 : -1) {
            for (int ix = update_state.xorder ? 0 : mod - 1;
                 ix != (update_state.xorder ? mod : -1);
                 ix += update_state.xorder ? 1 : -1) {
                modres.emplace_back(ix, iy);
            }
        }
    }
    // to make sure the references are valid:
    m_chunk_data.reserve(m_world->m_chunks.count());
    max_travel = {m_world->m_chunk_size, m_world->m_chunk_size};
    for (auto&& [xmod, ymod] : modres) {
        for (auto&& [pos, chunk] : m_world->view()) {
            if ((pos[0] + xmod) % mod != 0 || (pos[1] + ymod) % mod != 0)
                continue;
            if (!m_chunk_data.contains(pos[0], pos[1])) {
                m_chunk_data.emplace(
                    pos[0], pos[1], m_world->m_chunk_size, m_world->m_chunk_size
                );
            }
            auto& chunk_data = m_chunk_data.get(pos[0], pos[1]);
            if (!chunk_data.active()) continue;
            m_world->m_thread_pool->detach_task([=, &chunk, &chunk_data]() {
                chunk.reset_updated();
                int xmin = chunk_data.active_area[0];
                int xmax = chunk_data.active_area[1];
                int ymin = chunk_data.active_area[2];
                int ymax = chunk_data.active_area[3];
                static thread_local std::vector<std::pair<int, int>> cells;
                cells.reserve(chunk.count());
                for (auto&& [cell_pos, cell] : chunk.view()) {
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
                    auto x = pos[0] * m_world->m_chunk_size + cx;
                    auto y = pos[1] * m_world->m_chunk_size + cy;
                    step_particle(m_world, x, y, delta);
                }
                cells.clear();
                chunk_data.step_time(m_world->m_chunk_size);
            });
        }
        m_world->m_thread_pool->wait();
    }
    // step_maps(m_world, delta);
}
EPIX_API void Simulator_T::step_maps(World_T* m_world, float delta) {
    m_chunk_data.reserve(m_world->m_chunks.count());
    // currently only velocity
    static const float factor = std::powf(0.95f, delta / 0.016f);
    for (auto&& [pos, chunk] : m_world->view()) {
        if (!m_chunk_data.contains(pos[0], pos[1])) {
            m_chunk_data.emplace(
                pos[0], pos[1], m_world->m_chunk_size, m_world->m_chunk_size
            );
        }
        auto& chunk_data = m_chunk_data.get(pos[0], pos[1]);
        m_world->m_thread_pool->detach_task([&]() {
            for (auto&& [cell_pos, cell] : chunk.view()) {
                auto x = pos[0] * m_world->m_chunk_size + cell_pos[0];
                auto y = pos[1] * m_world->m_chunk_size + cell_pos[1];
                glm::vec2 velocity;
                read_velocity(m_world, x, y, &velocity);
                velocity *= factor;
                write_velocity(m_world, x, y, velocity);
            }
        });
    }
    m_world->m_thread_pool->wait();
    // swap maps
    for (auto&& [pos, chunk] : m_world->view()) {
        if (!m_chunk_data.contains(pos[0], pos[1])) {
            m_chunk_data.emplace(
                pos[0], pos[1], m_world->m_chunk_size, m_world->m_chunk_size
            );
        }
        auto& chunk_data = m_chunk_data.get(pos[0], pos[1]);
        m_world->m_thread_pool->detach_task([&]() {
            chunk_data.swap_maps();
            std::swap(chunk_data.pressure_back, chunk.pressure());
            std::swap(chunk_data.temperature_back, chunk.temperature());
            chunk_data.pressure_back    = chunk.pressure();
            chunk_data.temperature_back = chunk.temperature();
        });
    }
    m_world->m_thread_pool->wait();
}