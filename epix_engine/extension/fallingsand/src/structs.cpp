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
#include <variant>
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
    std::size_t bid = value.base_id;
    if (!insert_cell({x, y}, std::move(value)).has_value()) return false;
    // Initialize the ThermalCell for this position via the element's thermal factory.
    if (bid < m_registry->size()) {
        const ElementBase& base = (*m_registry)[bid];
        // Per-cell seed packed from world coords; gives deterministic variation.
        std::uint64_t seed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                             static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
        ThermalCell t      = base.thermal_construct_func(bid, base, seed);
        (void)insert_cell<ThermalCell>({x, y}, std::move(t));
    }
    return true;
}

bool SandSimulation::clear_cell(std::int64_t x, std::int64_t y) {
    (void)remove_cell<ThermalCell>({x, y});
    return remove_cell<Element>({x, y}).has_value();
}

bool SandSimulation::move_cell(std::int64_t fx, std::int64_t fy, std::int64_t tx, std::int64_t ty) {
    if (cell_state(tx, ty) != CellState::EmptyInChunk) return false;
    auto from = get_cell<Element>({fx, fy});
    if (!from.has_value()) return false;
    Element moved = from->get();
    // Move thermal cell along with the element so heat follows the matter.
    ThermalCell thermal_moved{};
    if (auto t = get_cell<ThermalCell>({fx, fy}); t.has_value()) thermal_moved = t->get();
    if (!remove_cell<Element>({fx, fy}).has_value()) return false;
    if (!insert_cell({tx, ty}, std::move(moved)).has_value()) return false;
    (void)insert_cell<ThermalCell>({tx, ty}, ThermalCell{thermal_moved});
    (void)remove_cell<ThermalCell>({fx, fy});
    return true;
}

bool SandSimulation::swap_cells(std::int64_t fx, std::int64_t fy, std::int64_t tx, std::int64_t ty) {
    auto fs = cell_state(fx, fy);
    auto ts = cell_state(tx, ty);
    if (fs == CellState::Blocked || ts == CellState::Blocked) return false;
    bool from_occ = (fs == CellState::Occupied);
    bool to_occ   = (ts == CellState::Occupied);
    if (!from_occ && !to_occ) return false;
    // Snapshot thermal cells (sparse; absent means default-temperature).
    ThermalCell tf{}, tt{};
    if (auto t = get_cell<ThermalCell>({fx, fy}); t.has_value()) tf = t->get();
    if (auto t = get_cell<ThermalCell>({tx, ty}); t.has_value()) tt = t->get();
    if (from_occ && to_occ) {
        Element fc = get_cell<Element>({fx, fy})->get();
        Element tc = get_cell<Element>({tx, ty})->get();
        (void)remove_cell<Element>({fx, fy});
        (void)remove_cell<Element>({tx, ty});
        (void)insert_cell({fx, fy}, std::move(tc));
        (void)insert_cell({tx, ty}, std::move(fc));
        (void)insert_cell<ThermalCell>({fx, fy}, ThermalCell{tt});
        (void)insert_cell<ThermalCell>({tx, ty}, ThermalCell{tf});
        return true;
    }
    if (from_occ) {
        // to was empty: move element and its thermal to tx,ty; clear thermal at fx,fy
        Element fc = get_cell<Element>({fx, fy})->get();
        (void)remove_cell<Element>({fx, fy});
        (void)insert_cell({tx, ty}, std::move(fc));
        (void)insert_cell<ThermalCell>({tx, ty}, ThermalCell{tf});
        (void)remove_cell<ThermalCell>({fx, fy});
        return true;
    }
    // to was occupied, from was empty: move element and its thermal to fx,fy; clear thermal at tx,ty
    Element tc = get_cell<Element>({tx, ty})->get();
    (void)remove_cell<Element>({tx, ty});
    (void)insert_cell({fx, fy}, std::move(tc));
    (void)insert_cell<ThermalCell>({fx, fy}, ThermalCell{tt});
    (void)remove_cell<ThermalCell>({tx, ty});
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
    auto cv       = grid::chunk_element<Element>(chunk_res->get());
    auto elem_res = cv.get_mut({lx, ly});
    return elem_res.has_value() ? &elem_res->get() : nullptr;
}

