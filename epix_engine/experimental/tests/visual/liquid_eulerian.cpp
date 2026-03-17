import std;
import glm;
import epix.assets;
import epix.core;
import epix.window;
import epix.glfw.core;
import epix.glfw.render;
import epix.render;
import epix.core_graph;
import epix.mesh;
import epix.transform;
import epix.input;
import epix.extension.grid;
import epix.sprite;
import epix.text;

#include "../../../text/tests/font_array.hpp"

namespace {
using namespace core;
using ext::grid::packed_grid;

constexpr std::uint32_t kGridWidth  = 120;
constexpr std::uint32_t kGridHeight = 80;
constexpr float kCellSize           = 10.0f;
constexpr float kBaseDt             = 1.0f / 60.0f;

struct HudTextTag {};

enum class PaintTool {
    Water,
    Wall,
    Eraser,
};

struct LiquidSim {
    packed_grid<2, float> density{{kGridWidth, kGridHeight}, 0.0f};
    packed_grid<2, float> density_next{{kGridWidth, kGridHeight}, 0.0f};
    packed_grid<2, float> density_delta{{kGridWidth, kGridHeight}, 0.0f};
    packed_grid<2, float> pressure{{kGridWidth, kGridHeight}, 0.0f};
    packed_grid<2, std::uint8_t> solid{{kGridWidth, kGridHeight}, 0};

    packed_grid<2, float> u{{kGridWidth + 1, kGridHeight}, 0.0f};
    packed_grid<2, float> v{{kGridWidth, kGridHeight + 1}, 0.0f};
    packed_grid<2, float> u_prev{{kGridWidth + 1, kGridHeight}, 0.0f};
    packed_grid<2, float> v_prev{{kGridWidth, kGridHeight + 1}, 0.0f};

    PaintTool tool      = PaintTool::Water;
    bool paused         = false;
    int brush_radius    = 3;
    float dt_scale      = 1.0f;
    float gravity       = 0.5f;
    int pressure_iters  = 20;
    float max_velocity  = 8.0f;
    float velocity_damp = 0.999f;
    float density_decay = 1.0f;

    float target_mass  = 0.0f;
    float current_mass = 0.0f;
    float last_max_vel = 0.0f;
    int last_substeps  = 0;

    static bool in_bounds(int x, int y) {
        return x >= 0 && y >= 0 && x < static_cast<int>(kGridWidth) && y < static_cast<int>(kGridHeight);
    }

    static std::uint32_t to_u32(int value) { return static_cast<std::uint32_t>(value); }

    float get_density(int x, int y) const {
        if (!in_bounds(x, y)) return 0.0f;
        return density.get({to_u32(x), to_u32(y)}).value().get();
    }

    float get_pressure(int x, int y) const {
        if (!in_bounds(x, y)) return 0.0f;
        return pressure.get({to_u32(x), to_u32(y)}).value().get();
    }

    float get_u(int x, int y) const {
        if (x < 0 || y < 0 || x > static_cast<int>(kGridWidth) || y >= static_cast<int>(kGridHeight)) return 0.0f;
        return u.get({to_u32(x), to_u32(y)}).value().get();
    }

    float get_v(int x, int y) const {
        if (x < 0 || y < 0 || x >= static_cast<int>(kGridWidth) || y > static_cast<int>(kGridHeight)) return 0.0f;
        return v.get({to_u32(x), to_u32(y)}).value().get();
    }

    std::uint8_t get_solid(int x, int y) const {
        if (!in_bounds(x, y)) return 1;
        return solid.get({to_u32(x), to_u32(y)}).value().get();
    }

    void set_density(int x, int y, float value) {
        if (!in_bounds(x, y)) return;
        (void)density.set({to_u32(x), to_u32(y)}, std::clamp(value, 0.0f, 1.25f));
    }

    void set_pressure(int x, int y, float value) {
        if (!in_bounds(x, y)) return;
        (void)pressure.set({to_u32(x), to_u32(y)}, value);
    }

    void set_solid(int x, int y, std::uint8_t value) {
        if (!in_bounds(x, y)) return;
        (void)solid.set({to_u32(x), to_u32(y)}, value);
    }

