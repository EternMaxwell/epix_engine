#include "epix/world/sand.h"

using namespace epix::world::sand;

EPIX_API Simulator::RaycastResult Simulator::raycast_to(
    int x, int y, int tx, int ty
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
EPIX_API Simulator::RaycastResult Simulator::raycast_to(
    int x, int y, float dx, float dy
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

EPIX_API bool Simulator::collide(int x, int y, int tx, int ty) {
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
EPIX_API bool Simulator::collide(
    Particle& part1, Particle& part2, const glm::vec2& dir
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
EPIX_API void Simulator::touch(int x, int y) {
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