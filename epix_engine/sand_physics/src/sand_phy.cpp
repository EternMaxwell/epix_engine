#include "epix/world/sand_physics.h"

struct ChunkConverter {
    const epix::world::sand::components::Simulation& sim;
    const epix::world::sand::components::Simulation::Chunk& chunk;

    bool contains(int x, int y) const {
        auto& cell = chunk.get(x, y);
        if (!cell) return false;
        auto& elem = sim.registry().get_elem(cell.elem_id);
        if (elem.is_gas() || elem.is_liquid()) return false;
        if (elem.is_powder() && cell.freefall) return false;
        return true;
    }
    glm::ivec2 size() const { return chunk.size(); }
};

namespace epix::world::sand_physics {
EPIX_API std::vector<std::vector<std::vector<glm::ivec2>>> get_chunk_collision(
    const epix::world::sand::components::Simulation& sim,
    const epix::world::sand::components::Simulation::Chunk& chunk
) {
    ChunkConverter grid{sim, chunk};
    return epix::utils::grid2d::get_polygon_simplified_multi(grid, 0.5f);
}
EPIX_API SimulationCollisions<void>::SimulationCollisions()
    : thread_pool(
          std::make_unique<BS::thread_pool>(std::thread::hardware_concurrency())
      ) {}
EPIX_API void SimulationCollisions<void>::sync(
    const epix::world::sand::components::Simulation& sim
) {
    for (auto [pos, chunk] : sim.chunk_map()) {
        collisions.try_emplace(
            pos.x, pos.y, SimulationCollisions::ChunkCollisions{}
        );
    }
    for (auto [pos, chunk] : sim.chunk_map()) {
        if (!collisions.contains(pos.x, pos.y)) continue;
        if (!chunk.should_update()) continue;
        modified.insert(pos);
        thread_pool->submit_task([this, &sim, &chunk, pos]() {
            auto collisions = get_chunk_collision(sim, chunk);
            this->collisions.get(pos.x, pos.y)->collisions = collisions;
        });
    }
    thread_pool->wait();
}

EPIX_API SimulationCollisionGeneral::PositionConverter
SimulationCollisionGeneral::pos_converter(
    int chunk_size, float cell_size, const glm::ivec2& offset
) {
    return {chunk_size, cell_size, offset};
}
EPIX_API SimulationCollisionGeneral::PositionConverter
SimulationCollisionGeneral::pos_converter(
    int chunk_size, float cell_size, int offset_x, int offset_y
) {
    return {chunk_size, cell_size, {offset_x, offset_y}};
}
EPIX_API SimulationCollisionGeneral::SimulationCollisionGeneral()
    : SimulationCollisions() {}
EPIX_API void SimulationCollisionGeneral::sync(
    const epix::world::sand::components::Simulation& sim
) {
    SimulationCollisions::sync(sim);
}
EPIX_API void SimulationCollisionGeneral::sync(
    b2WorldId world, const PositionConverter& converter
) {
    for (auto&& pos : modified) {
        auto real_pos = glm::ivec2(
            converter.chunk_size * pos.x - converter.offset.x,
            converter.chunk_size * pos.y - converter.offset.y
        );
        auto&& chunk_collision_opt = collisions(pos.x, pos.y);
        if (!chunk_collision_opt) continue;
        auto&& chunk_collision = *chunk_collision_opt;
        auto&& body_id         = chunk_collision.user_data.first;
        auto&& chain_ids       = chunk_collision.user_data.second;
        if (b2Body_IsValid(body_id)) {
            for (auto&& chain_id : chain_ids) {
                b2DestroyChain(chain_id);
            }
            chain_ids.clear();
        } else {
            auto body_def = b2DefaultBodyDef();
            body_def.type = b2_staticBody;
            body_id       = b2CreateBody(world, &body_def);
        }
        if (chunk_collision.collisions.empty()) continue;
        for (auto&& polygon : chunk_collision.collisions) {
            if (polygon.empty()) continue;
            auto outline   = polygon[0];
            b2Vec2* points = new b2Vec2[outline.size()];
            for (size_t i = 0; i < outline.size(); i++) {
                points[i] = {
                    converter.cell_size *
                        (outline[outline.size() - i - 1].x + real_pos.x),
                    converter.cell_size *
                        (outline[outline.size() - i - 1].y + real_pos.y)
                };
            }
            auto chain_def   = b2DefaultChainDef();
            chain_def.points = points;
            chain_def.count  = outline.size();
            chain_ids.push_back(b2CreateChain(body_id, &chain_def));
            for (size_t i = 1; i < polygon.size(); i++) {
                auto hole           = polygon[i];
                b2Vec2* hole_points = new b2Vec2[hole.size()];
                for (size_t j = 0; j < hole.size(); j++) {
                    hole_points[j] = {
                        converter.cell_size * (hole[j].x + real_pos.x),
                        converter.cell_size * (hole[j].y + real_pos.y)
                    };
                }
                chain_def.points = hole_points;
                chain_def.count  = hole.size();
                chain_ids.push_back(b2CreateChain(body_id, &chain_def));
            }
        }
    }
    clear_modified();
}
}  // namespace epix::world::sand_physics