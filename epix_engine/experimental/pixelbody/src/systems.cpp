// Implementation of all PixelBodyPlugin systems and the plugin itself.
// See modules/systems.cppm for declarations.

module;

#include <box2d/box2d.h>
#include <box2d/collision.h>
#include <box2d/id.h>
#include <box2d/types.h>

#include <mapbox/earcut.hpp>

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>
#endif

module epix.experimental.pixelbody;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;
import epix.assets;
import epix.mesh;
import epix.transform;
import epix.render;
import epix.time;
import epix.extension.grid;
import epix.extension.fallingsand;
import glm;
import webgpu;

namespace epix::experimental::pixelbody {

using namespace epix::core;
namespace fs   = epix::ext::fallingsand;
namespace grid = epix::ext::grid;

namespace {

// Earcut adaptor: tell mapbox how to read glm::ivec2.
}  // anonymous namespace

}  // namespace epix::experimental::pixelbody

// Earcut adaptor must live in mapbox::util namespace.
namespace mapbox::util {
template <>
struct nth<0, glm::ivec2> {
    static int get(const glm::ivec2& p) { return p.x; }
};
template <>
struct nth<1, glm::ivec2> {
    static int get(const glm::ivec2& p) { return p.y; }
};
}  // namespace mapbox::util

namespace epix::experimental::pixelbody {

// ─────────────────────────────────────────────────────────────────────────────
// PixelBody factories
// ─────────────────────────────────────────────────────────────────────────────

PixelBody PixelBody::make_solid(
    std::uint32_t w, std::uint32_t h, std::size_t base_id, const fs::ElementRegistry& registry, std::uint64_t seed) {
    PixelBody body;
    body.cells          = grid::dense_grid<2, fs::Element>(std::array<std::uint32_t, 2>{w, h});
    auto color_func_opt = registry.get(base_id);
    auto get_color      = [&](std::uint64_t s) -> glm::vec4 {
        if (!color_func_opt) return glm::vec4(1, 1, 1, 1);
        const auto& base = color_func_opt->get();
        if (!base.color_func) return glm::vec4(1, 1, 1, 1);
        return base.color_func(s);
    };
    std::uint64_t i = 0;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x, ++i) {
            fs::Element e;
            e.base_id = base_id;
            e.color   = get_color(seed + i);
            (void)body.cells.set({x, y}, e);
        }
    }
    body.shapes_dirty  = true;
    body.mesh_dirty    = true;
    body.physics_dirty = true;
    return body;
}

PixelBody PixelBody::from_grid(grid::dense_grid<2, fs::Element>&& cells) {
    PixelBody body;
    body.cells         = std::move(cells);
    body.shapes_dirty  = true;
    body.mesh_dirty    = true;
    body.physics_dirty = true;
    return body;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

inline b2Vec2 to_b2(glm::vec2 v) { return b2Vec2{v.x, v.y}; }
inline glm::vec2 from_b2(b2Vec2 v) { return glm::vec2(v.x, v.y); }

/// Adapter so dense_grid<2, fs::Element> satisfies grid::DataGrid (and
/// grid::BoolGrid via `contains(x, y)`).
struct DenseElementGridView {
    const grid::dense_grid<2, fs::Element>& g;

    std::array<std::int32_t, 2> size() const {
        auto d = g.dimensions();
        return {static_cast<std::int32_t>(d[0]), static_cast<std::int32_t>(d[1])};
    }
    bool contains(std::int32_t x, std::int32_t y) const {
        if (x < 0 || y < 0) return false;
        auto d = g.dimensions();
        if (static_cast<std::uint32_t>(x) >= d[0] || static_cast<std::uint32_t>(y) >= d[1]) return false;
        return g.contains({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)});
    }
    const fs::Element& get(std::int32_t x, std::int32_t y) const {
        return g.get({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)})->get();
    }
};

/// Adapter so a Chunk<2> satisfies grid::DataGrid for the Element layer.
struct ChunkElementGridView {
    const grid::Chunk<fs::kDim>& chunk;