    void set_u(int x, int y, float value) {
        if (x < 0 || y < 0 || x > static_cast<int>(kGridWidth) || y >= static_cast<int>(kGridHeight)) return;
        (void)u.set({to_u32(x), to_u32(y)}, value);
    }

    void set_v(int x, int y, float value) {
        if (x < 0 || y < 0 || x >= static_cast<int>(kGridWidth) || y > static_cast<int>(kGridHeight)) return;
        (void)v.set({to_u32(x), to_u32(y)}, value);
    }

    float cell_u(int x, int y) const { return 0.5f * (get_u(x, y) + get_u(x + 1, y)); }
    float cell_v(int x, int y) const { return 0.5f * (get_v(x, y) + get_v(x, y + 1)); }

    void reset() {
        density.clear();
        density_next.clear();
        density_delta.clear();
        pressure.clear();
        solid.clear();
        u.clear();
        v.clear();
        u_prev.clear();
        v_prev.clear();

        for (std::uint32_t x = 0; x < kGridWidth; ++x) {
            set_solid(static_cast<int>(x), 0, 1);
            set_solid(static_cast<int>(x), static_cast<int>(kGridHeight - 1), 1);
        }
        for (std::uint32_t y = 0; y < kGridHeight; ++y) {
            set_solid(0, static_cast<int>(y), 1);
            set_solid(static_cast<int>(kGridWidth - 1), static_cast<int>(y), 1);
        }

        for (int x = 20; x < 40; ++x) {
            for (int y = 45; y < 70; ++y) {
                set_density(x, y, 1.0f);
            }
        }

        for (int x = 54; x < 78; ++x) set_solid(x, 22, 1);

        update_mass();
        target_mass = current_mass;
    }

    void clear_fluid() {
        density.clear();
        density_next.clear();
        density_delta.clear();
        u.clear();
        v.clear();
        u_prev.clear();
        v_prev.clear();
        pressure.clear();
        update_mass();
        target_mass = 0.0f;
    }

    static float bilerp(float a00, float a10, float a01, float a11, float tx, float ty) {
        const float x0 = std::lerp(a00, a10, tx);
        const float x1 = std::lerp(a01, a11, tx);
        return std::lerp(x0, x1, ty);
    }

    float sample_u_prev(float x, float y) const {
        x = std::clamp(x, 0.0f, static_cast<float>(kGridWidth));
        y = std::clamp(y, 0.0f, static_cast<float>(kGridHeight - 1));

        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = std::min(x0 + 1, static_cast<int>(kGridWidth));
        const int y1 = std::min(y0 + 1, static_cast<int>(kGridHeight - 1));

        const float tx = x - static_cast<float>(x0);
        const float ty = y - static_cast<float>(y0);

        auto read = [&](int px, int py) { return u_prev.get({to_u32(px), to_u32(py)}).value().get(); };
        return bilerp(read(x0, y0), read(x1, y0), read(x0, y1), read(x1, y1), tx, ty);
    }

    float sample_v_prev(float x, float y) const {
        x = std::clamp(x, 0.0f, static_cast<float>(kGridWidth - 1));
        y = std::clamp(y, 0.0f, static_cast<float>(kGridHeight));

        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = std::min(x0 + 1, static_cast<int>(kGridWidth - 1));
        const int y1 = std::min(y0 + 1, static_cast<int>(kGridHeight));

        const float tx = x - static_cast<float>(x0);
        const float ty = y - static_cast<float>(y0);

        auto read = [&](int px, int py) { return v_prev.get({to_u32(px), to_u32(py)}).value().get(); };
        return bilerp(read(x0, y0), read(x1, y0), read(x0, y1), read(x1, y1), tx, ty);
    }

    void update_mass() {
        current_mass = 0.0f;
        for (int y = 0; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
                if (get_solid(x, y) != 0) continue;
                current_mass += get_density(x, y);
            }
        }
    }

    void adjust_target_mass_for_cell(int x, int y, float before, float after) {
        if (get_solid(x, y) != 0) return;
        target_mass += (after - before);
        if (target_mass < 0.0f) target_mass = 0.0f;
    }

