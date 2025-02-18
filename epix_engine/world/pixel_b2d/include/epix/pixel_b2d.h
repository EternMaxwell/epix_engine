#pragma once

#include <box2d/box2d.h>

#include <earcut.hpp>
#include <glm/glm.hpp>

#include "epix/utils/grid.h"
#include "epix/world/sand.h"

namespace epix::world::px_phy2d {
struct PixelDef {
    std::variant<std::string, int> type;
    std::optional<glm::vec4> color;

    PixelDef(const std::string& name) : type(name) {}
    PixelDef(int id) : type(id) {}

    PixelDef(const PixelDef& other) : type(other.type), color(other.color) {}
    PixelDef(PixelDef&& other)
        : type(std::move(other.type)), color(std::move(other.color)) {}
    PixelDef& operator=(const PixelDef& other) {
        type  = other.type;
        color = other.color;
        return *this;
    }
    PixelDef& operator=(PixelDef&& other) {
        type  = std::move(other.type);
        color = std::move(other.color);
        return *this;
    }

    PixelDef& set_color(const glm::vec4& color) {
        this->color = color;
        return *this;
    }
    PixelDef& set_color(float r, float g, float b, float a) {
        this->color = glm::vec4{r, g, b, a};
        return *this;
    }
};

struct PixPhyWorld {
    using Cell = epix::world::sand::components::Cell;

    b2WorldId _world;
    struct PixBodyCreateInfo {
       private:
        glm::vec2 _pos;
        utils::grid::extendable_grid<PixelDef, 2> _grid;
        float _scale;
        const epix::world::sand::components::ElemRegistry* _reg;

       public:
        PixBodyCreateInfo& set_pos(const glm::vec2& pos) {
            _pos = pos;
            return *this;
        }
        PixBodyCreateInfo& set_grid(
            utils::grid::extendable_grid<PixelDef, 2>&& grid
        ) {
            _grid = std::move(grid);
            return *this;
        }
        PixBodyCreateInfo& set_scale(float scale) {
            _scale = scale;
            return *this;
        }
        PixBodyCreateInfo& set_reg(
            const epix::world::sand::components::ElemRegistry& reg
        ) {
            _reg = &reg;
            return *this;
        }

        PixelDef& def(int x, int y, const std::string& name) {
            if (!_grid.contains(x, y)) {
                _grid.emplace(x, y, name);
            }
            return _grid.get(x, y);
        }

        PixelDef& def(int x, int y, int id) {
            if (!_grid.contains(x, y)) {
                _grid.emplace(x, y, id);
            }
            return _grid.get(x, y);
        }

        utils::grid::extendable_grid<PixelDef, 2>& grid() { return _grid; }

        friend struct PixPhyWorld;
    };
    struct PixBody {
        b2BodyId _body;
        float _scale;
        utils::grid::sparse_grid<Cell, 2> _grid;

        PixBody(
            const b2BodyId& body,
            float scale,
            const utils::grid::sparse_grid<Cell, 2>& grid
        )
            : _body(body), _scale(scale), _grid(grid) {}

        PixBody(
            const b2BodyId& body,
            float scale,
            utils::grid::sparse_grid<Cell, 2>&& grid
        )
            : _body(body), _scale(scale), _grid(std::move(grid)) {}

        void draw_pixels(
            std::function<void(const glm::mat4&)>& model,
            std::function<void(const glm::vec2&, const glm::vec4&)>& each_pixel
        ) {
            auto view = _grid.view();
            for (auto&& [pos, cell] : view) {
                each_pixel({pos[0], pos[1]}, cell.color);
            }
        }
    };
    std::vector<PixBody*> _bodies;
    std::stack<size_t> _free_bodies;

    PixPhyWorld() {}
    void create() {
        auto world_def    = b2DefaultWorldDef();
        world_def.gravity = {0.0f, -9.8f};
        _world            = b2CreateWorld(&world_def);
    }

    b2WorldId get_world() { return _world; }

