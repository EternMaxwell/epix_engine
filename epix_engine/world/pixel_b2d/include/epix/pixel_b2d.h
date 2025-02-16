#pragma once

#include <box2d/box2d.h>

#include <earcut.hpp>
#include <glm/glm.hpp>

#include "epix/utils/grid.h"
#include "epix/world/sand.h"

namespace epix::world::px_phy2d {
struct PixPhyWorld {
    using Cell = epix::world::sand::components::Cell;

    b2WorldId _world;
    struct PixBodyCreateInfo {
        struct PixelDef {
            enum class Type { NAME, ID } type;
            union {
                std::string name;
                int id;
            };
            std::optional<glm::vec4> color;

            PixelDef(const std::string& name) : type(Type::NAME), name(name) {}
            PixelDef(int id) : type(Type::ID), id(id) {}

            PixelDef(const PixelDef& other) : type(other.type) {
                if (type == Type::NAME) {
                    new (&name) std::string(other.name);
                } else {
                    id = other.id;
                }
            }
            PixelDef(PixelDef&& other) : type(other.type) {
                if (type == Type::NAME) {
                    new (&name) std::string(std::move(other.name));
                } else {
                    id = other.id;
                }
            }
            PixelDef& operator=(const PixelDef& other) {
                if (type == Type::NAME) {
                    name.~basic_string();
                }
                type = other.type;
                if (type == Type::NAME) {
                    new (&name) std::string(other.name);
                } else {
                    id = other.id;
                }
                return *this;
            }
            PixelDef& operator=(PixelDef&& other) {
                if (type == Type::NAME) {
                    name.~basic_string();
                }
                type = other.type;
                if (type == Type::NAME) {
                    new (&name) std::string(std::move(other.name));
                } else {
                    id = other.id;
                }
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

            ~PixelDef() {
                if (type == Type::NAME) {
                    name.~basic_string();
                }
            }
        };

        glm::vec2 _pos;
        utils::grid::extendable_grid<PixelDef, 2> _grid;
        float _scale;
        const epix::world::sand::components::ElemRegistry* _reg;
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
        for (auto&& [r_pos, cell] : info._grid.view()) {
            int id;
            auto x = r_pos[0] - info._grid.origin(0);
            auto y = r_pos[1] - info._grid.origin(1);
            if (cell.type == PixBodyCreateInfo::PixelDef::Type::NAME) {
                id = info._reg->elem_id(cell.name);
            } else {
                id = cell.id;
            }
            cell_grid.emplace(
                x, y, id,
                cell.color ? *cell.color : info._reg->get_elem(id).gen_color()
            );
        }
        float density = 0.f, friction = 0.f, restitution = 0.f;
        {
            int count  = 0;
            auto&& reg = *info._reg;
            for (auto&& [pos, def] : info._grid.view()) {
                count++;
                if (def.type == PixBodyCreateInfo::PixelDef::Type::NAME) {
                    auto&& elem = reg.get_elem(def.name);
                    friction += elem.friction;
                    density += elem.density;
                    restitution += elem.bouncing;
                } else {
                    auto&& elem = reg.get_elem(def.id);
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