ThermalCell* SandSimulation::get_thermal_ptr(std::int64_t x, std::int64_t y) {
    auto cell = get_cell_mut<ThermalCell>({x, y});
    return cell.has_value() ? &cell->get() : nullptr;
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
        if (!valid(nx, ny)) {
            if (m_world->missing_chunk_as_solid()) result.hit = {nx, ny};
            break;
        }
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
    // Update the chunk dirty rect (matches old Simulation::touch -> chunk.touch)
    if (valid(x, y)) {
        const std::size_t shift             = chunk_width_shift();
        const std::int64_t mask             = static_cast<std::int64_t>((1 << shift) - 1);
        std::array<std::int32_t, kDim> cpos = {
            static_cast<std::int32_t>(x >> shift),
            static_cast<std::int32_t>(y >> shift),
        };
        auto dr_opt = m_chunk_dirty_rects.get_mut(cpos);
        if (dr_opt.has_value()) {
            dr_opt->get()->touch(static_cast<std::int32_t>(x & mask), static_cast<std::int32_t>(y & mask));
        }
    }
    // Wake sleeping cell (matches old Simulation::touch probability-based wake)
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
// SandSimulation::put_cell / erase_cell — convenience wrappers.
// ──────────────────────────────────────────────────────────────────────────────

bool SandSimulation::put_cell(std::array<std::int64_t, kDim> pos, Element elem) {
    std::size_t bid = elem.base_id;
    auto res        = insert_cell<Element>(pos, std::move(elem));
    if (!res.has_value()) return false;
    if (bid < m_registry->size()) {
        const ElementBase& base = (*m_registry)[bid];
        std::uint64_t seed      = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pos[0])) << 32) |
                                  static_cast<std::uint64_t>(static_cast<std::uint32_t>(pos[1]));
        ThermalCell t           = base.thermal_construct_func(bid, base, seed);
        (void)insert_cell<ThermalCell>(pos, std::move(t));
    }
    touch(pos[0], pos[1]);
    touch(pos[0] + 1, pos[1]);
    touch(pos[0] - 1, pos[1]);
    touch(pos[0], pos[1] + 1);
    touch(pos[0], pos[1] - 1);
    return true;
}

bool SandSimulation::erase_cell(std::array<std::int64_t, kDim> pos) {
    (void)remove_cell<ThermalCell>(pos);
    auto res = remove_cell<Element>(pos);
    touch(pos[0], pos[1]);
    touch(pos[0] + 1, pos[1]);
    touch(pos[0] - 1, pos[1]);
    touch(pos[0], pos[1] + 1);
    touch(pos[0], pos[1] - 1);
    return res.has_value();
}

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation — temperature / heat transfer
// ──────────────────────────────────────────────────────────────────────────────

// ElementBase::density is a dimensionless game unit (water = 1.0).
// Physical thermal-mass calculations require kg/m³.  Multiply game density by
// this factor.
static constexpr float kPhysDensityScale = 1.0f;

void SandSimulation::conduct_thermal_thermal(
    ThermalCell& t1, const ElementBase& b1, ThermalCell& t2, const ElementBase& b2, float delta) {
    float dT = t2.temperature - t1.temperature;
    if (std::abs(dT) < 0.01f) return;
    float cell_area = m_world->cell_size() * m_world->cell_size();
    // Geometric mean of conductivities × contact factor for granular direct contact.
    constexpr float kContactFactor = 20.0f;
    float k_eff                    = std::sqrt(b1.thermal_conductivity * b2.thermal_conductivity) * kContactFactor;
    float flux                     = k_eff * dT * delta;
    float inv_m1                   = 1.0f / (b1.specific_heat * b1.density * kPhysDensityScale * cell_area + 1e-6f);
    float inv_m2                   = 1.0f / (b2.specific_heat * b2.density * kPhysDensityScale * cell_area + 1e-6f);
    constexpr float kMaxDT         = 500.0f;  // cap per-tick temperature change
    t1.temperature += std::clamp(flux * inv_m1, -kMaxDT, kMaxDT);
    t2.temperature -= std::clamp(flux * inv_m2, -kMaxDT, kMaxDT);
}

