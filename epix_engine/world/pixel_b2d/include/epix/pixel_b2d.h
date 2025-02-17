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

        PixelDef& def(int x, int y) {
            if (!_grid.contains(x, y)) {
                _grid.emplace(x, y);
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