#pragma once

#include <box2d/box2d.h>
#include <epix/world/sand.h>

#include <BS_thread_pool.hpp>
#include <entt/container/dense_set.hpp>
#include <glm/glm.hpp>

namespace epix::world::sync::sand2b2d {
EPIX_API bool get_chunk_collision(
    epix::world::sand::World sim,
    const epix::world::sand::Chunk& chunk,
    std::vector<std::vector<std::vector<glm::ivec2>>>& polygons
);
struct Ivec2Hash {
    std::size_t operator()(const glm::ivec2& vec) const {
        return std::hash<size_t>()(*(size_t*)&vec);
    }
};
struct Ivec2Equal {
    bool operator()(const glm::ivec2& lhs, const glm::ivec2& rhs) const {
        return lhs == rhs;
    }
};
template <typename T>
struct SimulationCollisions {
    struct ChunkCollisions {
        T user_data                                                  = {};
        bool has_collision                                           = false;
        std::vector<std::vector<std::vector<glm::ivec2>>> collisions = {};
        operator bool() const { return has_collision; }
        bool operator!() const { return !has_collision; }
    };
    using user_data_type = T;
    using Grid = epix::utils::grid::extendable_grid<ChunkCollisions, 2>;
    using thread_pool_t = BS::thread_pool<BS::tp::none>;
    Grid collisions;
    entt::dense_set<glm::ivec2, Ivec2Hash, Ivec2Equal> cached;
    std::unique_ptr<thread_pool_t> thread_pool;

    SimulationCollisions() : thread_pool(std::make_unique<thread_pool_t>()) {}
    template <typename... Args>
    void cache(const epix::world::sand::World sim, Args&&... args) {
        for (auto&& [pos, chunk] : sim->view()) {
            if (!chunk.should_update()) continue;
            cached.insert({pos[0], pos[1]});
            collisions.try_emplace(pos[0], pos[1], std::forward<Args>(args)...);
        }
        // for (auto [pos, chunk] : sim.chunk_map()) {
        //     if (!collisions.contains(pos.x, pos.y) || !chunk.should_update())
        //         continue;
        //     thread_pool->submit_task([this, &sim, &chunk, pos]() {
        //         collisions.get(pos.x, pos.y).has_collision =
        //             get_chunk_collision(
        //                 sim, chunk, collisions.get(pos.x, pos.y).collisions
        //             );
        //     });
        // }
        // thread_pool->wait();
    }
    void sync(const epix::world::sand::World sim) {
        for (auto pos : cached) {
            if (!sim->m_chunks.contains(pos.x, pos.y)) continue;
            auto& chunk = sim->m_chunks.get(pos.x, pos.y);
            if (!collisions.contains(pos.x, pos.y)) continue;
            thread_pool->detach_task([this, &sim, &chunk, pos]() {
                auto& chunk_collision = collisions.get(pos.x, pos.y);
                chunk_collision.has_collision =
                    get_chunk_collision(sim, chunk, chunk_collision.collisions);
            });
        }
        thread_pool->wait();
    }
    template <typename... Args>
    void cache_sync(const epix::world::sand::World sim, Args&&... args) {
        cache(sim, std::forward<Args>(args)...);
        sync(sim);
    }
    void clear_modified() { cached.clear(); }
};
template <>
struct SimulationCollisions<void> {
    struct ChunkCollisions {
        bool has_collision                                           = false;
        std::vector<std::vector<std::vector<glm::ivec2>>> collisions = {};
        operator bool() const { return !collisions.empty(); }
        bool operator!() const { return collisions.empty(); }
    };
    using user_data_type = void;
    using Grid = epix::utils::grid::extendable_grid<ChunkCollisions, 2>;
    using thread_pool_t = BS::thread_pool<BS::tp::none>;
    Grid collisions;
    entt::dense_set<glm::ivec2, Ivec2Hash> cached;
    std::unique_ptr<thread_pool_t> thread_pool;