void SandSimulation::convect_thermal_air(ThermalCell& t, const ElementBase& base, AirCell& air, float delta) {
    float dT = air.temperature - t.temperature;
    if (std::abs(dT) < 0.01f) return;
    float cell_area        = m_world->cell_size() * m_world->cell_size();
    constexpr float h      = 50.0f;  // convective + radiative, W/(m²·K)
    float flux             = h * dT * delta;
    float inv_m_elem       = 1.0f / (base.specific_heat * base.density * kPhysDensityScale * cell_area + 1e-6f);
    float inv_m_air        = 1.0f / (kAirSpecificHeat * air.density * cell_area + 1e-6f);
    constexpr float kMaxDT = 500.0f;
    t.temperature += std::clamp(flux * inv_m_elem, -kMaxDT, kMaxDT);
    air.temperature -= std::clamp(flux * inv_m_air, -kMaxDT, kMaxDT);
}

void SandSimulation::spread_heat(std::int64_t wx, std::int64_t wy, float delta) {
    // Read element via const path so heat-only updates do NOT mark the
    // element grid as modified.  Mut access is acquired only when an action
    // actually mutates the element (set_burning / try_transition / clear).
    auto curr_ref = get_cell<Element>({wx, wy});
    if (!curr_ref.has_value()) return;
    const Element& curr_const    = curr_ref->get();
    const ElementBase& curr_base = (*m_registry)[curr_const.base_id];

    // Always-mutable thermal cell at this position.
    ThermalCell* therm = get_thermal_ptr(wx, wy);
    if (!therm) return;

    static constexpr std::array<std::pair<std::int64_t, std::int64_t>, 4> kCardinals = {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};

    for (auto [dx, dy] : kCardinals) {
        auto nx = wx + dx, ny = wy + dy;
        auto ns = cell_state(nx, ny);
        if (ns == CellState::Occupied) {
            // Read neighbor element via const, get its thermal mut.
            auto neighbor_elem    = get_cell<Element>({nx, ny});
            ThermalCell* nthermal = get_thermal_ptr(nx, ny);
            if (neighbor_elem.has_value() && nthermal) {
                const ElementBase& nb = (*m_registry)[neighbor_elem->get().base_id];
                conduct_thermal_thermal(*therm, curr_base, *nthermal, nb, delta);
            }
        } else if (ns == CellState::EmptyInChunk) {
            auto air = get_cell_mut<AirCell>({nx, ny});
            if (air.has_value()) {
                convect_thermal_air(*therm, curr_base, air->get(), delta);
            }
        }
        // Blocked cells: no heat exchange
    }

    // ── Phase-transition staging heat ─────────────────────────────────────
    // Scan actions for TemperatureAbove/Below conditions and manage staging heat.
    float cell_area = m_world->cell_size() * m_world->cell_size();
    for (const auto& act : curr_base.actions) {
        for (const auto& cond : act.conditions) {
            std::visit(
                [&](const auto& c) {
                    using T = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<T, TemperatureAbove>) {
                        if (!c.clamp) return;  // pure condition, no staging
                        float t = c.target;
                        if (therm->staging_heat > 0.0f && therm->temperature < t) {
                            float dT_release =
                                therm->staging_heat /
                                (curr_base.specific_heat * curr_base.density * kPhysDensityScale * cell_area + 1e-6f);
                            therm->temperature += dT_release;
                            therm->staging_heat = 0.0f;
                        }
                        if (therm->temperature > t) {
                            float dT = therm->temperature - t;
                            float excess =
                                dT * curr_base.specific_heat * curr_base.density * kPhysDensityScale * cell_area;
                            therm->temperature  = t;
                            therm->staging_heat = std::clamp(therm->staging_heat + excess, -1e8f, 1e8f);
                        }
                    } else if constexpr (std::is_same_v<T, TemperatureBelow>) {
                        if (!c.clamp) return;  // pure condition, no staging
                        float t = c.target;
                        if (therm->staging_heat < 0.0f && therm->temperature > t) {
                            float dT_release =
                                -therm->staging_heat /
                                (curr_base.specific_heat * curr_base.density * kPhysDensityScale * cell_area + 1e-6f);
                            therm->temperature += dT_release;
                            therm->staging_heat = 0.0f;
                        }
                        if (therm->temperature < t) {
                            float dT = therm->temperature - t;
                            float deficit =
                                dT * curr_base.specific_heat * curr_base.density * kPhysDensityScale * cell_area;
                            therm->temperature  = t;
                            therm->staging_heat = std::clamp(therm->staging_heat + deficit, -1e8f, 1e8f);
                        }
                    }
                },
                cond);
        }
    }

    // Clamp temperature to sane bounds
    therm->temperature = std::clamp(therm->temperature, 0.0f, 10000.0f);

    // Fire actions with TemperatureAbove/Below + StagingHeat immediately
    for (const auto& act : curr_base.actions) {
        // Only fire actions that have BOTH a temperature threshold and StagingHeat
        bool has_temp = false, has_staging = false;
        for (const auto& cond : act.conditions) {
            if (std::holds_alternative<TemperatureAbove>(cond) || std::holds_alternative<TemperatureBelow>(cond))
                has_temp = true;
            if (std::holds_alternative<StagingHeat>(cond)) has_staging = true;
        }
        if (!has_temp || !has_staging) continue;

        // Check all conditions (skip RandomTick — immediate actions don't need it)
        bool all_met = true;
        for (const auto& cond : act.conditions) {
            bool met = std::visit(
                [&](const auto& c) -> bool {
                    using T = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<T, TemperatureAbove>)
                        return therm->temperature >= c.target;
                    else if constexpr (std::is_same_v<T, TemperatureBelow>)
                        return therm->temperature <= c.target;
                    else if constexpr (std::is_same_v<T, StagingHeat>) {
                        bool is_above = false;
                        for (const auto& oc : act.conditions)
                            if (std::holds_alternative<TemperatureAbove>(oc)) {
                                is_above = true;
                                break;
                            }
                        float required = c.latent_heat_j_per_kg * curr_base.density * kPhysDensityScale * cell_area;
                        return is_above ? therm->staging_heat >= required : therm->staging_heat <= -required;
                    } else if constexpr (std::is_same_v<T, RandomTick>)
                        return true;  // skip random tick for immediate actions
                    else if constexpr (std::is_same_v<T, IsBurning>)
                        return curr_const.burning();
                    else if constexpr (std::is_same_v<T, HasTag>)
                        return curr_const.transition_tag == c.tag;
                    else
                        return true;
                },
                cond);
            if (!met) {
                all_met = false;
                break;
            }
        }
        if (all_met) {
            // Mut access required for transition.
            Element* curr_mut = get_elem_ptr(wx, wy);
            if (!curr_mut) return;
            auto old_id = curr_mut->base_id;
            try_transition(wx, wy, *curr_mut, *therm, curr_base);
            // If despawned or transformed, stop processing this cell
            Element* check = get_elem_ptr(wx, wy);
            if (!check || check->base_id != old_id) return;
        }
    }

    // Visual burning flag: set when above ignition temperature.
    // Only acquire mut access when the flag value actually needs to change.
    if (curr_base.ignition_temperature > 0.0f && therm->temperature >= curr_base.ignition_temperature &&
        !curr_const.burning()) {
        if (Element* m = get_elem_ptr(wx, wy)) m->set_burning(true);
    }
    // Exothermic reaction: burning element self-heats.
    // Total energy = heat_of_combustion * mass, released over burn duration.
    if (curr_const.burning() && curr_base.heat_of_combustion > 0.0f) {
        constexpr float kBurnDurationTicks = 1200.0f;  // ~20 s at 60 fps
        float dT_per_tick = curr_base.heat_of_combustion / (curr_base.specific_heat * kBurnDurationTicks + 1e-6f);
        therm->temperature += dT_per_tick;

        // Track total heat emitted; despawn when fully burned
        therm->heat_emitted +=
            dT_per_tick * curr_base.specific_heat * curr_base.density * kPhysDensityScale * cell_area;
        float total_heat = curr_base.heat_of_combustion * curr_base.density * kPhysDensityScale * cell_area;
        if (therm->heat_emitted >= total_heat) {
            clear_cell(wx, wy);
            touch(wx, wy);
            return;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation — air simulation
// ──────────────────────────────────────────────────────────────────────────────

void SandSimulation::step_air_full(grid::chunk_element_view<kDim, AirCell>& air_view,
                                   const std::array<std::int32_t, kDim>& cpos,
                                   float delta) {
    const std::int64_t cw = static_cast<std::int64_t>(chunk_width());
    float cell_area       = m_world->cell_size() * m_world->cell_size();
    static constexpr std::array<std::pair<std::int64_t, std::int64_t>, 4> kCardinals = {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};

    for (auto&& [lpos, air] : air_view.iter_mut()) {
        (void)lpos;
        std::int64_t wx = static_cast<std::int64_t>(cpos[0]) * cw + static_cast<std::int64_t>(lpos[0]);
        std::int64_t wy = static_cast<std::int64_t>(cpos[1]) * cw + static_cast<std::int64_t>(lpos[1]);

        if (has_cell(wx, wy)) continue;  // dormant under particle

        // Buoyancy: hot air rises, cold air sinks
        float temp_factor = (air.temperature - 293.0f) / 293.0f;
        air.velocity.y += temp_factor * 200.0f * delta;

        // Air-to-air temperature diffusion (const-read neighbors)
        for (auto [dx, dy] : kCardinals) {
            auto neighbor = get_cell<AirCell>({wx + dx, wy + dy});
            if (!neighbor.has_value()) continue;
            float dT               = neighbor->get().temperature - air.temperature;
            float flux             = kAirThermalConductivity * dT * delta;
            constexpr float kMaxDT = 500.0f;
            air.temperature += std::clamp(flux / (kAirSpecificHeat * air.density * cell_area + 1e-6f), -kMaxDT, kMaxDT);
        }

        // Velocity damping
        air.velocity *= 0.95f;

        // Density equalization toward neighbours (simple diffusion)
        for (auto [dx, dy] : kCardinals) {
            auto neighbor = get_cell<AirCell>({wx + dx, wy + dy});
            if (!neighbor.has_value()) continue;
            air.density += (neighbor->get().density - air.density) * 0.1f * delta;
        }
    }
}

void SandSimulation::step_air_decay(grid::chunk_element_view<kDim, AirCell>& air_view,
                                    const std::array<std::int32_t, kDim>& cpos,
                                    float delta) {
    const std::int64_t cw = static_cast<std::int64_t>(chunk_width());

    for (auto&& [lpos, air] : air_view.iter_mut()) {
        (void)lpos;
        std::int64_t wx = static_cast<std::int64_t>(cpos[0]) * cw + static_cast<std::int64_t>(lpos[0]);
        std::int64_t wy = static_cast<std::int64_t>(cpos[1]) * cw + static_cast<std::int64_t>(lpos[1]);

        if (has_cell(wx, wy)) continue;  // dormant under particle

        // Gentle decay toward ambient — no neighbour access
        air.temperature += (293.0f - air.temperature) * 0.5f * delta;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// SandSimulation — random tick / state transitions
// ──────────────────────────────────────────────────────────────────────────────

void SandSimulation::spawn_nearby(
    std::int64_t x, std::int64_t y, std::size_t element_id, int count, std::uint8_t spawn_tag) {
    if (count <= 0) return;
    const ElementBase& target = (*m_registry)[element_id];

    // 8-neighbourhood in random order
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::array<std::pair<std::int64_t, std::int64_t>, 8> offsets = {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
        {1, 1},
        {-1, -1},
        {1, -1},
        {-1, 1},
    }};
    std::shuffle(offsets.begin(), offsets.end(), rng);

    int placed = 0;
    for (auto [dx, dy] : offsets) {
        if (placed >= count) break;
        auto nx = x + dx, ny = y + dy;
        if (cell_state(nx, ny) != CellState::EmptyInChunk) continue;
        std::uint64_t seed     = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(nx)) << 32) |
                                 static_cast<std::uint64_t>(static_cast<std::uint32_t>(ny));
        Element spawned        = target.construct_func(element_id, target, seed);
        spawned.transition_tag = spawn_tag;
        if (set_cell(nx, ny, std::move(spawned))) {
            touch(nx, ny);
            ++placed;
        }
    }
}

void SandSimulation::try_transition(
    std::int64_t x, std::int64_t y, Element& elem, ThermalCell& thermal, const ElementBase& base) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_real_distribution<float> dist{0.0f, 1.0f};
    float cell_area = m_world->cell_size() * m_world->cell_size();

    for (const auto& act : base.actions) {
        // Evaluate all conditions (all must be true)
        bool all_met = true;
        for (const auto& cond : act.conditions) {
            bool met = std::visit(
                [&](const auto& c) -> bool {
                    using T = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<T, TemperatureAbove>) {
                        return thermal.temperature >= c.target;
                    } else if constexpr (std::is_same_v<T, TemperatureBelow>) {
                        return thermal.temperature <= c.target;
                    } else if constexpr (std::is_same_v<T, IsBurning>) {
                        return elem.burning();
                    } else if constexpr (std::is_same_v<T, HasTag>) {
                        return elem.transition_tag == c.tag;
                    } else if constexpr (std::is_same_v<T, ContactWith>) {
                        static constexpr std::array<std::pair<std::int64_t, std::int64_t>, 4> kCardinals = {{
                            {1, 0},
                            {-1, 0},
                            {0, 1},
                            {0, -1},
                        }};
                        for (auto [dx, dy] : kCardinals) {
                            Element* nb = get_elem_ptr(x + dx, y + dy);
                            if (nb && nb->base_id == c.element_id) return true;
                        }
                        return false;
                    } else if constexpr (std::is_same_v<T, StagingHeat>) {
                        float required = c.latent_heat_j_per_kg * base.density * kPhysDensityScale * cell_area;
                        bool is_above  = false;
                        for (const auto& oc : act.conditions) {
                            if (std::holds_alternative<TemperatureAbove>(oc)) {
                                is_above = true;
                                break;
                            }
                        }
                        if (is_above)
                            return thermal.staging_heat >= required;
                        else
                            return thermal.staging_heat <= -required;
                    } else if constexpr (std::is_same_v<T, RandomTick>) {
                        float prob = c.probability;
                        if (prob <= 0.0f && c.compute_prob) prob = c.compute_prob(elem);
                        // Burning elements: scale probability with temperature
                        if (elem.burning() && base.ignition_temperature > 0.0f && prob > 0.0f)
                            prob *= thermal.temperature / base.ignition_temperature;
                        return dist(rng) < prob;
                    }
                    return false;
                },
                cond);
            if (!met) {
                all_met = false;
                break;
            }
        }

        if (!all_met) continue;

        // Execute action
        std::visit(
            [&](const auto& a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, Ignite>) {
                    elem.set_burning(true);
                } else if constexpr (std::is_same_v<T, Extinguish>) {
                    elem.set_burning(false);
                } else if constexpr (std::is_same_v<T, SpawnNearby>) {
                    int count = a.count_min;
                    if (a.count_max > a.count_min) {
                        std::uniform_int_distribution<int> count_dist(a.count_min, a.count_max);
                        count = count_dist(rng);
                    }
                    spawn_nearby(x, y, a.element_id, count, a.spawn_tag);
                } else if constexpr (std::is_same_v<T, TransformTo>) {
                    if (a.target_id != elem.base_id) {
                        const ElementBase& target = (*m_registry)[a.target_id];
                        std::uint64_t seed        = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                                                    static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
                        // Element is replaced; thermal state is preserved (matter stays at the same cell).
                        elem = target.construct_func(a.target_id, target, seed);
                    }
                    thermal.staging_heat = 0.0f;
                    touch(x, y);
                } else if constexpr (std::is_same_v<T, Despawn>) {
                    clear_cell(x, y);
                }
            },
            act.action);

        // Destructive actions (TransformTo/Despawn) invalidate elem — stop.
        if (std::holds_alternative<Despawn>(act.action) || std::holds_alternative<TransformTo>(act.action)) {
            return;
        }
        // Non-destructive actions (Ignite, Extinguish, SpawnNearby) — continue
        // to allow multiple actions to fire in sequence.
    }
}

