#pragma once

#include <epix/pixel_b2d.h>
#include <epix/world/sand.h>

#include <glm/glm.hpp>

namespace epix::world::sync::b2d2sand {
struct PixPhy2Simulation {
    struct PosConverter {
        glm::vec2 offset;  // offset of simulation world origin in box2d world
        float cell_size;   // size of a cell in simulation in box2d world
    };

   private:
    std::vector<std::vector<std::array<int, 2>>>
        _occupies;  // each body links to a vector
    std::vector<std::pair<glm::vec2, glm::vec2>> _last_transforms;

   public:
    void sync(
        const epix::world::pixel_b2d::PixPhyWorld& world,
        epix::world::sand::components::Simulation& sim,
        const PosConverter& converter,
        float max_diff = 0.5f
    ) {
        size_t index = 0;
        world.draw_pixel_rasterized(
            converter.offset, converter.cell_size,
            [&](const glm::vec2& pos, const glm::vec2& rot, bool awake,
                size_t i) {
                index = i;
                if (awake) {
                    if (index >= _occupies.size()) {
                        _occupies.resize(index + 1);
                        _last_transforms.resize(index + 1);
                        _last_transforms[index] = {pos, rot};
                    }
                    if (glm::length(_last_transforms[index].first - pos) <
                            max_diff &&
                        glm::length(_last_transforms[index].second - rot) <
                            max_diff / 5)
                        return false;
                    _last_transforms[index] = {pos, rot};
                    for (auto&& [x, y] : _occupies[index]) {
                        if (!sim.valid(x, y)) continue;
                        if (!sim.contain_cell(x, y)) continue;
                        sim.remove(x, y);
                    }
                    _occupies[index].clear();
                }
                return awake;
            },
            [&](const glm::vec2& pos, const glm::vec4& color) {
                int x = std::round(pos.x / converter.cell_size);
                int y = std::round(pos.y / converter.cell_size);
                if (!sim.valid(x, y)) return;
                if (sim.contain_cell(x, y)) return;
                sim.create(
                    x, y, epix::world::sand::components::CellDef(0)
                );  // 0 is placeholder by default in elem registry
                _occupies[index].push_back({x, y});
            }
        );
    }
};
}  // namespace epix::world::sync::b2d2sand