    size_t create_body(const PixBodyCreateInfo& info) {
        size_t index;
        if (_free_bodies.empty()) {
            index = _bodies.size();
            _bodies.push_back({});
        } else {
            index = _free_bodies.top();
            _free_bodies.pop();
        }
        auto body_def     = b2DefaultBodyDef();
        body_def.type     = b2BodyType::b2_dynamicBody;
        body_def.position = {info._pos.x, info._pos.y};
        body_def.position.x += info._grid.origin(0) * info._scale;
        body_def.position.y += info._grid.origin(1) * info._scale;
        auto body = b2CreateBody(_world, &body_def);
        utils::grid::sparse_grid<Cell, 2> cell_grid(info._grid.size());
        for (auto&& [r_pos, def] : info._grid.view()) {
            int id;
            auto x = r_pos[0] - info._grid.origin(0);
            auto y = r_pos[1] - info._grid.origin(1);
            if (std::holds_alternative<std::string>(def.type)) {
                id = info._reg->elem_id(std::get<std::string>(def.type));
            } else {
                id = std::get<int>(def.type);
            }
            cell_grid.emplace(
                x, y, id,
                def.color ? *def.color : info._reg->get_elem(id).gen_color()
            );
        }
        float density = 0.f, friction = 0.f, restitution = 0.f;
        {
            int count  = 0;
            auto&& reg = *info._reg;
            for (auto&& [pos, def] : info._grid.view()) {
                count++;
                if (std::holds_alternative<std::string>(def.type)) {
                    auto&& elem = reg.get_elem(std::get<std::string>(def.type));
                    friction += elem.friction;
                    density += elem.density;
                    restitution += elem.bouncing;
                } else {
                    auto&& elem = reg.get_elem(std::get<int>(def.type));
                    friction += elem.friction;
                    density += elem.density;
                    restitution += elem.bouncing;
                }
            }
            if (!count) return index;
            friction /= count;
            density /= count;
            restitution /= count;
        }
        auto outlines         = utils::grid::get_polygon_simplified(cell_grid);
        auto triangle_indices = mapbox::earcut(outlines);
        auto vertex_at        = [&](size_t i) {
            for (size_t x = 0; x < outlines.size(); x++) {
                if (i < outlines[x].size()) {
                    return outlines[x][i];
                } else {
                    i -= outlines[x].size();
                }
            }
        };
        for (size_t i = 0; i < triangle_indices.size(); i += 3) {
            b2Vec2 vertices[3];
            for (size_t j = 0; j < 3; j++) {
                auto&& vertex = vertex_at(triangle_indices[i + j]);
                vertices[j] = {vertex.x * info._scale, vertex.y * info._scale};
            }
            b2Hull hull           = b2ComputeHull(vertices, 3);
            float radius          = 0.0f;
            b2Polygon triangle    = b2MakePolygon(&hull, radius);
            b2ShapeDef shape_def  = b2DefaultShapeDef();
            shape_def.density     = density;
            shape_def.friction    = friction;
            shape_def.restitution = restitution;
            b2CreatePolygonShape(body, &shape_def, &triangle);
        }
        _bodies[index] = new PixBody(body, info._scale, std::move(cell_grid));
        return index;
    }

