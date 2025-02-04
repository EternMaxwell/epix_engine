#pragma once

#include <box2d/box2d.h>

#include <glm/glm.hpp>

#include "epix/utils/grid.h"
#include "physics2d/utils.h"

namespace epix::world::px_phy2d {
template <typename T>
struct PixPhyWorld {
    b2WorldId _world;
    struct PixBodyCreateInfo {
        glm::vec2 _pos;
        utils::grid::extendable_grid<T, 2> _grid;
    };
    struct PixBody {
        b2BodyId _body;
        utils::grid::sparse_grid<T, 2> _grid;
    };
    std::vector<PixBody> _bodies;
    std::stack<size_t> _free_bodies;
};
}  // namespace epix::world::px_phy2d