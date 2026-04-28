module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <utility>
#include <vector>
#endif
#include <spdlog/spdlog.h>

module epix.extension.fallingsand;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.tasks;

namespace epix::ext::fallingsand {

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation — private helpers
// ──────────────────────────────────────────────────────────────────────────────

SandSimulation::CellState SandSimulation::cell_state(std::int64_t x, std::int64_t y) const {
    auto cell = get_cell<Element>({x, y});
    if (cell.has_value()) return CellState::Occupied;
    return std::visit(
        [](auto&& error) -> CellState {
            using E = std::remove_cvref_t<decltype(error)>;
            if constexpr (std::is_same_v<E, grid::LayerError>) {
                return error == grid::LayerError::EmptyCell ? CellState::EmptyInChunk : CellState::Blocked;
            }
            return CellState::Blocked;
        },
        cell.error());
}

bool SandSimulation::has_cell(std::int64_t x, std::int64_t y) const { return cell_state(x, y) == CellState::Occupied; }

bool SandSimulation::set_cell(std::int64_t x, std::int64_t y, Element value) {
    return insert_cell({x, y}, std::move(value)).has_value();
}

bool SandSimulation::clear_cell(std::int64_t x, std::int64_t y) { return remove_cell<Element>({x, y}).has_value(); }

bool SandSimulation::move_cell(std::int64_t fx, std::int64_t fy, std::int64_t tx, std::int64_t ty) {
    if (cell_state(tx, ty) != CellState::EmptyInChunk) return false;
    auto from = get_cell<Element>({fx, fy});
    if (!from.has_value()) return false;
    Element moved = from->get();
    if (!remove_cell<Element>({fx, fy}).has_value()) return false;
    if (!insert_cell({tx, ty}, std::move(moved)).has_value()) return false;
    return true;
}

bool SandSimulation::swap_cells(std::int64_t fx, std::int64_t fy, std::int64_t tx, std::int64_t ty) {
    auto fs = cell_state(fx, fy);
    auto ts = cell_state(tx, ty);
    if (fs == CellState::Blocked || ts == CellState::Blocked) return false;
    bool from_occ = (fs == CellState::Occupied);
    bool to_occ   = (ts == CellState::Occupied);
    if (!from_occ && !to_occ) return false;
    if (from_occ && to_occ) {
        Element fc = get_cell<Element>({fx, fy})->get();
        Element tc = get_cell<Element>({tx, ty})->get();
        (void)remove_cell<Element>({fx, fy});
        (void)remove_cell<Element>({tx, ty});
        (void)insert_cell({fx, fy}, std::move(tc));
        (void)insert_cell({tx, ty}, std::move(fc));
        return true;
    }
    if (from_occ) {
        Element fc = get_cell<Element>({fx, fy})->get();
        (void)remove_cell<Element>({fx, fy});
        (void)insert_cell({tx, ty}, std::move(fc));
        return true;
    }
    Element tc = get_cell<Element>({tx, ty})->get();
    (void)remove_cell<Element>({tx, ty});
    (void)insert_cell({fx, fy}, std::move(tc));
    return true;
}

void SandSimulation::mutate_cell(std::int64_t x, std::int64_t y, epix::utils::function_ref<void(Element&)> fn) {
    if (Element* e = get_elem_ptr(x, y)) fn(*e);
}

Element* SandSimulation::get_elem_ptr(std::int64_t x, std::int64_t y) {
    const std::int64_t cw = static_cast<std::int64_t>(chunk_width());
    auto cx               = static_cast<std::int32_t>(x >= 0 ? x / cw : (x - cw + 1) / cw);
    auto cy               = static_cast<std::int32_t>(y >= 0 ? y / cw : (y - cw + 1) / cw);
    auto lx               = static_cast<std::uint32_t>(((x % cw) + cw) % cw);
    auto ly               = static_cast<std::uint32_t>(((y % cw) + cw) % cw);
    auto chunk_res        = get_chunk_mut({cx, cy});
    if (!chunk_res.has_value()) return nullptr;
    auto& layer   = static_cast<grid::ChunkLayer<kDim>&>(chunk_res->get());
    auto elem_res = layer.template get_mut<Element>({lx, ly});
    return elem_res.has_value() ? &elem_res->get() : nullptr;
}

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation — physics helpers
// ──────────────────────────────────────────────────────────────────────────────

bool SandSimulation::valid(std::int64_t x, std::int64_t y) const { return cell_state(x, y) != CellState::Blocked; }

glm::vec2 SandSimulation::get_grav(std::int64_t x, std::int64_t y) const {
    (void)x;
    (void)y;
    return m_world->gravity();
}

glm::vec2 SandSimulation::get_default_vel(std::int64_t x, std::int64_t y) const { return {0.0f, 0.0f}; }

float SandSimulation::air_density(std::int64_t x, std::int64_t y) const {
    (void)x;
    (void)y;
    return 0.001225f;
}

int SandSimulation::not_moving_threshold(glm::vec2 grav) const {
    float len = glm::length(grav);
    if (len < 0.001f) return std::numeric_limits<int>::max();
    return static_cast<int>(std::max(1.0f, std::min(4000.0f / std::pow(len, 0.3f), 65535.0f)));
}

SandSimulation::RaycastResult SandSimulation::raycast_to(std::int64_t x,
                                                         std::int64_t y,
                                                         std::int64_t tx,
                                                         std::int64_t ty) {
    RaycastResult result{0, x, y, std::nullopt};
    std::int64_t dx = tx - x, dy = ty - y;
    auto steps_total = static_cast<int>(std::max(std::abs(dx), std::abs(dy)));
    if (steps_total == 0) return result;
    float sx = static_cast<float>(dx) / steps_total;
    float sy = static_cast<float>(dy) / steps_total;
    float cx = static_cast<float>(x) + sx;
    float cy = static_cast<float>(y) + sy;
    for (int i = 0; i < steps_total; ++i, cx += sx, cy += sy) {
        auto nx = static_cast<std::int64_t>(std::round(cx));
        auto ny = static_cast<std::int64_t>(std::round(cy));
        if (has_cell(nx, ny)) {
            result.hit = {nx, ny};
            break;
        }
        if (!valid(nx, ny)) break;
        result.new_x = nx;
        result.new_y = ny;
        result.steps++;
    }
    return result;
}

bool SandSimulation::collide(std::int64_t x, std::int64_t y, std::int64_t tx, std::int64_t ty) {
    Element* cell  = get_elem_ptr(x, y);
    Element* tcell = get_elem_ptr(tx, ty);
    if (!cell || !tcell) return false;
    const ElementBase& elem  = (*m_registry)[cell->base_id];
    const ElementBase& telem = (*m_registry)[tcell->base_id];

    float dx   = static_cast<float>(tx - x) + tcell->inpos.x - cell->inpos.x;
    float dy   = static_cast<float>(ty - y) + tcell->inpos.y - cell->inpos.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.001f) return false;
    glm::vec2 d = {dx / dist, dy / dist};