    std::array<std::int32_t, 2> size() const {
        auto w = static_cast<std::int32_t>(chunk.width());
        return {w, w};
    }
    bool contains(std::int32_t x, std::int32_t y) const {
        if (x < 0 || y < 0) return false;
        auto w = static_cast<std::int32_t>(chunk.width());
        if (x >= w || y >= w) return false;
        auto r = static_cast<const grid::ChunkLayer<fs::kDim>&>(chunk).template get<fs::Element>(
            {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)});
        return r.has_value();
    }
    const fs::Element& get(std::int32_t x, std::int32_t y) const {
        return static_cast<const grid::ChunkLayer<fs::kDim>&>(chunk)
            .template get<fs::Element>({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)})
            ->get();
    }
};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Systems (stub bodies — populated incrementally)
// ─────────────────────────────────────────────────────────────────────────────

void init_pixel_body_worlds(Query<Item<Mut<PixelBodyWorld>>> worlds) {
    for (auto&& [w_mut] : worlds.iter()) {
        auto& w = w_mut.get_mut();
        if (B2_IS_NON_NULL(w.b2_world)) continue;
        b2WorldDef def = b2DefaultWorldDef();
        def.gravity    = to_b2(w.b2_gravity());
        w.b2_world     = b2CreateWorld(&def);
    }
}

void init_pixel_bodies(Query<Item<Entity, Mut<PixelBody>, const transform::Transform&, const Parent&>> bodies,
                       Query<Item<Mut<PixelBodyWorld>>> worlds,
                       Res<fs::ElementRegistry> registry) {
    for (auto&& [e, body_mut, tf, parent] : bodies.iter()) {
        auto& body = body_mut.get_mut();
        if (B2_IS_NON_NULL(body.b2_body)) continue;
        // Look up parent world.
        auto world_opt = worlds.get(parent.entity());
        if (!world_opt.has_value()) continue;
        auto&& [w_mut] = *world_opt;
        auto& w        = w_mut.get_mut();
        if (B2_IS_NULL(w.b2_world)) continue;

        b2BodyDef bd       = b2DefaultBodyDef();
        bd.type            = body.is_dynamic ? b2_dynamicBody : b2_kinematicBody;
        bd.position        = b2Vec2{tf.translation.x, tf.translation.y};
        bd.rotation        = b2MakeRot(0.0f);
        body.b2_body       = b2CreateBody(w.b2_world, &bd);
        body.shapes_dirty  = true;
        body.physics_dirty = true;
    }
    (void)registry;
}