    void apply_brush(int cx, int cy, PaintTool active_tool) {
        for (int dy = -brush_radius; dy <= brush_radius; ++dy) {
            for (int dx = -brush_radius; dx <= brush_radius; ++dx) {
                if (dx * dx + dy * dy > brush_radius * brush_radius) continue;
                const int x = cx + dx;
                const int y = cy + dy;
                if (!in_bounds(x, y)) continue;
                if (x == 0 || y == 0 || x == static_cast<int>(kGridWidth - 1) ||
                    y == static_cast<int>(kGridHeight - 1)) {
                    continue;
                }

                const float before = get_density(x, y);

                if (active_tool == PaintTool::Eraser) {
                    set_solid(x, y, 0);
                    set_density(x, y, 0.0f);
                    adjust_target_mass_for_cell(x, y, before, 0.0f);
                    continue;
                }

                if (active_tool == PaintTool::Wall) {
                    if (get_solid(x, y) == 0) {
                        set_solid(x, y, 1);
                        set_density(x, y, 0.0f);
                        adjust_target_mass_for_cell(x, y, before, 0.0f);
                    }
                    continue;
                }

                set_solid(x, y, 0);
                set_density(x, y, 1.0f);
                adjust_target_mass_for_cell(x, y, before, 1.0f);
            }
        }
    }

    void enforce_solid_boundaries() {
        for (int y = 0; y < static_cast<int>(kGridHeight); ++y) {
            set_u(0, y, 0.0f);
            set_u(static_cast<int>(kGridWidth), y, 0.0f);
        }
        for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
            set_v(x, 0, 0.0f);
            set_v(x, static_cast<int>(kGridHeight), 0.0f);
        }

