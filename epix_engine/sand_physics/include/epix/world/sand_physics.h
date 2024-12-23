#pragma once

#include <box2d/box2d.h>
#include <epix/physics2d.h>
#include <epix/world/sand.h>

#include <BS_thread_pool.hpp>
#include <glm/glm.hpp>

namespace epix::world::sand_physics {
EPIX_API bool get_chunk_collision(
    const epix::world::sand::components::Simulation& sim,
    const epix::world::sand::components::Simulation::Chunk& chunk,
    std::vector<std::vector<std::vector<glm::ivec2>>>& polygons
);
struct Ivec2Hash {
    std::size_t operator()(const glm::ivec2& vec) const {
        return std::hash<size_t>()(*(size_t*)&vec);
    }
};
template <typename T>
struct SimulationCollisions {
    struct ChunkCollisions {
        std::vector<std::vector<std::vector<glm::ivec2>>> collisions = {};
        bool has_collision;
        T user_data = {};
        operator bool() const { return has_collision; }
        bool operator!() const { return !has_collision; }
    };
    using user_data_type = T;
    using Grid =
        epix::utils::grid2d::ExtendableGrid2D<std::optional<ChunkCollisions>>;
    Grid collisions;
    spp::sparse_hash_set<glm::ivec2, Ivec2Hash> modified;
    std::unique_ptr<BS::thread_pool> thread_pool;

    SimulationCollisions() : thread_pool(std::make_unique<BS::thread_pool>()) {}
    void sync(const epix::world::sand::components::Simulation& sim) {
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
                collisions.get(pos.x, pos.y)->has_collision =
                    get_chunk_collision(
                        sim, chunk, collisions.get(pos.x, pos.y)->collisions
                    );
            });
        }
        thread_pool->wait();
    }
    void clear_modified() { modified.clear(); }
};
template <>
struct SimulationCollisions<void> {
    struct ChunkCollisions {
        std::vector<std::vector<std::vector<glm::ivec2>>> collisions = {};
        operator bool() const { return !collisions.empty(); }
        bool operator!() const { return collisions.empty(); }
    };
    using user_data_type = void;
    using Grid =
        epix::utils::grid2d::ExtendableGrid2D<std::optional<ChunkCollisions>>;
    Grid collisions;
    spp::sparse_hash_set<glm::ivec2, Ivec2Hash> modified;
    std::unique_ptr<BS::thread_pool> thread_pool;

    EPIX_API SimulationCollisions();
    EPIX_API void sync(const epix::world::sand::components::Simulation& sim);
};
struct SimulationCollisionGeneral
    : SimulationCollisions<std::pair<b2BodyId, std::vector<b2ChainId>>> {
    struct PositionConverter {
        int chunk_size;   // size of a chunk in cells
        float cell_size;  // size of a cell in box2d world
        glm::ivec2
            offset;  // offset of the box2d world origin in simulation world
    };
    /**
     * @brief create a position converter
     *
     * @param chunk_size size of a chunk in cells
     * @param cell_size size of a cell in box2d world
     * @param offset offset of the box2d world origin in simulation world
     * @return `PositionConverter` object
     */
    EPIX_API PositionConverter
    pos_converter(int chunk_size, float cell_size, const glm::ivec2& offset);
    /**
     * @brief create a position converter
     *
     * @param chunk_size size of a chunk in cells
     * @param cell_size size of a cell in box2d world
     * @param offset_x x offset of the box2d world origin in simulation world
     * @param offset_y y offset of the box2d world origin in simulation world
     * @return `PositionConverter` object
     */
    EPIX_API PositionConverter
    pos_converter(int chunk_size, float cell_size, int offset_x, int offset_y);
    EPIX_API SimulationCollisionGeneral();
    EPIX_API void sync(const epix::world::sand::components::Simulation& sim);
    EPIX_API void sync(b2WorldId world, const PositionConverter& converter);
};
}  // namespace epix::world::sand_physics