    glm::vec2 dv  = cell->velocity - tcell->velocity;
    float v_dot_d = dv.x * d.x + dv.y * d.y;
    if (v_dot_d <= 0.0f) return false;

    float m1 = elem.density;
    float m2 = telem.density;
    if (telem.type == ElementType::Solid || telem.type == ElementType::Body) {
        m1 = 0.0f;
    } else if (telem.type == ElementType::Powder && !tcell->freefall()) {
        m1 *= 0.5f;
    }
    if (telem.type == ElementType::Powder && elem.type == ElementType::Liquid && telem.density < elem.density) {
        return true;
    }
    if (elem.type == ElementType::Liquid) {
        d       = std::abs(dx) >= std::abs(dy) ? glm::vec2{dx > 0 ? 1.0f : -1.0f, 0.0f}
                                               : glm::vec2{0.0f, dy > 0 ? 1.0f : -1.0f};
        v_dot_d = dv.x * d.x + dv.y * d.y;
        if (v_dot_d <= 0.0f) return false;
    }

    float denom = m1 + m2;
    if (denom < 0.001f) return false;
    float restitution = std::max(elem.restitution, telem.restitution);
    float j           = -(1.0f + restitution) * v_dot_d / denom;
    glm::vec2 jxy     = {j * d.x, j * d.y};
    cell->velocity += jxy * m2;
    tcell->velocity -= jxy * m1;