        for (int y = 0; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x <= static_cast<int>(kGridWidth); ++x) {
                const bool left_solid  = get_solid(x - 1, y) != 0;
                const bool right_solid = get_solid(x, y) != 0;
                if (left_solid || right_solid) set_u(x, y, 0.0f);
            }
        }
        for (int y = 0; y <= static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
                const bool bottom_solid = get_solid(x, y - 1) != 0;
                const bool top_solid    = get_solid(x, y) != 0;
                if (bottom_solid || top_solid) set_v(x, y, 0.0f);
            }
        }
    }

    void restore_mass(float dt) {
        update_mass();
        if (target_mass <= 0.0001f) {
            if (current_mass > 0.001f) target_mass = current_mass;
            return;
        }

        float diff                 = target_mass - current_mass;
        const float max_correction = target_mass * 0.10f * dt;
        diff                       = std::clamp(diff, -max_correction, max_correction);
        if (std::abs(diff) < 0.0001f) return;

        int active_cells = 0;
        for (int y = 1; y < static_cast<int>(kGridHeight - 1); ++y) {
            for (int x = 1; x < static_cast<int>(kGridWidth - 1); ++x) {
                if (get_solid(x, y) != 0) continue;
                if (get_density(x, y) > 0.001f) active_cells++;
            }
        }
        if (active_cells == 0) return;

        const float add_per_cell = diff / static_cast<float>(active_cells);
        for (int y = 1; y < static_cast<int>(kGridHeight - 1); ++y) {
            for (int x = 1; x < static_cast<int>(kGridWidth - 1); ++x) {
                if (get_solid(x, y) != 0) continue;
                if (get_density(x, y) <= 0.001f) continue;
                set_density(x, y, get_density(x, y) + add_per_cell);
            }
        }
        update_mass();
    }

    void apply_gravity(float dt) {
        for (int y = 1; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
                if (get_solid(x, y - 1) != 0 || get_solid(x, y) != 0) continue;
                const float d0           = get_density(x, y - 1);
                const float d1           = get_density(x, y);
                const float fluid_weight = 0.5f * (d0 + d1);
                if (fluid_weight <= 0.0001f) continue;
                set_v(x, y, get_v(x, y) - gravity * dt * std::min(fluid_weight, 1.0f));
            }
        }
    }

    void project_pressure(float dt) {
        pressure.clear();
        const float omega = 1.8f;

        for (int iter = 0; iter < pressure_iters; ++iter) {
            const bool reverse = (iter % 2) != 0;
            const int y_begin  = reverse ? static_cast<int>(kGridHeight - 2) : 1;
            const int y_end    = reverse ? 0 : static_cast<int>(kGridHeight - 1);
            const int y_step   = reverse ? -1 : 1;
            const int x_begin  = reverse ? static_cast<int>(kGridWidth - 2) : 1;
            const int x_end    = reverse ? 0 : static_cast<int>(kGridWidth - 1);
            const int x_step   = reverse ? -1 : 1;

            for (int y = y_begin; y != y_end; y += y_step) {
                for (int x = x_begin; x != x_end; x += x_step) {
                    if (get_solid(x, y) != 0) {
                        set_pressure(x, y, 0.0f);
                        continue;
                    }

                    const float div = get_u(x + 1, y) - get_u(x, y) + get_v(x, y + 1) - get_v(x, y);
                    float sum       = 0.0f;
                    float count     = 0.0f;

                    if (get_solid(x - 1, y) == 0) {
                        sum += get_pressure(x - 1, y);
                        count += 1.0f;
                    }
                    if (get_solid(x + 1, y) == 0) {
                        sum += get_pressure(x + 1, y);
                        count += 1.0f;
                    }
                    if (get_solid(x, y - 1) == 0) {
                        sum += get_pressure(x, y - 1);
                        count += 1.0f;
                    }
                    if (get_solid(x, y + 1) == 0) {
                        sum += get_pressure(x, y + 1);
                        count += 1.0f;
                    }

                    if (count <= 0.0f) continue;
                    const float p_old = get_pressure(x, y);
                    const float p_new = (sum - div) / count;
                    set_pressure(x, y, std::lerp(p_old, p_new, omega));
                }
            }
        }

        const float pressure_scale = 1.0f / std::max(dt, 1.0e-4f);

        for (int y = 0; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 1; x < static_cast<int>(kGridWidth); ++x) {
                if (get_solid(x - 1, y) != 0 || get_solid(x, y) != 0) {
                    set_u(x, y, 0.0f);
                    continue;
                }
                const float grad = get_pressure(x, y) - get_pressure(x - 1, y);
                set_u(x, y, (get_u(x, y) - grad * pressure_scale) * velocity_damp);
            }
        }

        for (int y = 1; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
                if (get_solid(x, y - 1) != 0 || get_solid(x, y) != 0) {
                    set_v(x, y, 0.0f);
                    continue;
                }
                const float grad = get_pressure(x, y) - get_pressure(x, y - 1);
                set_v(x, y, (get_v(x, y) - grad * pressure_scale) * velocity_damp);
            }
        }

        enforce_solid_boundaries();
    }

    void advect_velocity(float dt) {
        u_prev = u;
        v_prev = v;

        for (int y = 0; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x <= static_cast<int>(kGridWidth); ++x) {
                if (x > 0 && x < static_cast<int>(kGridWidth) && (get_solid(x - 1, y) != 0 || get_solid(x, y) != 0)) {
                    set_u(x, y, 0.0f);
                    continue;
                }

                const float px = static_cast<float>(x);
                const float py = static_cast<float>(y) + 0.5f;

                const float vx = sample_u_prev(px, static_cast<float>(y));
                const float vy = sample_v_prev(std::clamp(px - 0.5f, 0.0f, static_cast<float>(kGridWidth - 1)), py);

                const float bx = px - vx * dt;
                const float by = py - vy * dt - 0.5f;
                set_u(x, y, sample_u_prev(bx, by));
            }
        }

        for (int y = 0; y <= static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
                if (y > 0 && y < static_cast<int>(kGridHeight) && (get_solid(x, y - 1) != 0 || get_solid(x, y) != 0)) {
                    set_v(x, y, 0.0f);
                    continue;
                }

                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y);

                const float vx = sample_u_prev(px, std::clamp(py - 0.5f, 0.0f, static_cast<float>(kGridHeight - 1)));
                const float vy = sample_v_prev(static_cast<float>(x), py);

                const float bx = px - vx * dt - 0.5f;
                const float by = py - vy * dt;
                set_v(x, y, sample_v_prev(bx, by));
            }
        }

        enforce_solid_boundaries();
    }

    void advect_density_flux(float dt) {
        density_delta.clear();

        for (int y = 0; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 1; x < static_cast<int>(kGridWidth); ++x) {
                if (get_solid(x - 1, y) != 0 || get_solid(x, y) != 0) continue;

                const float vel  = get_u(x, y);
                const float move = std::clamp(std::abs(vel) * dt, 0.0f, 1.0f);
                if (move <= 0.0f) continue;

                const int from_x = vel >= 0.0f ? x - 1 : x;
                const int to_x   = vel >= 0.0f ? x : x - 1;

                const float source_density = get_density(from_x, y);
                const float transfer       = std::min(source_density, source_density * move);
                if (transfer <= 0.0f) continue;

                const float from_delta = density_delta.get({to_u32(from_x), to_u32(y)}).value().get();
                const float to_delta   = density_delta.get({to_u32(to_x), to_u32(y)}).value().get();
                (void)density_delta.set({to_u32(from_x), to_u32(y)}, from_delta - transfer);
                (void)density_delta.set({to_u32(to_x), to_u32(y)}, to_delta + transfer);
            }
        }

        for (int y = 1; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
                if (get_solid(x, y - 1) != 0 || get_solid(x, y) != 0) continue;

                const float vel  = get_v(x, y);
                const float move = std::clamp(std::abs(vel) * dt, 0.0f, 1.0f);
                if (move <= 0.0f) continue;

                const int from_y = vel >= 0.0f ? y - 1 : y;
                const int to_y   = vel >= 0.0f ? y : y - 1;

                const float source_density = get_density(x, from_y);
                const float transfer       = std::min(source_density, source_density * move);
                if (transfer <= 0.0f) continue;

                const float from_delta = density_delta.get({to_u32(x), to_u32(from_y)}).value().get();
                const float to_delta   = density_delta.get({to_u32(x), to_u32(to_y)}).value().get();
                (void)density_delta.set({to_u32(x), to_u32(from_y)}, from_delta - transfer);
                (void)density_delta.set({to_u32(x), to_u32(to_y)}, to_delta + transfer);
            }
        }

        for (int y = 0; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
                if (get_solid(x, y) != 0) {
                    (void)density_next.set({to_u32(x), to_u32(y)}, 0.0f);
                    continue;
                }
                const float base  = get_density(x, y);
                const float delta = density_delta.get({to_u32(x), to_u32(y)}).value().get();
                (void)density_next.set({to_u32(x), to_u32(y)}, std::clamp((base + delta) * density_decay, 0.0f, 1.25f));
            }
        }

        density = density_next;
    }

    void clamp_velocity() {
        for (int y = 0; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x <= static_cast<int>(kGridWidth); ++x) {
                set_u(x, y, std::clamp(get_u(x, y), -max_velocity, max_velocity));
            }
        }
        for (int y = 0; y <= static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
                set_v(x, y, std::clamp(get_v(x, y), -max_velocity, max_velocity));
            }
        }
    }

    float compute_max_velocity() {
        float max_vel = 0.0f;
        for (int y = 0; y < static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x <= static_cast<int>(kGridWidth); ++x) {
                max_vel = std::max(max_vel, std::abs(get_u(x, y)));
            }
        }
        for (int y = 0; y <= static_cast<int>(kGridHeight); ++y) {
            for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
                max_vel = std::max(max_vel, std::abs(get_v(x, y)));
            }
        }
        last_max_vel = max_vel;
        return max_vel;
    }

    void physics_step(float dt) {
        restore_mass(dt);
        apply_gravity(dt);
        project_pressure(dt);
        clamp_velocity();
        advect_velocity(dt);
        project_pressure(dt);
        clamp_velocity();
        advect_density_flux(dt);
        update_mass();
    }

    void solve(float total_dt) {
        if (paused) {
            last_substeps = 0;
            update_mass();
            return;
        }

        const float max_vel = std::max(0.1f, compute_max_velocity());
        float max_step_dt   = 0.8f / max_vel;
        max_step_dt         = std::min(max_step_dt, 0.2f);

        float remaining = total_dt;
        int substeps    = 0;
        while (remaining > 1.0e-4f && substeps < 12) {
            const float step_dt = std::min(remaining, max_step_dt);
            physics_step(step_dt);
            remaining -= step_dt;
            substeps++;
        }
        last_substeps = substeps;
    }
};