void rebuild_pixel_body_shapes(Query<Item<Mut<PixelBody>, const Parent&>> bodies,
                               Query<Item<const PixelBodyWorld&>> worlds,
                               Res<fs::ElementRegistry> registry) {
    (void)worlds;
    for (auto&& [body_mut, parent] : bodies.iter()) {
        auto& body = body_mut.get_mut();
        if (B2_IS_NULL(body.b2_body)) continue;
        if (!body.shapes_dirty) continue;

        // Destroy old shapes.
        for (auto& sid : body.shapes) {
            if (B2_IS_NON_NULL(sid)) b2DestroyShape(sid, false);
        }
        body.shapes.clear();

        // Build a shape def using averaged element properties from the cells.
        b2ShapeDef sd           = b2DefaultShapeDef();
        sd.density              = 1.0f;
        sd.material.friction    = 0.3f;
        sd.material.restitution = 0.05f;
        sd.updateBodyMass       = false;
        {
            float dens_sum = 0.0f, fric_sum = 0.0f, rest_sum = 0.0f;
            std::size_t n = 0;
            for (auto&& [pos, e] : body.cells.iter()) {
                auto base_opt = (*registry).get(e.base_id);
                if (!base_opt.has_value()) continue;
                const auto& b = base_opt->get();
                dens_sum += b.density;
                fric_sum += b.friction;
                rest_sum += b.restitution;
                ++n;
            }
            if (n > 0) {
                sd.density              = dens_sum / static_cast<float>(n);
                sd.material.friction    = fric_sum / static_cast<float>(n);
                sd.material.restitution = rest_sum / static_cast<float>(n);
            }
        }

        // Extract polygons from the cell grid.
        DenseElementGridView view{body.cells};
        auto polys = grid::get_polygons_simplified_multi(view, 0.5f);
        for (const auto& poly : polys) {
            if (poly.outer.points.size() < 3) continue;

            // Flatten outer + holes for earcut.
            std::vector<std::vector<glm::ivec2>> rings;
            rings.reserve(1 + poly.holes.size());
            rings.push_back(poly.outer.points);
            for (const auto& h : poly.holes) rings.push_back(h.points);
            std::vector<glm::ivec2> verts;
            for (const auto& r : rings) verts.insert(verts.end(), r.begin(), r.end());
            auto idx = mapbox::earcut<std::uint32_t>(rings);

            for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
                std::array<b2Vec2, 3> pts = {
                    b2Vec2{static_cast<float>(verts[idx[t]].x), static_cast<float>(verts[idx[t]].y)},
                    b2Vec2{static_cast<float>(verts[idx[t + 1]].x), static_cast<float>(verts[idx[t + 1]].y)},
                    b2Vec2{static_cast<float>(verts[idx[t + 2]].x), static_cast<float>(verts[idx[t + 2]].y)},
                };
                b2Hull hull = b2ComputeHull(pts.data(), 3);
                if (hull.count < 3) continue;
                b2Polygon p   = b2MakePolygon(&hull, 0.0f);
                b2ShapeId sid = b2CreatePolygonShape(body.b2_body, &sd, &p);
                if (B2_IS_NON_NULL(sid)) body.shapes.push_back(sid);
            }
        }

        b2Body_ApplyMassFromShapes(body.b2_body);
        body.shapes_dirty  = false;
        body.physics_dirty = false;
    }
}

void sync_transforms_to_b2(Query<Item<const PixelBody&, const transform::Transform&, const Velocity&>> bodies) {
    for (auto&& [body, tf, vel] : bodies.iter()) {
        if (B2_IS_NULL(body.b2_body)) continue;
        // Extract z-axis rotation from quaternion.
        float angle = std::atan2(2.0f * (tf.rotation.w * tf.rotation.z + tf.rotation.x * tf.rotation.y),
                                 1.0f - 2.0f * (tf.rotation.y * tf.rotation.y + tf.rotation.z * tf.rotation.z));
        b2Body_SetTransform(body.b2_body, b2Vec2{tf.translation.x, tf.translation.y}, b2MakeRot(angle));
        b2Body_SetLinearVelocity(body.b2_body, to_b2(vel.linear));
        b2Body_SetAngularVelocity(body.b2_body, vel.angular);
    }
}

