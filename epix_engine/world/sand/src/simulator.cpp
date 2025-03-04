#include <numbers>
#include <ranges>

#include "epix/world/sand.h"

using namespace epix::world::sand;

EPIX_API void Simulator_T::UpdateState::next() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<int> dis(0, 1);
    if (random_state) {
        xorder  = dis(gen);
        yorder  = dis(gen);
        x_outer = dis(gen);
    } else {
        uint8_t state = xorder << 2 | yorder << 1 | x_outer;
        state++;
        xorder  = state & 0b100;
        yorder  = state & 0b010;
        x_outer = state & 0b001;
    }
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
        bool dir_x = std::abs(dx) > std::abs(dy);
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
    if (!tcell.freefall()) {
        tcell.velocity = {0.0f, 0.0f};
    }
    return true;
}
EPIX_API bool Simulator_T::collide(
    World_T* m_world, Particle& part1, Particle& part2, const glm::vec2& dir
) {
    float dx      = dir.x;
    float dy      = dir.y;
    float dist    = glm::length(glm::vec2(dx, dy));
    float dv_x    = part1.velocity.x - part2.velocity.x;
    float dv_y    = part1.velocity.y - part2.velocity.y;
    float v_dot_d = dv_x * dx + dv_y * dy;
    if (v_dot_d <= 0) return false;
    float m1 = part1.elem_id == -1
                   ? 0
                   : m_world->registry().get_elem(part1.elem_id).density;
    float m2 = part2.elem_id == -1
                   ? 0
                   : m_world->registry().get_elem(part2.elem_id).density;
    if (m1 == 0 && m2 == 0) return false;
    float restitution = std::max(
        m1 == 0 ? 0.0f
                : m_world->registry().get_elem(part1.elem_id).restitution,
        m2 == 0 ? 0.0f : m_world->registry().get_elem(part2.elem_id).restitution
    );
    float j  = -(1 + restitution) * v_dot_d / (m1 + m2);  // impulse scalar
    float jx = j * dx / dist;
    float jy = j * dy / dist;
    part1.velocity.x += jx * m2;
    part1.velocity.y += jy * m2;
    part2.velocity.x -= jx * m1;
    part2.velocity.y -= jy * m1;
    float friction = std::sqrt(
        m1 == 0
            ? 0.0f
            : m_world->registry().get_elem(part1.elem_id).friction *
                  (m2 == 0
                       ? 0.0f
                       : m_world->registry().get_elem(part2.elem_id).friction)
    );
    float dot2    = (dv_x * dy - dv_y * dx) / dist;
    float jf      = dot2 / (m1 + m2);
    float jfabs   = std::min(friction * std::abs(j), std::fabs(jf));
    float jfx_mod = dot2 > 0 ? dy / dist : -dy / dist;
    float jfy_mod = dot2 > 0 ? -dx / dist : dx / dist;
    float jfx     = jfabs * jfx_mod * 2.0f / 3.0f;
    float jfy     = jfabs * jfy_mod * 2.0f / 3.0f;
    part1.velocity.x -= jfx * m2;
    part1.velocity.y -= jfy * m2;
    part2.velocity.x += jfx * m1;
    part2.velocity.y += jfy * m1;
    return true;
}
EPIX_API void Simulator_T::touch(World_T* m_world, int x, int y) {
    auto&& [chunk_x, chunk_y] = m_world->to_chunk_pos(x, y);
    auto&& [cell_x, cell_y]   = m_world->in_chunk_pos(x, y);
    if (!m_world->m_chunks.contains(chunk_x, chunk_y)) return;
    auto& chunk = m_world->m_chunks.get(chunk_x, chunk_y);
    chunk.touch(cell_x, cell_y);
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
    auto& cell = m_world->m_chunks.get(chunk_x, chunk_y).get(cell_x, cell_y);
    if (cell.updated()) return;
    auto& elem = m_world->m_registry->get_elem(cell.elem_id);
    if (elem.is_place_holder() || elem.is_solid()) return;
    int final_x      = x_;
    int final_y      = y_;
    auto grav        = m_world->gravity_at(x_, y_);
    float grav_len_s = grav.x * grav.x + grav.y * grav.y;
    float grav_angle = std::atan2(grav.y, grav.x);
    glm::ivec2 below_d, above_d, lb_d, rb_d, left_d, right_d;
    {
        static const float sqrt2 = std::sqrt(2.0f);

        float gs = std::sin(grav_angle);
        float gc = std::cos(grav_angle);
        below_d  = {std::round(gc), std::round(gs)};
        above_d  = {-std::round(gc), -std::round(gs)};
        lb_d     = {std::round(gc - gs), std::round(gs + gc)};
        rb_d     = {std::round(gc + gs), std::round(gs - gc)};
        left_d   = {-std::round(gs), std::round(gc)};
        right_d  = {std::round(gs), -std::round(gc)};
    }
    cell.set_updated(true);
    if (elem.is_powder()) {
        {
            int liquid_count = 0;  // surrounding liquid particle count
            int empty_count  = 0;  // surrounding empty cell count
            float liquid_density =
                0.0f;  // surrounding liquid density in average
            int b_lb_rb_not_freefall = 0;  // surrounding not freefall count
            if (grav_len_s > 0.0f) {
                // below
                if (m_world->valid(x_ + below_d.x, y_ + below_d.y) &&
                    m_world->contains(x_ + below_d.x, y_ + below_d.y)) {
                    auto&& [tcell, telem] =
                        m_world->get(x_ + below_d.x, y_ + below_d.y);
                    if (telem.is_liquid()) {
                        liquid_count++;
                        liquid_density += telem.density;
                    }
                    if (telem.is_powder() && !tcell.freefall()) {
                        b_lb_rb_not_freefall++;
                    }
                } else {
                    empty_count++;
                }
                // above
                if (m_world->valid(x_ + above_d.x, y_ + above_d.y) &&
                    m_world->contains(x_ + above_d.x, y_ + above_d.y)) {
                    auto&& [tcell, telem] =
                        m_world->get(x_ + above_d.x, y_ + above_d.y);
                    if (telem.is_liquid()) {
                        liquid_count++;
                        liquid_density += telem.density;
                    }
                } else {
                    empty_count++;
                }
                // drag liquid (left and right only)
                static constexpr float liquid_drag   = 0.4f;
                static constexpr float vertical_rate = 0.0f;
                // left
                if (m_world->valid(x_ + left_d.x, y_ + left_d.y) &&
                    m_world->contains(x_ + left_d.x, y_ + left_d.y)) {
                    auto&& [tcell, telem] =
                        m_world->get(x_ + left_d.x, y_ + left_d.y);
                    if (telem.is_liquid() && telem.density > elem.density) {
                        liquid_count++;
                        liquid_density += telem.density;
                        glm::vec2 vel =
                            glm::normalize(glm::vec2(left_d)) *
                            glm::dot(tcell.velocity, glm::vec2(left_d));
                        vel = (1 - vertical_rate) * vel +
                              vertical_rate * tcell.velocity;
                        if (glm::length(vel) > glm::length(cell.velocity)) {
                            cell.velocity =
                                liquid_drag * vel +
                                (1.0f - liquid_drag) * cell.velocity;
                        }
                    }
                } else {
                    empty_count++;
                }
                // right
                if (m_world->valid(x_ + right_d.x, y_ + right_d.y) &&
                    m_world->contains(x_ + right_d.x, y_ + right_d.y)) {
                    auto&& [tcell, telem] =
                        m_world->get(x_ + right_d.x, y_ + right_d.y);
                    if (telem.is_liquid() && telem.density > elem.density) {
                        liquid_count++;
                        liquid_density += telem.density;
                        glm::vec2 vel =
                            glm::normalize(glm::vec2(right_d)) *
                            glm::dot(tcell.velocity, glm::vec2(right_d));
                        vel = (1 - vertical_rate) * vel +
                              vertical_rate * tcell.velocity;
                        if (glm::length(vel) > glm::length(cell.velocity)) {
                            cell.velocity =
                                liquid_drag * vel +
                                (1.0f - liquid_drag) * cell.velocity;
                        }
                    }
                } else {
                    empty_count++;
                }
                // left bottom
                if (m_world->valid(x_ + lb_d.x, y_ + lb_d.y) &&
                    m_world->contains(x_ + lb_d.x, y_ + lb_d.y)) {
                    auto&& [tcell, telem] =
                        m_world->get(x_ + lb_d.x, y_ + lb_d.y);
                    if (telem.is_powder() && !tcell.freefall()) {
                        b_lb_rb_not_freefall++;
                    }
                } else {
                    empty_count++;
                }
                // right bottom
                if (m_world->valid(x_ + rb_d.x, y_ + rb_d.y) &&
                    m_world->contains(x_ + rb_d.x, y_ + rb_d.y)) {
                    auto&& [tcell, telem] =
                        m_world->get(x_ + rb_d.x, y_ + rb_d.y);
                    if (telem.is_powder() && !tcell.freefall()) {
                        b_lb_rb_not_freefall++;
                    }
                } else {
                    empty_count++;
                }
            }
            if (b_lb_rb_not_freefall == 3) {
                cell.set_freefall(false);
                cell.velocity = {0.0f, 0.0f};
            }
            if (liquid_count > empty_count) {
                liquid_density /= liquid_count;
                grav *= (elem.density - liquid_density) / elem.density;
                cell.velocity += 0.9f;
            }
        }
        if (!cell.freefall()) {
            if (!m_world->valid(x_ + below_d.x, y_ + below_d.y)) return;
            if (m_world->contains(x_ + below_d.x, y_ + below_d.y)) {
                auto&& [tcell, telem] =
                    m_world->get(x_ + below_d.x, y_ + below_d.y);
                if (telem.is_solid()) return;
                if (telem.is_powder() && !tcell.freefall()) return;
            }
            if (m_world->valid(x_ + above_d.x, y_ + above_d.y) &&
                m_world->contains(x_ + above_d.x, y_ + above_d.y)) {
                auto&& [tcell, telem] =
                    m_world->get(x_ + above_d.x, y_ + above_d.y);
                if (telem.is_powder() && !tcell.freefall()) {
                    tcell.set_freefall(true);
                    tcell.velocity = m_world->default_velocity_at(
                        x_ + above_d.x, y_ + above_d.y
                    );
                    touch(m_world, x_ + above_d.x, y_ + above_d.y);
                    touch(m_world, x_ + above_d.x - 1, y_ + above_d.y);
                    touch(m_world, x_ + above_d.x + 1, y_ + above_d.y);
                    touch(m_world, x_ + above_d.x, y_ + above_d.y - 1);
                    touch(m_world, x_ + above_d.x, y_ + above_d.y + 1);
                }
            }
            cell.velocity = m_world->default_velocity_at(x_, y_);
            cell.set_freefall(true);
        }
        touch(m_world, x_, y_);
        cell.velocity += grav * delta;
        cell.velocity *= 0.99f;
        cell.inpos += cell.velocity * delta;
        int delta_x = std::round(cell.inpos.x);
        int delta_y = std::round(cell.inpos.y);
        if (delta_x == 0 && delta_y == 0) return;
        cell.inpos.x -= delta_x;
        cell.inpos.y -= delta_y;
        if (max_travel) {
            delta_x = std::clamp(delta_x, -max_travel->x, max_travel->x);
            delta_y = std::clamp(delta_y, -max_travel->y, max_travel->y);
        }
        int tx              = x_ + delta_x;
        int ty              = y_ + delta_y;
        bool moved          = false;
        auto raycast_result = raycast_to(m_world, x_, y_, tx, ty);
        if (raycast_result.steps) {
            m_world->swap(x_, y_, raycast_result.new_x, raycast_result.new_y);
            final_x = raycast_result.new_x;
            final_y = raycast_result.new_y;
            moved   = true;
        }
        if (raycast_result.hit) {
            auto&& [hit_x, hit_y]  = raycast_result.hit.value();
            bool blocking_freefall = false;
            bool collided          = false;
            if (m_world->valid(hit_x, hit_y)) {
                auto&& [tcell, telem] = m_world->get(hit_x, hit_y);
                if (telem.is_solid() || telem.is_powder()) {
                    collided = collide(
                        m_world, raycast_result.new_x, raycast_result.new_y,
                        hit_x, hit_y
                    );
                    blocking_freefall = tcell.freefall();
                } else {
                    m_world->swap(final_x, final_y, hit_x, hit_y);
                    final_x = hit_x;
                    final_y = hit_y;
                    moved   = true;
                }
            }
            if (!moved && grav_len_s > 0.0f) {
                if (powder_slide_setting.always_slide ||
                    (m_world->valid(hit_x, hit_y) &&
                     m_world->contains(hit_x, hit_y) &&
                     !m_world->particle_at(hit_x, hit_y).freefall())) {
                    static constexpr auto cal_angle_diff = [](glm::vec2& v1,
                                                              glm::vec2& v2) {
                        float dot = v1.x * v2.x + v1.y * v2.y;
                        float det = v1.x * v2.y - v1.y * v2.x;
                        return std::atan2(det, dot);
                    };
                    float diff = cal_angle_diff(cell.velocity, grav);
                    if (std::abs(diff) < std::numbers::pi / 2) {
                        glm::ivec2 dirs[2];
                        glm::ivec2 idirs[2];
                        static thread_local std::random_device rd;
                        static thread_local std::mt19937 gen(rd());
                        static thread_local std::uniform_real_distribution<
                            float>
                            dis(-1.0f, 1.0f);
                        if (dis(gen) > 0.0f) {
                            dirs[0]  = lb_d;
                            dirs[1]  = rb_d;
                            idirs[0] = left_d;
                            idirs[1] = right_d;
                        } else {
                            dirs[0]  = rb_d;
                            dirs[1]  = lb_d;
                            idirs[0] = right_d;
                            idirs[1] = left_d;
                        }
                        for (int i = 0; i < 2; i++) {
                            auto& dir   = dirs[i];
                            int delta_x = dir.x;
                            int delta_y = dir.y;
                            if (delta_x == 0 && delta_y == 0) {
                                continue;
                            }
                            int tx = final_x + delta_x;
                            int ty = final_y + delta_y;
                            int ux = final_x + idirs[i].x;
                            int uy = final_y + idirs[i].y;
                            if (!m_world->valid(tx, ty)) continue;
                            if (m_world->contains(tx, ty)) {
                                auto&& [tcell, telem] = m_world->get(tx, ty);
                                if (!telem.is_liquid()) continue;
                            }
                            if (m_world->valid(ux, uy) &&
                                !m_world->contains(ux, uy)) {
                                m_world->swap(tx, ty, ux, uy);
                            }
                            m_world->swap(final_x, final_y, tx, ty);
                            auto& ncell = m_world->particle_at(tx, ty);
                            ncell.velocity += glm::vec2(idirs[i] + dir) *
                                              powder_slide_setting.prefix /
                                              delta;
                            final_x = tx;
                            final_y = ty;
                            moved   = true;
                            break;
                        }
                    }
                }
            }
            if (!blocking_freefall && !moved) {
                auto& ncell = m_world->particle_at(final_x, final_y);
                ncell.set_freefall(false);
                ncell.velocity = {0.0f, 0.0f};
            }
        }
        if (moved) {
            auto& ncell          = m_world->particle_at(final_x, final_y);
            ncell.not_move_count = 0;
            touch(m_world, x_ - 1, y_);
            touch(m_world, x_ + 1, y_);
            touch(m_world, x_, y_ - 1);
            touch(m_world, x_, y_ + 1);
            touch(m_world, x_ - 1, y_ - 1);
            touch(m_world, x_ + 1, y_ - 1);
            touch(m_world, x_ - 1, y_ + 1);
            touch(m_world, x_ + 1, y_ + 1);
            touch(m_world, final_x - 1, final_y);
            touch(m_world, final_x + 1, final_y);
            touch(m_world, final_x, final_y - 1);
            touch(m_world, final_x, final_y + 1);
        } else {
            cell.not_move_count++;
            if (cell.not_move_count >= m_world->not_moving_threshold(grav)) {
                cell.not_move_count = 0;
                cell.set_freefall(false);
                cell.velocity = {0.0f, 0.0f};
            }
        }
    } else if (elem.is_liquid()) {
    } else if (elem.is_gas()) {
    }
    if (grav_len_s > 0.0f) {
        auto& chunk = m_world->m_chunks.get(chunk_x, chunk_y);
        chunk.time_threshold =
            std::max(chunk.time_threshold, (int)(12 * 10000 / grav_len_s));
    } else {
        auto& chunk          = m_world->m_chunks.get(chunk_x, chunk_y);
        chunk.time_threshold = std::numeric_limits<int>::max();
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
    for (auto&& [xmod, ymod] : modres) {
        for (auto&& [pos, chunk] : m_world->view()) {
            if ((pos[0] + xmod) % mod != 0 || (pos[1] + ymod) % mod != 0)
                continue;
            if (!chunk.should_update()) continue;
            m_world->m_thread_pool->detach_task([=, &chunk]() {
                chunk.reset_updated();
                int xmin = chunk.updating_area[0];
                int xmax = chunk.updating_area[1];
                int ymin = chunk.updating_area[2];
                int ymax = chunk.updating_area[3];
                std::vector<std::pair<int, int>> cells;
                cells.reserve(chunk.grid.data().size());
                for (auto&& [cell_pos, cell] : chunk.grid.view()) {
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
                chunk.step_time();
            });
        }
        m_world->m_thread_pool->wait();
    }
}