struct LiquidSimState {
    LiquidSim sim;
    assets::Handle<mesh::Mesh> mesh_handle;
};

glm::vec2 screen_to_world(glm::vec2 screen_pos,
                          glm::vec2 window_size,
                          const render::camera::Camera& camera,
                          const render::camera::Projection& projection,
                          const transform::Transform& cam_transform) {
    (void)projection;
    const float ndc_x = (screen_pos.x / window_size.x) * 2.0f - 1.0f;
    const float ndc_y = 1.0f - (screen_pos.y / window_size.y) * 2.0f;

    const glm::mat4 proj_matrix = camera.computed.projection;
    const glm::mat4 view_matrix = glm::inverse(cam_transform.to_matrix());
    const glm::mat4 vp_inv      = glm::inverse(proj_matrix * view_matrix);

    const glm::vec4 world = vp_inv * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    return glm::vec2(world.x / world.w, world.y / world.w);
}

std::optional<std::pair<int, int>> world_to_cell(glm::vec2 world_pos) {
    const float world_w = static_cast<float>(kGridWidth) * kCellSize;
    const float world_h = static_cast<float>(kGridHeight) * kCellSize;
    const float min_x   = -0.5f * world_w;
    const float min_y   = -0.5f * world_h;

    const int gx = static_cast<int>(std::floor((world_pos.x - min_x) / kCellSize));
    const int gy = static_cast<int>(std::floor((world_pos.y - min_y) / kCellSize));

    if (gx < 0 || gy < 0 || gx >= static_cast<int>(kGridWidth) || gy >= static_cast<int>(kGridHeight)) {
        return std::nullopt;
    }
    return std::pair{gx, gy};
}