void update_sand_static_bodies(
    Commands cmd,
    Res<fs::ElementRegistry> registry,
    Query<Item<Entity, const PixelBodyWorld&, const fs::SandWorld&, Opt<const Children&>>> worlds,
    Query<Item<Entity,
               const grid::Chunk<fs::kDim>&,
               const fs::SandChunkPos&,
               Opt<Mut<fs::SandChunkDirtyRect>>,
               Opt<Mut<SandStaticBody>>>> chunks) {
    for (auto&& [we, w, sand_world, maybe_children] : worlds.iter()) {
        (void)we;
        if (B2_IS_NULL(w.b2_world)) continue;
        if (!maybe_children.has_value()) continue;
        b2WorldId b2_world_id = w.b2_world;
        float cell_size       = sand_world.cell_size();
        std::int32_t chunk_w  = static_cast<std::int32_t>(std::size_t(1) << sand_world.chunk_shift());

        for (Entity child_ent : maybe_children->get().entities()) {
            auto chunk_opt = chunks.get(child_ent);
            if (!chunk_opt.has_value()) continue;
            auto&& [c_ent, chunk, cpos, dr_opt, ssb_opt] = *chunk_opt;

            // Decide rebuild trigger.
            bool need_rebuild = false;
            if (!ssb_opt.has_value()) {
                need_rebuild = true;
            } else {
                auto& ssb = ssb_opt->get_mut();
                if (ssb.dirty) need_rebuild = true;
                if (dr_opt.has_value() && dr_opt->get_mut().active()) need_rebuild = true;
            }
            if (!need_rebuild) continue;

            // Tear down any existing static body + chains.
            if (ssb_opt.has_value()) {
                auto& ssb = ssb_opt->get_mut();
                for (auto cid : ssb.chains) {
                    if (B2_IS_NON_NULL(cid)) b2DestroyChain(cid);
                }
                ssb.chains.clear();
                if (B2_IS_NON_NULL(ssb.b2_body)) {
                    b2DestroyBody(ssb.b2_body);
                    ssb.b2_body = b2_nullBodyId;
                }
            }

            // Extract solid polygons in chunk-local pixel space.
            ChunkElementGridView view{chunk};
            auto polys = grid::get_polygons_simplified_multi(
                view,
                [&](const ChunkElementGridView& g, std::int32_t x, std::int32_t y) {
                    if (!g.contains(x, y)) return false;
                    const auto& cell = g.get(x, y);
                    auto base        = (*registry).get(cell.base_id);
                    return base.has_value() && base->get().type == fs::ElementType::Solid;
                },
                0.5f);

            if (polys.empty()) {
                if (ssb_opt.has_value()) ssb_opt->get_mut().dirty = false;
                continue;
            }

            // Create static body at the chunk's world position.
            b2BodyDef bd = b2DefaultBodyDef();
            bd.type      = b2_staticBody;
            bd.position  = b2Vec2{
                static_cast<float>(cpos.value[0] * chunk_w) * cell_size,
                static_cast<float>(cpos.value[1] * chunk_w) * cell_size,
            };
            b2BodyId static_body = b2CreateBody(b2_world_id, &bd);

            std::vector<b2ChainId> chains;
            auto add_chain = [&](const std::vector<glm::ivec2>& pts_int) {
                if (pts_int.size() < 4) return;
                std::vector<b2Vec2> pts;
                pts.reserve(pts_int.size());
                // Engine convention: Y is up. The polygon module emits outer
                // rings CW with the solid on the right of each edge. b2 v3
                // chain shapes need the solid on the LEFT (CCW outer loop) so
                // edge normals point outward. Reverse the point order.
                for (auto it = pts_int.rbegin(); it != pts_int.rend(); ++it) {
                    pts.push_back({static_cast<float>(it->x) * cell_size, static_cast<float>(it->y) * cell_size});
                }
                b2ChainDef cd = b2DefaultChainDef();
                cd.isLoop     = true;
                cd.points     = pts.data();
                cd.count      = static_cast<int>(pts.size());
                b2ChainId cid = b2CreateChain(static_body, &cd);
                if (B2_IS_NON_NULL(cid)) chains.push_back(cid);
            };
            for (const auto& poly : polys) {
                add_chain(poly.outer.points);
                for (const auto& h : poly.holes) add_chain(h.points);
            }

            if (ssb_opt.has_value()) {
                auto& ssb   = ssb_opt->get_mut();
                ssb.b2_body = static_body;
                ssb.chains  = std::move(chains);
                ssb.dirty   = false;
            } else {
                cmd.entity(c_ent).insert(SandStaticBody{
                    .b2_body = static_body,
                    .chains  = std::move(chains),
                    .dirty   = false,
                });
            }
        }
    }
}