    template <typename Arg = float>
    void draw_debug_shape(
        const std::function<void(Arg, Arg, Arg, Arg, bool)>& draw_line_shape
    ) {
        // draw the collision shapes.
        for (auto&& body : _bodies) {
            if (body) {
                auto id = body->_body;
                if (!b2Body_IsValid(id)) continue;
                auto pos  = b2Body_GetPosition(id);
                auto vel  = b2Body_GetLinearVelocity(id);
                auto rot  = b2Body_GetRotation(id);  // a cosine / sine pair
                auto sinv = rot.s, cosv = rot.c;
                auto shape_count = b2Body_GetShapeCount(id);
                bool awake       = b2Body_IsAwake(id);
                std::vector<b2ShapeId> shapes(shape_count);
                b2Body_GetShapes(id, shapes.data(), shape_count);
                for (auto&& shape : shapes) {
                    if (b2Shape_GetType(shape) ==
                        b2ShapeType::b2_polygonShape) {
                        auto polygon = b2Shape_GetPolygon(shape);
                        for (size_t i = 0; i < polygon.count; i++) {
                            auto& vertex1 = polygon.vertices[i];
                            auto& vertex2 =
                                polygon.vertices[(i + 1) % polygon.count];
                            auto x1 =
                                vertex1.x * cosv - vertex1.y * sinv + pos.x;
                            auto y1 =
                                vertex1.x * sinv + vertex1.y * cosv + pos.y;
                            auto x2 =
                                vertex2.x * cosv - vertex2.y * sinv + pos.x;
                            auto y2 =
                                vertex2.x * sinv + vertex2.y * cosv + pos.y;
                            draw_line_shape(x1, y1, x2, y2, awake);
                        }
                    } else if (b2Shape_GetType(shape) ==
                               b2ShapeType::b2_smoothSegmentShape) {
                        auto segment  = b2Shape_GetSmoothSegment(shape);
                        auto& vertex1 = segment.segment.point1;
                        auto& vertex2 = segment.segment.point2;
                        auto x1 = vertex1.x * cosv - vertex1.y * sinv + pos.x;
                        auto y1 = vertex1.x * sinv + vertex1.y * cosv + pos.y;
                        auto x2 = vertex2.x * cosv - vertex2.y * sinv + pos.x;
                        auto y2 = vertex2.x * sinv + vertex2.y * cosv + pos.y;
                        draw_line_shape(x1, y1, x2, y2, awake);
                    } else if (b2Shape_GetType(shape) ==
                               b2ShapeType::b2_segmentShape) {
                        auto segment  = b2Shape_GetSegment(shape);
                        auto& vertex1 = segment.point1;
                        auto& vertex2 = segment.point2;
                        auto x1 = vertex1.x * cosv - vertex1.y * sinv + pos.x;
                        auto y1 = vertex1.x * sinv + vertex1.y * cosv + pos.y;
                        auto x2 = vertex2.x * cosv - vertex2.y * sinv + pos.x;
                        auto y2 = vertex2.x * sinv + vertex2.y * cosv + pos.y;
                        draw_line_shape(x1, y1, x2, y2, awake);
                    }
                }
            }
        }
    }

    template <typename Arg = float>
    void draw_debug_vel(const std::function<void(Arg, Arg, Arg, Arg)>& draw_func
    ) {
        for (auto&& body : _bodies) {
            if (body) {
                auto id = body->_body;
                if (!b2Body_IsValid(id)) continue;
                if (!b2Body_IsAwake(id)) continue;
                auto pos = b2Body_GetPosition(id);
                auto msc = b2Body_GetMassCenter(id);
                auto vel = b2Body_GetLinearVelocity(id);
                auto x1  = pos.x + msc.x;
                auto y1  = pos.y + msc.y;
                draw_line_vel(x1, y1, vel.x, vel.y);
            }
        }
    }

    void draw_pixel_smooth(
        const std::function<void(const glm::mat4&, bool)>& each_body,
        const std::function<void(const glm::vec2&, const glm::vec4&)>&
            each_pixel
    ) {
        for (auto&& body : _bodies) {
            if (body) {
                if (!b2Body_IsValid(body->_body)) continue;
                auto awake = b2Body_IsAwake(body->_body);
                auto pos   = b2Body_GetPosition(body->_body);
                auto rot   = b2Body_GetRotation(body->_body);
                auto angle = std::atan2(rot.s, rot.c);
                glm::mat4 model =
                    glm::translate(glm::mat4(1.0f), {pos.x, pos.y, 0.0f});
                model = glm::rotate(model, angle, {0.0f, 0.0f, 1.0f});
                each_body(model, awake);
                for (auto&& [pos, cell] : body->_grid.view()) {
                    each_pixel({pos[0], pos[1]}, cell.color);
                }
            }
        }
    }