    // Friction (tangential impulse) — geometric mean, 2/3 factor (matches reference)
    float friction   = std::sqrt(elem.friction * telem.friction);
    float cross_dv   = (dv.x * dy - dv.y * dx) / dist;
    float jf         = cross_dv / denom;
    float jf_max     = friction * std::abs(j);
    float jfabs      = std::min(jf_max, std::abs(jf));
    glm::vec2 jf_tan = {cross_dv > 0.0f ? dy / dist : -dy / dist, cross_dv > 0.0f ? -dx / dist : dx / dist};
    glm::vec2 jfxy   = jf_tan * (jfabs * 2.0f / 3.0f);
    cell->velocity -= jfxy * m2;
    tcell->velocity += jfxy * m1;

    if (elem.type == ElementType::Liquid && telem.type == ElementType::Liquid) {
        glm::vec2 blend = cell->velocity * 0.55f + tcell->velocity * 0.45f;
        cell->velocity  = blend;
        tcell->velocity = blend;
    }
    if (!tcell->freefall()) tcell->velocity = {};
    return true;
}

void SandSimulation::touch(std::int64_t x, std::int64_t y) {
    Element* cell = get_elem_ptr(x, y);
    if (!cell) return;
    const ElementBase& base = (*m_registry)[cell->base_id];
    if (base.type == ElementType::Solid || base.type == ElementType::Body) return;
    if (cell->freefall()) return;
    if (base.awake_rate <= 0.0f) return;
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_real_distribution<float> dist_f{0.0f, 1.0f};
    if (dist_f(rng) < base.awake_rate) {
        cell->set_freefall(true);
        cell->velocity = get_default_vel(x, y);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation::step_particle — per-element behaviour dispatch.
// ──────────────────────────────────────────────────────────────────────────────

void SandSimulation::step_particle_powder(std::int64_t x_, std::int64_t y_, std::uint64_t tick, float delta) {
    Element* cell = get_elem_ptr(x_, y_);
    if (!cell) return;
    const ElementBase& base_elem = (*m_registry)[cell->base_id];

    glm::vec2 grav = get_grav(x_, y_);
    float glen     = glm::length(grav);
    glm::ivec2 gd =
        glen > 0.001f ? glm::ivec2{(int)std::round(grav.x / glen), (int)std::round(grav.y / glen)} : glm::ivec2{0, -1};
    glm::ivec2 lp = {-gd.y, gd.x};   // perpendicular left
    glm::ivec2 rp = {gd.y, -gd.x};   // perpendicular right
    glm::ivec2 ag = {-gd.x, -gd.y};  // anti-gravity (above)

    // ── Buoyancy + settling check (feature/pixel_b2d logic) ───────────────────
    // Directions: below (gd), above (ag), left (lp), right (rp).
    int liquid_count = 0, empty_count = 0;
    float liquid_density  = 0.0f;
    int b_lr_not_freefall = 0;  // below + left + right that are sleeping powder

    // below
    {
        std::int64_t bx = x_ + gd.x, by = y_ + gd.y;
        auto s = cell_state(bx, by);
        if (s == CellState::Occupied) {
            Element* nc = get_elem_ptr(bx, by);
            if (nc) {
                const ElementBase& ne = (*m_registry)[nc->base_id];
                if (ne.type == ElementType::Liquid) {
                    liquid_count++;
                    liquid_density += ne.density;
                }
                if (ne.type == ElementType::Powder && !nc->freefall()) b_lr_not_freefall++;
            }
        } else if (s == CellState::EmptyInChunk) {
            empty_count++;
        }
    }
    // above
    {
        std::int64_t ax = x_ + ag.x, ay = y_ + ag.y;
        auto s = cell_state(ax, ay);
        if (s == CellState::Occupied) {
            Element* nc = get_elem_ptr(ax, ay);
            if (nc) {
                const ElementBase& ne = (*m_registry)[nc->base_id];
                if (ne.type == ElementType::Liquid) {
                    liquid_count++;
                    liquid_density += ne.density;
                }
            }
        } else if (s == CellState::EmptyInChunk) {
            empty_count++;
        }
    }
    // left + right — also capture liquid velocity drag
    for (auto dir : std::array<glm::ivec2, 2>{lp, rp}) {
        std::int64_t nx = x_ + dir.x, ny = y_ + dir.y;
        auto s = cell_state(nx, ny);
        if (s == CellState::Occupied) {
            Element* nc = get_elem_ptr(nx, ny);
            if (nc) {
                const ElementBase& ne = (*m_registry)[nc->base_id];
                if (ne.type == ElementType::Liquid && ne.density > base_elem.density) {
                    liquid_count++;
                    liquid_density += ne.density;
                    glm::vec2 d_hat    = {(float)dir.x, (float)dir.y};
                    glm::vec2 vel_hori = d_hat * glm::dot(nc->velocity, d_hat);
                    if (glm::length(vel_hori) > glm::length(cell->velocity))
                        cell->velocity = 0.4f * vel_hori + 0.6f * cell->velocity;
                }
                if (ne.type == ElementType::Powder && !nc->freefall()) b_lr_not_freefall++;
            }
        } else if (s == CellState::EmptyInChunk) {
            empty_count++;
        }
    }

    // Force sleep if all three directions (below, left, right) blocked by sleeping powder
    if (b_lr_not_freefall == 3) {
        cell->set_freefall(false);
        cell->velocity = {};
    }

    // Apply liquid buoyancy
    if (liquid_count > empty_count && liquid_count > 0) {
        liquid_density /= (float)liquid_count;
        grav *= (base_elem.density - liquid_density) / base_elem.density;
        glen = glm::length(grav);
        cell->velocity *= 0.9f;
    }

    // ── Wake-up check when sleeping ───────────────────────────────────────────
    if (!cell->freefall()) {
        // Check if below is blocked by solid or sleeping powder
        std::int64_t bx = x_ + gd.x, by = y_ + gd.y;
        if (!valid(bx, by)) return;
        bool below_blocked = false;
        {
            auto s = cell_state(bx, by);
            if (s == CellState::Occupied) {
                Element* nc = get_elem_ptr(bx, by);
                if (nc) {
                    const ElementBase& ne = (*m_registry)[nc->base_id];
                    if (ne.type == ElementType::Solid || ne.type == ElementType::Body) below_blocked = true;
                    if (ne.type == ElementType::Powder && !nc->freefall()) below_blocked = true;
                }
            }
        }
        if (below_blocked) return;

        // Wake sleeping powder directly above us
        {
            std::int64_t ax = x_ + ag.x, ay = y_ + ag.y;
            if (valid(ax, ay) && cell_state(ax, ay) == CellState::Occupied) {
                Element* ac = get_elem_ptr(ax, ay);
                if (ac && !ac->freefall() && (*m_registry)[ac->base_id].type == ElementType::Powder) {
                    ac->set_freefall(true);
                    ac->velocity = get_default_vel(ax, ay);
                    touch(ax - 1, ay);
                    touch(ax + 1, ay);
                    touch(ax, ay - 1);
                    touch(ax, ay + 1);
                }
            }
        }

        cell->velocity = get_default_vel(x_, y_);
        cell->set_freefall(true);
    }

    // ── Physics integration ───────────────────────────────────────────────────
    touch(x_, y_);

    cell->velocity += grav * delta;
    cell->velocity *= 0.99f;
    cell->inpos += cell->velocity * delta;
    auto di = static_cast<std::int64_t>(std::round(cell->inpos.x));
    auto dj = static_cast<std::int64_t>(std::round(cell->inpos.y));
    if (di == 0 && dj == 0) return;
    cell->inpos.x -= (float)di;
    cell->inpos.y -= (float)dj;
    auto mt = static_cast<std::int64_t>(chunk_width());
    di      = std::clamp(di, -mt, mt);
    dj      = std::clamp(dj, -mt, mt);

    auto res        = raycast_to(x_, y_, x_ + di, y_ + dj);
    bool moved      = false;
    std::int64_t cx = x_, cy = y_;

    if (res.steps > 0) {
        swap_cells(x_, y_, res.new_x, res.new_y);
        cx   = res.new_x;
        cy   = res.new_y;
        cell = get_elem_ptr(cx, cy);
        if (cell) cell->set_updated(true);
        moved = true;
    }

    if (res.hit.has_value()) {
        auto [hx, hy]          = *res.hit;
        Element* hc            = get_elem_ptr(hx, hy);
        bool blocking_freefall = false;
        if (hc && cell) {
            const ElementBase& hb = (*m_registry)[hc->base_id];
            if (hb.type == ElementType::Solid || hb.type == ElementType::Body || hb.type == ElementType::Powder) {
                collide(cx, cy, hx, hy);
                blocking_freefall = hc->freefall();
            } else {
                // Lighter material: swap through
                if (swap_cells(cx, cy, hx, hy)) {
                    cx   = hx;
                    cy   = hy;
                    cell = get_elem_ptr(cx, cy);
                    if (cell) cell->set_updated(true);
                    moved = true;
                }
            }
        }

        // ── Diagonal slide ────────────────────────────────────────────────────
        // always_slide = true (matches feature/pixel_b2d default)
        if (!moved && glen > 0.001f && cell) {
            // Only slide when velocity is roughly aligned with gravity
            auto calculate_angle_diff = [](glm::vec2 a, glm::vec2 b) -> float {
                float dot = a.x * b.x + a.y * b.y;
                float det = a.x * b.y - a.y * b.x;
                return std::atan2(det, dot);
            };
            float angle_diff = calculate_angle_diff(cell->velocity, grav);
            if (std::abs(angle_diff) < std::numbers::pi / 2.0f) {
                float grav_angle = std::atan2(grav.y, grav.x);
                glm::vec2 lb_f   = {std::cos(grav_angle - std::numbers::pi / 4.0f),
                                    std::sin(grav_angle - std::numbers::pi / 4.0f)};
                glm::vec2 rb_f   = {std::cos(grav_angle + std::numbers::pi / 4.0f),
                                    std::sin(grav_angle + std::numbers::pi / 4.0f)};
                glm::vec2 l_f    = {std::cos(grav_angle - std::numbers::pi / 2.0f),
                                    std::sin(grav_angle - std::numbers::pi / 2.0f)};
                glm::vec2 r_f    = {std::cos(grav_angle + std::numbers::pi / 2.0f),
                                    std::sin(grav_angle + std::numbers::pi / 2.0f)};

                static thread_local std::mt19937 slide_rng{std::random_device{}()};
                static thread_local std::uniform_real_distribution<float> slide_dis{-0.3f, 0.3f};
                bool lb_first = slide_dis(slide_rng) > 0.0f;

                glm::vec2 dirs[2]  = {lb_first ? lb_f : rb_f, lb_first ? rb_f : lb_f};
                glm::vec2 idirs[2] = {lb_first ? l_f : r_f, lb_first ? r_f : l_f};

                for (int i = 0; i < 2 && !moved; ++i) {
                    std::int64_t ddx = (std::int64_t)std::round(dirs[i].x);
                    std::int64_t ddy = (std::int64_t)std::round(dirs[i].y);
                    if (ddx == 0 && ddy == 0) continue;
                    auto tx = cx + ddx, ty = cy + ddy;
                    auto ux = cx + (std::int64_t)std::round(idirs[i].x);
                    auto uy = cy + (std::int64_t)std::round(idirs[i].y);
                    if (!valid(tx, ty)) continue;
                    auto ts  = cell_state(tx, ty);
                    bool can = (ts == CellState::EmptyInChunk);
                    if (!can && ts == CellState::Occupied) {
                        Element* nc = get_elem_ptr(tx, ty);
                        can         = nc && (*m_registry)[nc->base_id].type == ElementType::Liquid;
                    }
                    if (!can) continue;
                    if (valid(ux, uy) && cell_state(ux, uy) == CellState::EmptyInChunk) swap_cells(tx, ty, ux, uy);
                    if (swap_cells(cx, cy, tx, ty)) {
                        cx   = tx;
                        cy   = ty;
                        cell = get_elem_ptr(cx, cy);
                        if (cell) {
                            cell->set_updated(true);
                            constexpr float kSlidePrefix = 0.05f;
                            cell->velocity += glm::vec2(idirs[i] + dirs[i]) * (kSlidePrefix / delta);
                        }
                        moved = true;
                    }
                }
            }
        }

        if (!blocking_freefall && !moved && cell) {
            cell->set_freefall(false);
            cell->velocity = {};
        }
    }

    if (moved && cell) {
        cell->not_move_count = 0;
        touch(x_ - 1, y_);
        touch(x_ + 1, y_);
        touch(x_, y_ - 1);
        touch(x_, y_ + 1);
        touch(x_ - 1, y_ - 1);
        touch(x_ + 1, y_ - 1);
        touch(x_ - 1, y_ + 1);
        touch(x_ + 1, y_ + 1);
        touch(cx - 1, cy);
        touch(cx + 1, cy);
        touch(cx, cy - 1);
        touch(cx, cy + 1);
    } else if (!moved && cell) {
        cell->not_move_count++;
        auto thr = (std::uint16_t)std::clamp(not_moving_threshold(grav), 1, (int)UINT16_MAX);
        if (cell->not_move_count >= thr) {
            cell->set_freefall(false);
            cell->velocity       = {};
            cell->not_move_count = 0;
        }
    }
}

void SandSimulation::step_particle(std::int64_t x_, std::int64_t y_, std::uint64_t tick) {
    Element* cell = get_elem_ptr(x_, y_);
    if (!cell || cell->updated()) return;
    cell->set_updated(true);
    const ElementBase& elem = (*m_registry)[cell->base_id];
    constexpr float kDelta  = 1.0f / 60.0f;
    switch (elem.type) {
        case ElementType::Powder:
            step_particle_powder(x_, y_, tick, kDelta);
            break;
        case ElementType::Liquid:
            step_particle_liquid(x_, y_, tick, kDelta);
            break;
        case ElementType::Gas:
            step_particle_gas(x_, y_, tick, kDelta);
            break;
        default:
            break;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation::step_cells — parallel update using 3×3 modulo groups.
// ──────────────────────────────────────────────────────────────────────────────

void SandSimulation::step_particle_liquid(std::int64_t x_, std::int64_t y_, std::uint64_t tick, float delta) {
    Element* cell = get_elem_ptr(x_, y_);
    if (!cell) return;
    const ElementBase& base_elem = (*m_registry)[cell->base_id];
    cell->set_freefall(true);

    glm::vec2 grav = get_grav(x_, y_);
    float glen     = glm::length(grav);
    glm::ivec2 gd =
        glen > 0.001f ? glm::ivec2{(int)std::round(grav.x / glen), (int)std::round(grav.y / glen)} : glm::ivec2{0, -1};
    glm::ivec2 lp = {-gd.y, gd.x};
    glm::ivec2 rp = {gd.y, -gd.x};

    // Viscosity: slow down neighbouring liquids
    for (auto [ox, oy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
        Element* nc = get_elem_ptr(x_ + ox, y_ + oy);
        if (nc && (*m_registry)[nc->base_id].type == ElementType::Liquid) nc->velocity *= 0.97f;
    }

    cell->velocity += grav * delta;
    cell->velocity *= 0.99f;
    cell->inpos += cell->velocity * delta;
    auto di = static_cast<std::int64_t>(std::round(cell->inpos.x));
    auto dj = static_cast<std::int64_t>(std::round(cell->inpos.y));
    cell->inpos.x -= (float)di;
    cell->inpos.y -= (float)dj;
    auto mt = static_cast<std::int64_t>(chunk_width());
    di      = std::clamp(di, -mt, mt);
    dj      = std::clamp(dj, -mt, mt);

    auto res        = raycast_to(x_, y_, x_ + di, y_ + dj);
    bool moved      = false;
    std::int64_t cx = x_, cy = y_;

    if (res.steps > 0) {
        swap_cells(x_, y_, res.new_x, res.new_y);
        cx   = res.new_x;
        cy   = res.new_y;
        cell = get_elem_ptr(cx, cy);
        if (cell) cell->set_updated(true);
        moved = true;
    }

    if (res.hit.has_value()) {
        auto [hx, hy] = *res.hit;
        Element* hc   = get_elem_ptr(hx, hy);
        if (hc) {
            const ElementBase& hb = (*m_registry)[hc->base_id];
            bool solid_like =
                hb.type == ElementType::Solid || hb.type == ElementType::Body || hb.type == ElementType::Powder;
            bool heavy_liq = hb.type == ElementType::Liquid && hb.density >= base_elem.density;
            if ((solid_like || heavy_liq) && cell) {
                collide(cx, cy, hx, hy);
            } else if (hb.type == ElementType::Liquid && cell) {
                if (swap_cells(cx, cy, hx, hy)) {
                    cx   = hx;
                    cy   = hy;
                    cell = get_elem_ptr(cx, cy);
                    if (cell) cell->set_updated(true);
                    moved = true;
                }
            }
        }
    }

    // Spread: diagonal then horizontal
    if (!moved && cell) {
        bool pl       = ((x_ + y_ + (std::int64_t)tick) & 1) == 0;
        glm::ivec2 d1 = {gd.x + lp.x, gd.y + lp.y}, d2 = {gd.x + rp.x, gd.y + rp.y};
        for (auto d : pl ? std::array{d1, d2} : std::array{d2, d1}) {
            if (moved) break;
            auto nx = cx + d.x, ny = cy + d.y;
            auto s   = cell_state(nx, ny);
            bool can = (s == CellState::EmptyInChunk);
            if (!can && s == CellState::Occupied) {
                Element* nc = get_elem_ptr(nx, ny);
                can         = nc && (*m_registry)[nc->base_id].type == ElementType::Liquid &&
                              (*m_registry)[nc->base_id].density < base_elem.density;
            }
            if (can && swap_cells(cx, cy, nx, ny)) {
                cx   = nx;
                cy   = ny;
                cell = get_elem_ptr(cx, cy);
                if (cell) cell->set_updated(true);
                moved = true;
            }
        }
        if (!moved) {
            std::int64_t spread = 3;
            for (auto dp : pl ? std::array{lp, rp} : std::array{rp, lp}) {
                if (moved) break;
                auto sr = raycast_to(cx, cy, cx + dp.x * spread, cy + dp.y * spread);
                if (sr.steps > 0 && swap_cells(cx, cy, sr.new_x, sr.new_y)) {
                    cx   = sr.new_x;
                    cy   = sr.new_y;
                    cell = get_elem_ptr(cx, cy);
                    if (cell) cell->set_updated(true);
                    moved = true;
                }
            }
        }
    }

    if (moved && cell) {
        cell->not_move_count = 0;
        touch(cx - 1, cy);
        touch(cx + 1, cy);
        touch(cx, cy - 1);
        touch(cx, cy + 1);
    } else if (!moved && cell) {
        cell->not_move_count++;
        auto thr = (std::uint16_t)std::clamp(not_moving_threshold(grav) / 15, 1, (int)UINT16_MAX);
        if (cell->not_move_count >= thr) {
            cell->set_freefall(false);
            cell->velocity       = {};
            cell->not_move_count = 0;
        }
    }
}

void SandSimulation::step_particle_gas(std::int64_t x_, std::int64_t y_, std::uint64_t tick, float delta) {
    Element* cell = get_elem_ptr(x_, y_);
    if (!cell) return;
    const ElementBase& base_elem = (*m_registry)[cell->base_id];
    cell->set_freefall(true);

    glm::vec2 grav = get_grav(x_, y_);
    float glen     = glm::length(grav);
    // Buoyancy: scale grav by density ratio; lighter-than-air rises
    float ad = air_density(x_, y_);
    if (glen > 0.001f) {
        float ratio = (base_elem.density - ad) / std::max(base_elem.density, ad);
        grav *= ratio;
        glen = glm::length(grav);
    }
    glm::ivec2 gd =
        glen > 0.001f ? glm::ivec2{(int)std::round(grav.x / glen), (int)std::round(grav.y / glen)} : glm::ivec2{0, 1};
    glm::ivec2 lp = {-gd.y, gd.x};
    glm::ivec2 rp = {gd.y, -gd.x};

    // Random horizontal drift + drag
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> rand_f{-1.0f, 1.0f};
    glm::vec2 horiz = glm::vec2{(float)lp.x, (float)lp.y} * rand_f(rng) * 5.0f;
    float vlen      = glm::length(cell->velocity);
    glm::vec2 drag  = vlen > 0.001f ? -0.1f * vlen * cell->velocity / 20.0f : glm::vec2{};

    cell->velocity += (grav + horiz) * delta + drag;
    cell->inpos += cell->velocity * delta;
    auto di = static_cast<std::int64_t>(std::round(cell->inpos.x));
    auto dj = static_cast<std::int64_t>(std::round(cell->inpos.y));
    cell->inpos.x -= (float)di;
    cell->inpos.y -= (float)dj;
    auto mt = static_cast<std::int64_t>(chunk_width());
    di      = std::clamp(di, -mt, mt);
    dj      = std::clamp(dj, -mt, mt);

    auto res        = raycast_to(x_, y_, x_ + di, y_ + dj);
    bool moved      = false;
    std::int64_t cx = x_, cy = y_;

    if (res.steps > 0) {
        swap_cells(x_, y_, res.new_x, res.new_y);
        cx   = res.new_x;
        cy   = res.new_y;
        cell = get_elem_ptr(cx, cy);
        if (cell) cell->set_updated(true);
        moved = true;
    }

    if (res.hit.has_value() && cell) {
        auto [hx, hy] = *res.hit;
        Element* hc   = get_elem_ptr(hx, hy);
        if (hc && (*m_registry)[hc->base_id].type == ElementType::Gas &&
            (*m_registry)[hc->base_id].density > base_elem.density) {
            if (swap_cells(cx, cy, hx, hy)) {
                cx   = hx;
                cy   = hy;
                cell = get_elem_ptr(cx, cy);
                if (cell) cell->set_updated(true);
                moved = true;
            }
        }
    }

    // Horizontal spread
    if (!moved && cell) {
        bool pl = ((x_ + y_ + (std::int64_t)tick) & 1) == 0;
        for (auto dp : pl ? std::array{lp, rp} : std::array{rp, lp}) {
            if (moved) break;
            auto nx = cx + dp.x, ny = cy + dp.y;
            auto s   = cell_state(nx, ny);
            bool can = (s == CellState::EmptyInChunk);
            if (!can && s == CellState::Occupied) {
                Element* nc = get_elem_ptr(nx, ny);
                can         = nc && (*m_registry)[nc->base_id].type == ElementType::Gas && nc->base_id != cell->base_id;
            }
            if (can && swap_cells(cx, cy, nx, ny)) {
                cx   = nx;
                cy   = ny;
                cell = get_elem_ptr(cx, cy);
                if (cell) cell->set_updated(true);
                moved = true;
            }
        }
    }

    if (moved && cell) {
        cell->not_move_count = 0;
        touch(cx - 1, cy);
        touch(cx + 1, cy);
        touch(cx, cy - 1);
        touch(cx, cy + 1);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation::step_cells — parallel update using 3×3 modulo groups.

void SandSimulation::step_cells() {
    static std::uint64_t s_tick = 0;
    const std::uint64_t tick    = s_tick++;
    const std::int64_t cw       = static_cast<std::int64_t>(chunk_width());

    // ── 1. Clear `updated` flags on every live cell ───────────────────────────
    for (auto chunk_ref : iter_chunks_mut()) {
        for (auto&& [lpos, elem] : chunk_ref.get().iter_mut<Element>()) {
            (void)lpos;
            elem.set_updated(false);
        }
    }

    // ── 2. Collect and shuffle chunk coordinates ──────────────────────────────
    auto offset_coords =
        std::ranges::to<std::vector>(std::views::cartesian_product(std::views::iota(0, 3), std::views::iota(0, 3)));
    static auto rng = std::mt19937{std::random_device{}()};
    std::shuffle(offset_coords.begin(), offset_coords.end(), rng);

    auto& pool = tasks::ComputeTaskPool::get();

    // ── 3. Process in 3×3 modulo groups (no two adjacent chunks run in parallel)
    for (auto&& [rx, ry] : offset_coords) {
        thread_local static std::vector<tasks::Task<void>> group_tasks;
        for (auto&& cpos : std::views::filter(iter_chunk_pos(), [rx, ry, this](auto&& cpos) {
                 if (!((cpos[0] % 3 + 3) % 3 == rx && (cpos[1] % 3 + 3) % 3 == ry)) return false;
                 if (m_active_chunks.empty()) return true;
                 auto cx = static_cast<std::int32_t>(cpos[0]);
                 auto cy = static_cast<std::int32_t>(cpos[1]);
                 for (auto& ac : m_active_chunks) {
                     if (ac[0] == cx && ac[1] == cy) return true;
                 }
                 return false;
             })) {
            group_tasks.push_back(pool.spawn([cpos, cw, tick, this] {
                // Snapshot positions first — prevents iterator invalidation during steps.
                thread_local static std::vector<std::array<std::int64_t, 2>> positions;
                for (auto&& [lpos, _] : get_chunk(cpos).value().get().iter<Element>()) {
                    positions.push_back({
                        static_cast<std::int64_t>(cpos[0]) * cw + static_cast<std::int64_t>(lpos[0]),
                        static_cast<std::int64_t>(cpos[1]) * cw + static_cast<std::int64_t>(lpos[1]),
                    });
                }
                for (auto&& [x, y] : positions) {
                    step_particle(x, y, tick);
                }
                positions.clear();
            }));
        }
        for (auto& t : group_tasks) t.block();
        group_tasks.clear();
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation — public methods
// ──────────────────────────────────────────────────────────────────────────────

void SandSimulation::step() { step_cells(); }

}  // namespace epix::ext::fallingsand