void SandSimulation::random_tick_chunk(const std::array<std::int32_t, kDim>& cpos,
                                       grid::chunk_element_view<kDim, Element>& elem_view) {
    const std::int64_t cw = static_cast<std::int64_t>(chunk_width());
    auto cw_u32           = static_cast<std::uint32_t>(cw);

    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint32_t> pos_dist(0, cw_u32 - 1);

    std::array<int, 6> cell_counts = {0, 16, 8, 4, 2, 1};

    constexpr int kBudget    = 32;
    int ticked               = 0;
    constexpr int kMaxTicked = 24;

    for (int i = 0; i < kBudget && ticked < kMaxTicked; ++i) {
        auto lx = pos_dist(rng);
        auto ly = pos_dist(rng);
        // Const lookup first — does NOT mark element grid modified.
        auto elem_const = elem_view.get({lx, ly});
        if (!elem_const.has_value()) continue;
        const Element& el       = elem_const->get();
        const ElementBase& base = (*m_registry)[el.base_id];
        if (base.actions.empty()) continue;

        // Use the highest (most frequent) RandomTick intensity across all actions
        int best_intensity = 0;
        for (const auto& act : base.actions) {
            for (const auto& cond : act.conditions) {
                if (auto* rt = std::get_if<RandomTick>(&cond)) {
                    if (best_intensity == 0 || rt->intensity < best_intensity) best_intensity = rt->intensity;
                }
            }
        }
        if (best_intensity <= 0) continue;

        int cells = cell_counts[static_cast<std::size_t>(std::min(best_intensity, 5))];
        if (cells <= 0) continue;

        float pick_chance = static_cast<float>(cells) / static_cast<float>(cw_u32 * cw_u32);
        static thread_local std::uniform_real_distribution<float> dist{0.0f, 1.0f};
        if (dist(rng) > pick_chance) continue;

        std::int64_t wx = static_cast<std::int64_t>(cpos[0]) * cw + static_cast<std::int64_t>(lx);
        std::int64_t wy = static_cast<std::int64_t>(cpos[1]) * cw + static_cast<std::int64_t>(ly);

        // Only NOW grab mut access to element + thermal — marks them modified.
        Element* el_mut        = get_elem_ptr(wx, wy);
        ThermalCell* therm_mut = get_thermal_ptr(wx, wy);
        if (!el_mut || !therm_mut) continue;
        try_transition(wx, wy, *el_mut, *therm_mut, base);
        ++ticked;
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
        } else if (s == CellState::Blocked && m_world->missing_chunk_as_solid()) {
            b_lr_not_freefall++;
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
        } else if (s == CellState::Blocked && m_world->missing_chunk_as_solid()) {
            b_lr_not_freefall++;
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
        if (!valid(bx, by)) return;  // missing chunk: stay asleep (solid wall or out-of-bounds)
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
            } else if (s == CellState::Blocked && m_world->missing_chunk_as_solid()) {
                below_blocked = true;
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
                // Lighter material: push upward first if liquid, then swap.
                if (hb.type == ElementType::Liquid) {
                    for (auto [px, py] : std::array<std::pair<std::int64_t, std::int64_t>, 3>{{
                             {hx + ag.x, hy + ag.y},
                             {hx + ag.x + lp.x, hy + ag.y + lp.y},
                             {hx + ag.x + rp.x, hy + ag.y + rp.y},
                         }}) {
                        if (valid(px, py) && cell_state(px, py) == CellState::EmptyInChunk) {
                            swap_cells(hx, hy, px, py);
                            break;
                        }
                    }
                }
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
                    if (!valid(tx, ty)) continue;
                    auto ts        = cell_state(tx, ty);
                    bool can       = (ts == CellState::EmptyInChunk);
                    bool is_liquid = false;
                    if (!can && ts == CellState::Occupied) {
                        Element* nc = get_elem_ptr(tx, ty);
                        is_liquid   = nc && (*m_registry)[nc->base_id].type == ElementType::Liquid;
                        can         = is_liquid;
                    }
                    if (!can) continue;
                    // Push liquid upward (anti-gravity) before sliding into its cell.
                    if (is_liquid) {
                        for (auto [px, py] : std::array<std::pair<std::int64_t, std::int64_t>, 3>{{
                                 {tx + ag.x, ty + ag.y},
                                 {tx + ag.x + lp.x, ty + ag.y + lp.y},
                                 {tx + ag.x + rp.x, ty + ag.y + rp.y},
                             }}) {
                            if (valid(px, py) && cell_state(px, py) == CellState::EmptyInChunk) {
                                swap_cells(tx, ty, px, py);
                                break;
                            }
                        }
                    }
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
            } else if (hb.type == ElementType::Gas && cell) {
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
                if (nc) {
                    const ElementBase& nb = (*m_registry)[nc->base_id];
                    can                   = (nb.type == ElementType::Liquid && nb.density < base_elem.density) ||
                                            nb.type == ElementType::Gas;
                }
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
    glm::vec2 horiz = glm::vec2{(float)lp.x, (float)lp.y} * rand_f(rng) * 1.0f;
    float vlen      = glm::length(cell->velocity);
    glm::vec2 drag  = vlen > 0.001f ? -0.1f * vlen * cell->velocity / 20.0f : glm::vec2{};

    cell->velocity += (grav + horiz) * delta + drag;
    cell->velocity *= 0.9f;
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

    // Horizontal spread — only when blocked (raycast hit something)
    if (!moved && cell && res.hit.has_value()) {
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
    constexpr float delta       = 1.0f / 60.0f;

    // ── 1. Collect and shuffle chunk coordinates ──────────────────────────────
    auto offset_coords =
        std::ranges::to<std::vector>(std::views::cartesian_product(std::views::iota(0, 3), std::views::iota(0, 3)));
    static auto rng = std::mt19937{std::random_device{}()};
    std::shuffle(offset_coords.begin(), offset_coords.end(), rng);

    auto& pool = tasks::ComputeTaskPool::get();

    // ── 2. Process ALL chunks in 3×3 modulo groups ────────────────────────────
    for (auto&& [rx, ry] : offset_coords) {
        thread_local static std::vector<tasks::Task<void>> group_tasks;
        for (auto&& cpos : std::views::filter(iter_chunk_pos(), [rx, ry](auto&& cpos) {
                 return ((cpos[0] % 3 + 3) % 3 == rx && (cpos[1] % 3 + 3) % 3 == ry);
             })) {
            group_tasks.push_back(pool.spawn([cpos, cw, tick, delta, this] {
                auto chunk_mut = get_chunk_mut(cpos);
                if (!chunk_mut.has_value()) return;
                auto& chunk    = chunk_mut->get();
                auto elem_view = grid::chunk_element<Element>(chunk);
                auto air_view  = grid::chunk_element<AirCell>(chunk);

                // Check dirty status (const read — thread-safe)
                bool is_dirty = false;
                auto dr_opt   = m_chunk_dirty_rects.get(cpos);
                if (dr_opt.has_value()) {
                    is_dirty = dr_opt->get()->active();
                }

                // ── Heat transfer (every chunk, every tick) ─────────────────
                // Iterate const — heat-only updates must NOT mark the element grid modified.
                for (auto&& [lpos, elem] : elem_view.iter()) {
                    (void)elem;
                    std::int64_t wx = static_cast<std::int64_t>(cpos[0]) * cw + static_cast<std::int64_t>(lpos[0]);
                    std::int64_t wy = static_cast<std::int64_t>(cpos[1]) * cw + static_cast<std::int64_t>(lpos[1]);
                    spread_heat(wx, wy, delta);
                }

                // ── Random ticks (every chunk, every tick) ─────────────────
                random_tick_chunk(cpos, elem_view);

                if (is_dirty) {
                    // Snapshot element positions
                    thread_local static std::vector<std::array<std::int64_t, 2>> positions;
                    for (auto&& [lpos, _] : elem_view.iter()) {
                        positions.push_back({
                            static_cast<std::int64_t>(cpos[0]) * cw + static_cast<std::int64_t>(lpos[0]),
                            static_cast<std::int64_t>(cpos[1]) * cw + static_cast<std::int64_t>(lpos[1]),
                        });
                    }
                    for (auto&& [x, y] : positions) {
                        step_particle(x, y, tick);
                    }
                    positions.clear();

                    // Clear `updated` flags on every live cell in this chunk.
                    // Only run on dirty chunks so quiescent chunks don't get their
                    // element grid marked modified for nothing.
                    for (auto&& [lpos, elem] : elem_view.iter_mut()) {
                        (void)lpos;
                        elem.set_updated(false);
                    }

                    // Full air simulation
                    step_air_full(air_view, cpos, delta);
                } else {
                    // Simplified air update only
                    step_air_decay(air_view, cpos, delta);
                }
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