mesh::Mesh build_liquid_mesh(const LiquidSim& sim) {
    const float world_w = static_cast<float>(kGridWidth) * kCellSize;
    const float world_h = static_cast<float>(kGridHeight) * kCellSize;
    const float min_x   = -0.5f * world_w;
    const float min_y   = -0.5f * world_h;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> colors;
    std::vector<std::uint32_t> indices;

    positions.reserve(static_cast<std::size_t>(kGridWidth * kGridHeight * 4));
    colors.reserve(static_cast<std::size_t>(kGridWidth * kGridHeight * 4));
    indices.reserve(static_cast<std::size_t>(kGridWidth * kGridHeight * 6));

    std::uint32_t base = 0;

    for (int y = 0; y < static_cast<int>(kGridHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kGridWidth); ++x) {
            const bool is_solid = sim.get_solid(x, y) != 0;
            const float d       = sim.get_density(x, y);
            if (!is_solid && d < 0.01f) continue;

            const float px0 = min_x + static_cast<float>(x) * kCellSize;
            const float py0 = min_y + static_cast<float>(y) * kCellSize;
            const float px1 = px0 + kCellSize;
            const float py1 = py0 + kCellSize;

            positions.push_back({px0, py0, 0.0f});
            positions.push_back({px1, py0, 0.0f});
            positions.push_back({px1, py1, 0.0f});
            positions.push_back({px0, py1, 0.0f});

            if (is_solid) {
                const glm::vec4 wall_color(0.22f, 0.23f, 0.25f, 1.0f);
                colors.push_back(wall_color);
                colors.push_back(wall_color);
                colors.push_back(wall_color);
                colors.push_back(wall_color);
            } else {
                const float speed = glm::length(glm::vec2(sim.cell_u(x, y), sim.cell_v(x, y)));
                const float t     = std::clamp(d, 0.0f, 1.0f);
                const float s     = std::clamp(speed * 0.08f, 0.0f, 1.0f);
                const glm::vec4 liquid_color(0.10f + 0.10f * s, 0.30f + 0.28f * s, 0.70f + 0.26f * t,
                                             0.40f + 0.60f * t);
                colors.push_back(liquid_color);
                colors.push_back(liquid_color);
                colors.push_back(liquid_color);
                colors.push_back(liquid_color);
            }

            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
            indices.push_back(base + 0);
            base += 4;
        }
    }

    return mesh::Mesh()
        .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
        .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
        .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
        .with_indices<std::uint32_t>(indices);
}