    void draw_pixel_rasterized(
        const glm::vec2&
            anchor,        // the corner of any pixel in the render target
        float pixel_size,  // pixel size in world space
        const std::function<void(const glm::vec2&)>
            each_body,  // left bottom of the draw grid aabb
        const std::function<void(const glm::vec2&, const glm::vec4&)>&
            each_pixel  // x, y related to anchor
    ) {
        for (auto&& body : _bodies) {
            if (body) {
                if (!b2Body_IsValid(body->_body)) continue;
                auto pos = b2Body_GetPosition(body->_body);
                each_body({pos.x, pos.y});
                auto rot  = b2Body_GetRotation(body->_body);
                auto sinv = rot.s, cosv = rot.c;
                // get the aabb of the grid in body
                auto&& grid_size = body->_grid.size();
                glm::vec2 c1     = {0, 0};
                glm::vec2 c2     = {grid_size[0] * cosv, grid_size[0] * sinv};
                glm::vec2 c3     = {
                    grid_size[0] * cosv + grid_size[1] * -sinv,
                    grid_size[0] * sinv + grid_size[1] * cosv
                };
                glm::vec2 c4 = {grid_size[1] * -sinv, grid_size[1] * cosv};
                float minx   = std::min({c1.x, c2.x, c3.x, c4.x});
                float miny   = std::min({c1.y, c2.y, c3.y, c4.y});
                float maxx   = std::max({c1.x, c2.x, c3.x, c4.x});
                float maxy   = std::max({c1.y, c2.y, c3.y, c4.y});
                // extend the aabb to the make the coords be anchor.x(y) + n *
                // pixel_size
                minx = std::floor((minx) / pixel_size) * pixel_size;
                miny = std::floor((miny) / pixel_size) * pixel_size;
                maxx = std::ceil((maxx) / pixel_size) * pixel_size;
                maxy = std::ceil((maxy) / pixel_size) * pixel_size;
                for (float x = minx; x < maxx; x += pixel_size) {
                    for (float y = miny; y < maxy; y += pixel_size) {
                        // get the x, y in grid space
                        auto x_     = x + pixel_size / 2;
                        auto y_     = y + pixel_size / 2;
                        auto xg     = x_ * cosv + y_ * sinv;
                        auto yg     = y_ * cosv - x_ * sinv;
                        int floor_x = std::floor(xg);
                        int floor_y = std::floor(yg);
                        int ceil_x  = std::ceil(xg);
                        int ceil_y  = std::ceil(yg);
                        float lerpx = xg - floor_x;
                        float lerpy = yg - floor_y;
                        std::array<glm::ivec2, 4> checks = {
                            glm::ivec2{floor_x, floor_y},
                            glm::ivec2{ceil_x, floor_y},
                            glm::ivec2{ceil_x, ceil_y},
                            glm::ivec2{floor_x, ceil_y}
                        };
                        uint16_t count = 0;
                        for (auto&& check : checks) {
                            if (!body->_grid.valid(check.x, check.y)) continue;
                            if (!body->_grid.contains(check.x, check.y))
                                continue;
                            count++;
                        }
                        if (count != 4) continue;
                        auto&& color1 = body->_grid.get(floor_x, floor_y).color;
                        auto&& color2 = body->_grid.get(ceil_x, floor_y).color;
                        auto&& color3 = body->_grid.get(ceil_x, ceil_y).color;
                        auto&& color4 = body->_grid.get(floor_x, ceil_y).color;
                        glm::vec4 color = {
                            std::lerp(
                                std::lerp(color1.r, color2.r, lerpx),
                                std::lerp(color4.r, color3.r, lerpx), lerpy
                            ),
                            std::lerp(
                                std::lerp(color1.g, color2.g, lerpx),
                                std::lerp(color4.g, color3.g, lerpx), lerpy
                            ),
                            std::lerp(
                                std::lerp(color1.b, color2.b, lerpx),
                                std::lerp(color4.b, color3.b, lerpx), lerpy
                            ),
                            std::lerp(
                                std::lerp(color1.a, color2.a, lerpx),
                                std::lerp(color4.a, color3.a, lerpx), lerpy
                            )
                        };
                        each_pixel({x, y}, color);
                    }
                }
            }
        }
    }

    void destroy_body(size_t index) {
        if (_bodies[index]) {
            delete _bodies[index];
            _bodies[index] = nullptr;
        }
    }

    void destroy() {
        for (auto&& body : _bodies) {
            if (body) delete body;
        }
    }
};
}  // namespace epix::world::px_phy2d