void step_pixel_body_worlds(Res<time::Time<>> time, Query<Item<Mut<PixelBodyWorld>>> worlds) {
    float dt = time->delta_secs();
    for (auto&& [w_mut] : worlds.iter()) {
        auto& w = w_mut.get_mut();
        if (B2_IS_NULL(w.b2_world)) continue;
        if (w.paused) continue;
        w.accumulator += dt;
        while (w.accumulator >= w.fixed_dt) {
            b2World_Step(w.b2_world, w.fixed_dt, w.sub_steps);
            w.accumulator -= w.fixed_dt;
        }
    }
}

void sync_b2_to_transforms(Query<Item<const PixelBody&, Mut<transform::Transform>, Mut<Velocity>>> bodies) {
    for (auto&& [body, tf_mut, vel_mut] : bodies.iter()) {
        if (B2_IS_NULL(body.b2_body)) continue;
        b2Vec2 pos       = b2Body_GetPosition(body.b2_body);
        b2Rot rot        = b2Body_GetRotation(body.b2_body);
        float ang        = b2Rot_GetAngle(rot);
        auto& tf         = tf_mut.get_mut();
        tf.translation.x = pos.x;
        tf.translation.y = pos.y;
        // z-axis rotation only.
        tf.rotation = glm::quat(std::cos(ang * 0.5f), 0.0f, 0.0f, std::sin(ang * 0.5f));
        auto& vel   = vel_mut.get_mut();
        vel.linear  = from_b2(b2Body_GetLinearVelocity(body.b2_body));
        vel.angular = b2Body_GetAngularVelocity(body.b2_body);
    }
}

void interact_pixel_body_with_sand(
    Res<fs::ElementRegistry> registry,
    Query<Item<Entity, const PixelBodyWorld&, Mut<fs::SandWorld>, Opt<const Children&>>> worlds,
    Query<Item<const PixelBody&, const transform::Transform&>> bodies,
    Query<Item<Mut<grid::Chunk<fs::kDim>>, const fs::SandChunkPos&, Mut<fs::SandChunkDirtyRect>>> chunks) {
    constexpr int kMaxPush = 64;
    for (auto&& [world_ent, w, sand_world_mut, world_children_opt] : worlds.iter()) {
        (void)world_ent;
        if (B2_IS_NULL(w.b2_world)) continue;
        if (!world_children_opt.has_value()) continue;
        auto& sw  = sand_world_mut.get_mut();
        float scs = sw.cell_size();
        if (scs <= 0.0f) continue;

        // Collect sand chunks (chunks are children of the same entity).
        auto chunk_range =
            world_children_opt->get().entities() |
            std::views::filter([&](Entity e) { return chunks.get(e).has_value(); }) |
            std::views::transform(
                [&](Entity e) -> std::tuple<grid::Chunk<fs::kDim>&, const fs::SandChunkPos&, fs::SandChunkDirtyRect&> {
                    auto opt         = chunks.get(e);
                    auto&& [c, p, d] = *opt;
                    return {c.get_mut(), p, d.get_mut()};
                });
        auto sim_res = fs::SandSimulation::create(sw, registry.get(), chunk_range);
        if (!sim_res.has_value()) continue;
        auto& sim = *sim_res;

        // Anti-gravity direction (Y axis).
        std::int64_t dir_y = (w.gravity_cells.y < 0.0f) ? 1 : -1;

        // Iterate body children of this world.
        for (Entity body_ent : world_children_opt->get().entities()) {
            auto body_opt = bodies.get(body_ent);
            if (!body_opt.has_value()) continue;
            auto&& [body, body_tf] = *body_opt;

            for (auto&& [pos, e] : body.cells.iter()) {
                glm::vec3 local(static_cast<float>(pos[0]) + 0.5f, static_cast<float>(pos[1]) + 0.5f, 0.0f);
                glm::vec3 wpos = body_tf.translation + body_tf.rotation * local;

                std::int64_t sx = static_cast<std::int64_t>(std::floor(wpos.x / scs));
                std::int64_t sy = static_cast<std::int64_t>(std::floor(wpos.y / scs));

                auto src = sim.get_cell<fs::Element>({sx, sy});
                if (!src.has_value()) continue;
                fs::Element to_move = src->get();

                auto base_opt = (*registry).get(to_move.base_id);
                if (base_opt.has_value() && base_opt->get().type == fs::ElementType::Solid) {
                    continue;
                }

                for (int k = 1; k <= kMaxPush; ++k) {
                    std::int64_t ty = sy + dir_y * static_cast<std::int64_t>(k);
                    auto target     = sim.get_cell<fs::Element>({sx, ty});
                    if (target.has_value()) continue;
                    if (sim.put_cell({sx, ty}, to_move)) {
                        sim.erase_cell({sx, sy});
                    }
                    break;
                }
                (void)e;
            }
        }
    }
}