const char* tool_name(PaintTool tool) {
    switch (tool) {
        case PaintTool::Water:
            return "Water";
        case PaintTool::Wall:
            return "Wall";
        case PaintTool::Eraser:
            return "Eraser";
        default:
            return "Unknown";
    }
}

struct EulerianLiquidPlugin {
    void finish(core::App& app) {
        auto& world      = app.world_mut();
        auto& mesh_asset = world.resource_mut<assets::Assets<mesh::Mesh>>();
        auto& fonts      = world.resource_mut<assets::Assets<text::font::Font>>();

        world.spawn(core_graph::core_2d::Camera2DBundle{});

        LiquidSim sim;
        sim.reset();

        auto mesh_handle = mesh_asset.emplace(build_liquid_mesh(sim));
        world.spawn(mesh::Mesh2d{mesh_handle},
                    mesh::MeshMaterial2d{
                        .color      = glm::vec4(1.0f),
                        .alpha_mode = mesh::MeshAlphaMode2d::Blend,
                    },
                    transform::Transform{});

        text::font::Font font{std::make_unique<std::byte[]>(font_data_array_size), font_data_array_size};
        std::memcpy(font.data.get(), font_data_array, font_data_array_size);
        const auto font_handle = fonts.emplace(std::move(font));

        world.spawn(text::TextBundle{.text{"HUD"},
                                     .font{
                                         .font            = font_handle,
                                         .size            = 18.0f,
                                         .line_height     = 18.0f,
                                         .relative_height = false,
                                     },
                                     .layout{.justify = text::Justify::Left}},
                    text::Text2d{}, transform::Transform{.translation = glm::vec3(-580.0f, 380.0f, 0.2f)},
                    text::TextColor{.r = 0.92f, .g = 0.95f, .b = 1.0f, .a = 1.0f}, HudTextTag{});

        world.insert_resource(LiquidSimState{
            .sim         = std::move(sim),
            .mesh_handle = std::move(mesh_handle),
        });

        app.add_systems(
            core::Update,
            core::into([](core::ResMut<LiquidSimState> state,
                          core::Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                          core::Res<input::ButtonInput<input::KeyCode>> keys,
                          core::Query<core::Item<const window::CachedWindow&>, core::With<window::PrimaryWindow>>
                              window_query,
                          core::Query<core::Item<const render::camera::Camera&, const render::camera::Projection&,
                                                 const transform::Transform&>> camera_query,
                          core::Query<core::Item<core::Mut<text::Text>>, core::With<HudTextTag>> hud_query,
                          core::ResMut<assets::Assets<mesh::Mesh>> meshes) {
                if (keys->just_pressed(input::KeyCode::KeySpace)) state->sim.paused = !state->sim.paused;
                if (keys->just_pressed(input::KeyCode::KeyR)) state->sim.reset();
                if (keys->just_pressed(input::KeyCode::KeyC)) state->sim.clear_fluid();

                if (keys->just_pressed(input::KeyCode::Key1)) state->sim.tool = PaintTool::Water;
                if (keys->just_pressed(input::KeyCode::Key2)) state->sim.tool = PaintTool::Wall;
                if (keys->just_pressed(input::KeyCode::Key3)) state->sim.tool = PaintTool::Eraser;

                if (keys->just_pressed(input::KeyCode::KeyLeftBracket)) {
                    state->sim.brush_radius = std::max(1, state->sim.brush_radius - 1);
                }
                if (keys->just_pressed(input::KeyCode::KeyRightBracket)) {
                    state->sim.brush_radius = std::min(16, state->sim.brush_radius + 1);
                }

                if (keys->just_pressed(input::KeyCode::KeyMinus)) {
                    state->sim.dt_scale = std::max(0.2f, state->sim.dt_scale * 0.9f);
                }
                if (keys->just_pressed(input::KeyCode::KeyEqual)) {
                    state->sim.dt_scale = std::min(3.0f, state->sim.dt_scale * 1.1f);
                }

                if (keys->just_pressed(input::KeyCode::KeyG)) {
                    state->sim.gravity = std::max(0.05f, state->sim.gravity * 0.85f);
                }
                if (keys->just_pressed(input::KeyCode::KeyH)) {
                    state->sim.gravity = std::min(2.5f, state->sim.gravity * 1.15f);
                }

                if (keys->just_pressed(input::KeyCode::KeyJ)) {
                    state->sim.pressure_iters = std::max(4, state->sim.pressure_iters - 2);
                }
                if (keys->just_pressed(input::KeyCode::KeyK)) {
                    state->sim.pressure_iters = std::min(80, state->sim.pressure_iters + 2);
                }

                auto win_opt = window_query.single();
                auto cam_opt = camera_query.single();

                if (win_opt && cam_opt) {
                    auto&& [window]                   = *win_opt;
                    auto&& [cam, proj, cam_transform] = *cam_opt;

                    const auto [cursor_x, cursor_y] = window.cursor_pos;
                    const auto [win_w, win_h]       = window.size;
                    if (win_w > 0 && win_h > 0) {
                        const glm::vec2 world_pos = screen_to_world(
                            glm::vec2(static_cast<float>(cursor_x), static_cast<float>(cursor_y)),
                            glm::vec2(static_cast<float>(win_w), static_cast<float>(win_h)), cam, proj, cam_transform);

                        if (auto cell = world_to_cell(world_pos); cell.has_value()) {
                            const bool lmb = mouse_buttons->pressed(input::MouseButton::MouseButtonLeft);
                            const bool rmb = mouse_buttons->pressed(input::MouseButton::MouseButtonRight);

                            if (lmb || rmb) {
                                const PaintTool active_tool = rmb ? PaintTool::Eraser : state->sim.tool;
                                state->sim.apply_brush(cell->first, cell->second, active_tool);
                            }
                        }
                    }
                }

                state->sim.solve(kBaseDt * state->sim.dt_scale);
                (void)meshes->insert(state->mesh_handle.id(), build_liquid_mesh(state->sim));

                if (auto hud = hud_query.single(); hud.has_value()) {
                    auto&& [text_comp]          = *hud;
                    text_comp.get_mut().content = std::format(
                        "Eulerian Liquid (HTML-like)\n"
                        "Tool[1/2/3]: {}\n"
                        "LMB paint | RMB erase | Space pause | R reset | C clear\n"
                        "Brush[[]/]]: {}\n"
                        "dt[-/=]: {:.2f} | gravity[G/H]: {:.3f}\n"
                        "pressure[J/K]: {} | maxVel: {:.3f}\n"
                        "mass target: {:.2f} | actual: {:.2f}\n"
                        "substeps: {} | paused: {}",
                        tool_name(state->sim.tool), state->sim.brush_radius, state->sim.dt_scale, state->sim.gravity,
                        state->sim.pressure_iters, state->sim.last_max_vel, state->sim.target_mass,
                        state->sim.current_mass, state->sim.last_substeps, state->sim.paused ? "yes" : "no");
                }
            }).set_name("eulerian liquid update"));
    }
};
}  // namespace

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title =
        "Eulerian Liquid | 1 water 2 wall 3 eraser | [ ] brush | - = dt | G/H gravity | J/K pressure";
    primary_window.size = {1280, 720};

    app.add_plugins(window::WindowPlugin{
                        .primary_window = primary_window,
                        .exit_condition = window::ExitCondition::OnPrimaryClosed,
                    })
        .add_plugins(input::InputPlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::RenderPlugin{}.set_validation(0))
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshRenderPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(text::TextPlugin{})
        .add_plugins(text::TextRenderPlugin{})
        .add_plugins(EulerianLiquidPlugin{});

    app.run();
}