    EPIX_API SimulationCollisions();
    template <typename... Args>
    void cache(const epix::world::sand::World sim, Args&&... args) {
        for (auto [pos, chunk] : sim.view()) {
            if (!chunk.should_update()) continue;
            cached.emplace(pos);
            collisions.try_emplace(pos.x, pos.y, std::forward<Args>(args)...);
        }
        // for (auto [pos, chunk] : sim.chunk_map()) {
        //     if (!collisions.contains(pos.x, pos.y)) continue;
        //     thread_pool->submit_task([this, &sim, &chunk, pos]() {
        //         auto& chunk_collision = collisions.get(pos.x, pos.y);
        //         chunk_collision.has_collision =
        //             get_chunk_collision(sim, chunk,
        //             chunk_collision.collisions);
        //     });
        // }
        // thread_pool->wait();
    }
    EPIX_API void sync(const epix::world::sand::World sim);
    template <typename... Args>
    void cache_sync(const epix::world::sand::World sim, Args&&... args) {
        cache(sim, std::forward<Args>(args)...);
        sync(sim);
    }
    EPIX_API void clear_modified();
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
    EPIX_API void cache(const epix::world::sand::World sim);

    EPIX_API void sync(const epix::world::sand::World sim);
    EPIX_API void sync(b2WorldId world, const PositionConverter& converter);
    // EPIX_API ~SimulationCollisionGeneral();

    template <typename Arg = int>
    void draw_debug_cache(
        const std::function<void(Arg, Arg, Arg, Arg)>& draw_line
    ) const {
        for (auto&& [pos, chunk] : collisions.view()) {
            if (!chunk.has_collision) continue;
            for (auto&& outlines : chunk.collisions) {
                for (auto&& outline : outlines) {
                    for (size_t i = 0; i < outline.size(); i++) {
                        draw_line(
                            outline[i].x, outline[i].y,
                            outline[(i + 1) % outline.size()].x,
                            outline[(i + 1) % outline.size()].y
                        );
                    }
                }
            }
        }
    }
    template <typename Arg = float>
    void draw_debug_b2d(const std::function<void(Arg, Arg, Arg, Arg)>& draw_line
    ) const {
        for (auto&& [pos, chunk] : collisions.view()) {
            if (!chunk.has_collision) continue;
            auto&& body = chunk.user_data.first;
            if (!b2Body_IsValid(body)) continue;
            auto shape_count = b2Body_GetShapeCount(body);
            auto shapes      = new b2ShapeId[shape_count];
            b2Body_GetShapes(body, shapes, shape_count);
            for (int i = 0; i < shape_count; i++) {
                auto shape = shapes[i];
                if (b2Shape_GetType(shape) == b2ShapeType::b2_polygonShape) {
                    auto polygon = b2Shape_GetPolygon(shape);
                    for (size_t i = 0; i < polygon.count; i++) {
                        auto& vertex1 = polygon.vertices[i];
                        auto& vertex2 =
                            polygon.vertices[(i + 1) % polygon.count];
                        draw_line(vertex1.x, vertex1.y, vertex2.x, vertex2.y);
                    }
                } else if (b2Shape_GetType(shape) ==
                           b2ShapeType::b2_smoothSegmentShape) {
                    auto segment  = b2Shape_GetSmoothSegment(shape);
                    auto& vertex1 = segment.segment.point1;
                    auto& vertex2 = segment.segment.point2;
                    draw_line(vertex1.x, vertex1.y, vertex2.x, vertex2.y);
                } else if (b2Shape_GetType(shape) ==
                           b2ShapeType::b2_segmentShape) {
                    auto segment  = b2Shape_GetSegment(shape);
                    auto& vertex1 = segment.point1;
                    auto& vertex2 = segment.point2;
                    draw_line(vertex1.x, vertex1.y, vertex2.x, vertex2.y);
                }
            }
        }
    }
};
}  // namespace epix::world::sync::sand2b2d
