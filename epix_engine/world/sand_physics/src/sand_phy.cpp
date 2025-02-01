#include <iostream>

#include "epix/world/sand_physics.h"

struct ChunkConverter {
    const epix::world::sand::components::Simulation& sim;
    const epix::world::sand::components::Simulation::Chunk& chunk;
    const bool should_update = chunk.should_update();

    bool contains(int x, int y) const {
        if (!chunk.contains(x, y)) return false;
        auto& cell = chunk.get(x, y);
        auto& elem = sim.registry().get_elem(cell.elem_id);
        if (elem.is_solid()) return true;
        if (elem.is_gas() || elem.is_liquid()) return false;
        if (elem.is_powder() && cell.freefall && should_update) return false;
        return true;
    }
    glm::ivec2 size() const { return chunk.size(); }
    int size(int i) const {
        if (i == 0) return chunk.width;
        return chunk.height;
    }
};

namespace epix::world::sand_physics {
EPIX_API bool get_chunk_collision(
    const epix::world::sand::components::Simulation& sim,
    const epix::world::sand::components::Simulation::Chunk& chunk,
    std::vector<std::vector<std::vector<glm::ivec2>>>& polygons
) {
    ChunkConverter grid{sim, chunk};
    return epix::utils::grid::get_polygon_multi(grid, polygons);
}
EPIX_API SimulationCollisions<void>::SimulationCollisions()
    : thread_pool(
          std::make_unique<thread_pool_t>(std::thread::hardware_concurrency())
      ) {}
EPIX_API void SimulationCollisions<void>::sync(
    const epix::world::sand::components::Simulation& sim
) {
    for (auto pos : cached) {
        if (!sim.chunk_map().contains(pos.x, pos.y)) continue;
        auto& chunk = sim.chunk_map().get_chunk(pos.x, pos.y);
        if (!collisions.contains(pos.x, pos.y) || !chunk.should_update())
            continue;
        thread_pool->submit_task([this, &sim, &chunk, pos]() {
            auto& chunk_collision = collisions.get(pos.x, pos.y);
            chunk_collision.has_collision =
                get_chunk_collision(sim, chunk, chunk_collision.collisions);
        });
    }
    thread_pool->wait();
}
EPIX_API void SimulationCollisions<void>::clear_modified() { cached.clear(); }

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
EPIX_API void SimulationCollisionGeneral::cache(
    const epix::world::sand::components::Simulation& sim
) {
    SimulationCollisions::cache(sim);
}
EPIX_API void SimulationCollisionGeneral::sync(
    const epix::world::sand::components::Simulation& sim
) {
    SimulationCollisions::sync(sim);
}
EPIX_API void SimulationCollisionGeneral::sync(
    b2WorldId world, const PositionConverter& converter
) {
    for (auto&& pos : cached) {
        if (!collisions.contains(pos.x, pos.y)) continue;
        auto real_pos = glm::ivec2(
            converter.chunk_size * pos.x - converter.offset.x,
            converter.chunk_size * pos.y - converter.offset.y
        );
        auto&& chunk_collision = collisions.get(pos.x, pos.y);
        auto&& body_id         = chunk_collision.user_data.first;
        auto&& chain_ids       = chunk_collision.user_data.second;
        if (!chunk_collision.has_collision) {
            if (b2Body_IsValid(body_id)) {
                b2DestroyBody(body_id);
            }
            collisions.remove(pos.x, pos.y);
            continue;
        }
        if (b2Body_IsValid(body_id)) {
            for (auto chain_id : chain_ids) {
                b2DestroyChain(chain_id);
            }
            chain_ids.clear();
        } else {
            auto body_def = b2DefaultBodyDef();
            body_def.type = b2_staticBody;
            body_id       = b2CreateBody(world, &body_def);
        }
        for (auto&& polygon : chunk_collision.collisions) {
            auto&& outline = polygon[0];
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
            chain_def.isLoop = true;
            chain_def.count  = outline.size();
            chain_ids.push_back(b2CreateChain(body_id, &chain_def));
            delete[] points;
            for (size_t i = 1; i < polygon.size(); i++) {
                auto&& hole         = polygon[i];
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
                delete[] hole_points;
            }
        }
    }
    collisions.shrink();
    clear_modified();
}
// EPIX_API SimulationCollisionGeneral::~SimulationCollisionGeneral() {
//     for (auto&& [pos, user_data] : collisions.data()) {
//         auto& [body_id, chain_ids] = user_data.user_data;
//         if (b2Body_IsValid(body_id)) {
//             for (auto chain_id : chain_ids) {
//                 b2DestroyChain(chain_id);
//             }
//             b2DestroyBody(body_id);
//         }
//     }
// }
}  // namespace epix::world::sand_physics