void build_pixel_body_meshes(Commands cmd,
                             ResMut<assets::Assets<mesh::Mesh>> meshes,
                             Query<Item<Entity, Mut<PixelBody>>> bodies) {
    for (auto&& [body_ent, body_mut] : bodies.iter()) {
        auto& body = body_mut.get_mut();
        if (!body.mesh_dirty) continue;

        // Build per-cell quads (cell size = 1 unit in body-local space).
        std::vector<glm::vec3> positions;
        std::vector<glm::vec4> colors;
        std::vector<std::uint32_t> indices;
        positions.reserve(body.cells.count() * 4);
        colors.reserve(body.cells.count() * 4);
        indices.reserve(body.cells.count() * 6);
        std::uint32_t base = 0;
        for (auto&& [pos, e] : body.cells.iter()) {
            float x = static_cast<float>(pos[0]);
            float y = static_cast<float>(pos[1]);
            positions.push_back({x, y, 0.0f});
            positions.push_back({x + 1.0f, y, 0.0f});
            positions.push_back({x + 1.0f, y + 1.0f, 0.0f});
            positions.push_back({x, y + 1.0f, 0.0f});
            for (int i = 0; i < 4; ++i) colors.push_back(e.color);
            indices.push_back(base);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
            indices.push_back(base);
            base += 4;
        }
        auto new_mesh = mesh::Mesh()
                            .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
                            .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
                            .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
                            .with_indices<std::uint32_t>(indices);
        auto handle   = meshes->emplace(std::move(new_mesh));

        if (body.mesh_child_entity.has_value()) {
            cmd.entity(*body.mesh_child_entity).insert(mesh::Mesh2d{handle});
        } else {
            auto child             = cmd.entity(body_ent)
                                         .spawn(mesh::Mesh2d{handle},
                                                mesh::MeshMaterial2d{
                                                    .color      = glm::vec4(1.0f),
                                                    .alpha_mode = mesh::MeshAlphaMode2d::Opaque,
                                                },
                                                transform::Transform{.translation = glm::vec3(0.0f)})
                                         .id();
            body.mesh_child_entity = child;
        }
        body.mesh_dirty = false;
    }
}

void PixelBodyPlugin::build(epix::core::App& app) {
    using namespace epix::core;
    app.add_systems(PreUpdate,
                    into(init_pixel_body_worlds, init_pixel_bodies, rebuild_pixel_body_shapes, sync_transforms_to_b2)
                        .set_names(std::array{
                            "pixelbody init_worlds",
                            "pixelbody init_bodies",
                            "pixelbody rebuild_shapes",
                            "pixelbody sync_to_b2",
                        }));
    app.add_systems(Update, into(update_sand_static_bodies, step_pixel_body_worlds)
                                .set_names(std::array{
                                    "pixelbody update_static_bodies",
                                    "pixelbody step_worlds",
                                }));
    app.add_systems(PostUpdate, into(sync_b2_to_transforms, interact_pixel_body_with_sand, build_pixel_body_meshes)
                                    .set_names(std::array{
                                        "pixelbody sync_from_b2",
                                        "pixelbody interact_with_sand",
                                        "pixelbody build_meshes",
                                    }));
}

}  // namespace epix::experimental::pixelbody
