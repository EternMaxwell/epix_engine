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
#include <unordered_set>
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
    body.cells     = grid::dense_grid<2, fs::Element>(std::array<std::uint32_t, 2>{w, h});
    auto base_opt  = registry.get(base_id);
    auto get_color = [&](std::uint64_t s) -> glm::vec4 {
        if (!base_opt) return glm::vec4(1, 1, 1, 1);
        return base_opt->get().construct_func(0, base_opt->get(), s).color;
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

/// Adapter so dense_grid<2, fs::Element> satisfies grid::poly_grid directly
/// — since dense_grid already satisfies any_grid with pos_type = array<uint32_t, 2>,
/// it can be passed directly to polygon functions without an adapter.

/// Adapter wrapping a ChunkElementGrid (tree_grid<2, fs::Element>) to expose only Solid cells,
/// satisfying grid::poly_grid.
struct ChunkSolidGridView {
    const fs::ChunkElementGrid& chunk;
    const fs::ElementRegistry& registry;

    std::array<std::uint32_t, 2> dimensions() const {
        auto d = chunk.dimensions();
        return {d[0], d[1]};
    }
    bool contains(const std::array<std::uint32_t, 2>& pos) const {
        auto d = chunk.dimensions();
        if (pos[0] >= d[0] || pos[1] >= d[1]) return false;
        auto r = chunk.get(pos);
        if (!r.has_value()) return false;
        auto base = registry.get(r->get().base_id);
        return base.has_value() && base->get().type == fs::ElementType::Solid;
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
        if (B2_IS_NON_NULL(body_mut.get().b2_body)) continue;
        auto& body = body_mut.get_mut();
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
    for (auto&& [body_mut, parent] : bodies.iter()) {
        if (B2_IS_NULL(body_mut.get().b2_body)) continue;
        if (!body_mut.get().shapes_dirty) continue;
        auto& body = body_mut.get_mut();

        // Look up cell_size from the parent PixelBodyWorld so shapes are built in
        // world (pixel) units rather than cell units.
        float cell_size = 1.0f;
        if (auto wopt = worlds.get(parent.entity()); wopt.has_value()) {
            auto&& [w] = *wopt;
            cell_size  = w.cell_size;
        }

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

        // Extract polygons from the cell grid (dense_grid satisfies poly_grid directly).
        auto polys = grid::get_polygons_simplified_multi(body.cells, 0.5f);
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
                    b2Vec2{static_cast<float>(verts[idx[t]].x) * cell_size,
                           static_cast<float>(verts[idx[t]].y) * cell_size},
                    b2Vec2{static_cast<float>(verts[idx[t + 1]].x) * cell_size,
                           static_cast<float>(verts[idx[t + 1]].y) * cell_size},
                    b2Vec2{static_cast<float>(verts[idx[t + 2]].x) * cell_size,
                           static_cast<float>(verts[idx[t + 2]].y) * cell_size},
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
        // Skip sleeping bodies — b2Body_SetTransform always wakes the body in
        // Box2D v3, so calling it unconditionally prevents bodies from ever
        // staying asleep.  If Box2D owns the authoritative position (sleeping),
        // the ECS transform was already updated by sync_b2_to_transforms and
        // there is nothing to write back.
        if (!b2Body_IsAwake(body.b2_body)) continue;
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
               Ref<fs::ChunkElementGrid>,
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
            auto&& [c_ent, chunk_ref, cpos, dr_opt, ssb_opt] = *chunk_opt;

            // Skip entirely if the chunk data hasn't changed and we already
            // have an up-to-date static body — avoids unnecessary BinaryGrid
            // snapshot work and prevents spurious SandStaticBody modification.
            if (!chunk_ref.is_modified() && !chunk_ref.is_added() && ssb_opt.has_value() && !ssb_opt->get().dirty) {
                continue;
            }

            // Build a "is solid" snapshot for this chunk using BinaryGrid
            // from the grid module so we can compare structurally next frame.
            ChunkSolidGridView solid_view{chunk_ref.get(), *registry};
            auto snapshot = grid::rasterise(solid_view);

            // Decide rebuild trigger: structural change of solid cells, or
            // the static body component is missing entirely, or an explicit
            // dirty flag was set.
            bool need_rebuild = false;
            if (!ssb_opt.has_value()) {
                need_rebuild = true;
            } else {
                const auto& ssb = ssb_opt->get();
                if (ssb.dirty) need_rebuild = true;
                if (ssb.solid_snapshot != snapshot) need_rebuild = true;
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
            auto polys = grid::get_polygons_simplified_multi(solid_view, 0.5f);

            if (polys.empty()) {
                if (ssb_opt.has_value()) {
                    auto& ssb          = ssb_opt->get_mut();
                    ssb.dirty          = false;
                    ssb.solid_snapshot = std::move(snapshot);
                } else {
                    cmd.entity(c_ent).insert(SandStaticBody{
                        .b2_body        = b2_nullBodyId,
                        .chains         = {},
                        .dirty          = false,
                        .solid_snapshot = std::move(snapshot),
                    });
                }
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
                auto& ssb          = ssb_opt->get_mut();
                ssb.b2_body        = static_body;
                ssb.chains         = std::move(chains);
                ssb.dirty          = false;
                ssb.solid_snapshot = std::move(snapshot);
            } else {
                cmd.entity(c_ent).insert(SandStaticBody{
                    .b2_body        = static_body,
                    .chains         = std::move(chains),
                    .dirty          = false,
                    .solid_snapshot = std::move(snapshot),
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

void sync_pixel_body_to_sand(
    Commands cmd,
    ResMut<fs::ElementRegistry> registry,
    Query<
        Item<Entity, const PixelBodyWorld&, Mut<fs::SandWorld>, Opt<const Children&>, Opt<Mut<PixelBodySandBlockers>>>>
        worlds,
    Query<Item<const PixelBody&, const transform::Transform&>> bodies,
    Query<Item<Mut<fs::ChunkElementGrid>,
               Mut<fs::ChunkAirGrid>,
               Mut<fs::ChunkThermalGrid>,
               const fs::SandChunkPos&,
               Mut<fs::SandChunkDirtyRect>>> chunks) {
    // Auto-register the blocker element once.
    static std::size_t blocker_id   = std::numeric_limits<std::size_t>::max();
    static bool blocker_id_resolved = false;
    if (!blocker_id_resolved) {
        auto id_res = registry->get_id("pixel_body_blocker");
        if (id_res.has_value()) {
            blocker_id          = *id_res;
            blocker_id_resolved = true;
        } else {
            auto reg_res = registry->register_element(fs::ElementBase{
                .name           = "pixel_body_blocker",
                .density        = 0.0f,
                .type           = fs::ElementType::Body,
                .construct_func = [](std::size_t id, const fs::ElementBase&,
                                     std::uint64_t) { return fs::Element{id, glm::vec4(0.0f)}; },
            });
            if (reg_res.has_value()) {
                blocker_id          = *reg_res;
                blocker_id_resolved = true;
            }
        }
    }
    if (!blocker_id_resolved) return;

    const fs::Element blocker_elem{blocker_id, glm::vec4(0.0f)};

    // Helper: pack two int32s into one int64 key.
    auto encode = [](std::int64_t x, std::int64_t y) -> std::int64_t {
        return (x & 0xFFFFFFFFLL) | ((y & 0xFFFFFFFFLL) << 32);
    };

    constexpr int kMaxPush = 64;

    for (auto&& [world_ent, w, sand_world_mut, world_children_opt, blockers_opt] : worlds.iter()) {
        if (B2_IS_NULL(w.b2_world)) continue;
        if (!world_children_opt.has_value()) continue;

        // Ensure PixelBodySandBlockers component exists on the world entity.
        if (!blockers_opt.has_value()) {
            cmd.entity(world_ent).insert(PixelBodySandBlockers{});
            continue;  // will run with the component next frame
        }

        auto& sw  = sand_world_mut.get_mut();
        float scs = sw.cell_size();
        if (scs <= 0.0f) continue;

        const std::size_t shift = sw.chunk_shift();
        std::vector<std::tuple<grid::Chunk<fs::kDim>, const fs::SandChunkPos&, fs::SandChunkDirtyRect&>> chunk_tuples;
        for (Entity e : world_children_opt->get().entities()) {
            auto opt = chunks.get(e);
            if (!opt.has_value()) continue;
            auto&& [elem_g, air_g, therm_g, p, d] = *opt;
            grid::Chunk<fs::kDim> chunk(shift);
            (void)chunk.add_layer(
                std::make_unique<grid::layers::BasicGridRefLayer<fs::ChunkElementGrid>>(shift, std::move(elem_g)));
            (void)chunk.add_layer(
                std::make_unique<grid::layers::BasicGridRefLayer<fs::ChunkAirGrid>>(shift, std::move(air_g)));
            (void)chunk.add_layer(
                std::make_unique<grid::layers::BasicGridRefLayer<fs::ChunkThermalGrid>>(shift, std::move(therm_g)));
            chunk_tuples.emplace_back(std::move(chunk), p, d.get_mut());
        }
        auto chunk_range = chunk_tuples | std::views::as_rvalue;
        auto sim_res     = fs::SandSimulation::create(sw, registry.get(), chunk_range);
        if (!sim_res.has_value()) continue;
        auto& sim = *sim_res;

        std::int64_t dir_y = (w.gravity_cells.y < 0.0f) ? 1 : -1;

        // ── Step 1: Build current occupied set (AABB-based) ──────────────────
        // Instead of mapping each body-cell centre to one sand cell (which
        // leaves rotation-induced holes), we compute the world AABB of every
        // body cell, enumerate all sand cells in that AABB, then inverse-
        // transform each sand-cell centre back into body-local space to test
        // whether it truly falls inside a body cell.  This guarantees no holes
        // and produces a stable set that doesn't change on sub-cell movements.
        std::unordered_set<std::int64_t> current_set;
        for (Entity body_ent : world_children_opt->get().entities()) {
            auto body_opt = bodies.get(body_ent);
            if (!body_opt.has_value()) continue;
            auto&& [body, body_tf] = *body_opt;
            if (body.cells.count() == 0) continue;

            // World-space AABB: test all 4 corners of every occupied cell.
            float min_wx = std::numeric_limits<float>::max();
            float max_wx = std::numeric_limits<float>::lowest();
            float min_wy = std::numeric_limits<float>::max();
            float max_wy = std::numeric_limits<float>::lowest();
            for (auto&& [pos, elem] : body.cells.iter()) {
                (void)elem;
                for (int cx_i = 0; cx_i <= 1; ++cx_i) {
                    for (int cy_i = 0; cy_i <= 1; ++cy_i) {
                        glm::vec3 corner_local(static_cast<float>(pos[0] + static_cast<std::uint32_t>(cx_i)) * scs,
                                               static_cast<float>(pos[1] + static_cast<std::uint32_t>(cy_i)) * scs,
                                               0.0f);
                        glm::vec3 w = body_tf.translation + body_tf.rotation * corner_local;
                        min_wx      = std::min(min_wx, w.x);
                        max_wx      = std::max(max_wx, w.x);
                        min_wy      = std::min(min_wy, w.y);
                        max_wy      = std::max(max_wy, w.y);
                    }
                }
            }

            std::int64_t sx_min = static_cast<std::int64_t>(std::floor(min_wx / scs));
            std::int64_t sx_max = static_cast<std::int64_t>(std::floor(max_wx / scs));
            std::int64_t sy_min = static_cast<std::int64_t>(std::floor(min_wy / scs));
            std::int64_t sy_max = static_cast<std::int64_t>(std::floor(max_wy / scs));

            glm::quat inv_rot = glm::inverse(body_tf.rotation);
            auto body_dims    = body.cells.dimensions();

            for (std::int64_t sy = sy_min; sy <= sy_max; ++sy) {
                for (std::int64_t sx = sx_min; sx <= sx_max; ++sx) {
                    // Sand-cell centre in world space.
                    glm::vec3 world_center((static_cast<float>(sx) + 0.5f) * scs, (static_cast<float>(sy) + 0.5f) * scs,
                                           0.0f);
                    // Transform to body-local space.
                    glm::vec3 local = inv_rot * (world_center - body_tf.translation);
                    auto bx         = static_cast<std::int32_t>(std::floor(local.x / scs));
                    auto by         = static_cast<std::int32_t>(std::floor(local.y / scs));
                    if (bx < 0 || by < 0) continue;
                    if (static_cast<std::uint32_t>(bx) >= body_dims[0] ||
                        static_cast<std::uint32_t>(by) >= body_dims[1])
                        continue;
                    if (!body.cells.contains({static_cast<std::uint32_t>(bx), static_cast<std::uint32_t>(by)}))
                        continue;
                    current_set.insert(encode(sx, sy));
                }
            }
        }

        auto& old_cells = blockers_opt->get_mut().cells;

        // ── Step 2: Remove stale blockers ─────────────────────────────────────
        // Cells the body vacated: erase WITH touch so sleeping sand above falls.
        // Cells still occupied: remove WITHOUT touch (body is still there).
        for (std::int64_t key : old_cells) {
            std::int64_t ox = key & 0xFFFFFFFFLL;
            std::int64_t oy = (key >> 32) & 0xFFFFFFFFLL;
            // Sign-extend from 32-bit.
            if (ox >= (1LL << 31)) ox -= (1LL << 32);
            if (oy >= (1LL << 31)) oy -= (1LL << 32);

            // Only remove cells that actually hold our blocker.
            auto cell = sim.get_cell<fs::Element>({ox, oy});
            if (!cell.has_value()) continue;
            bool is_blocker = (cell->get().base_id == blocker_id);
            if (!is_blocker) continue;

            if (!current_set.contains(key)) {
                // Body moved away — wake the sand above so it falls.
                sim.erase_cell({ox, oy});
            } else {
                // Body still here — remove quietly (will be re-inserted below).
                (void)sim.remove_cell<fs::Element>({ox, oy});
            }
        }

        // ── Step 3: Push real sand out of body cells, then insert blockers ───
        // Iterate current_set (the AABB-derived coverage) instead of iterating
        // per body cell.  This covers every sand cell whose centre maps inside
        // a body cell, with no holes.
        bool any_displaced = false;
        for (std::int64_t key : current_set) {
            std::int64_t sx = key & 0xFFFFFFFFLL;
            std::int64_t sy = (key >> 32) & 0xFFFFFFFFLL;
            if (sx >= (1LL << 31)) sx -= (1LL << 32);
            if (sy >= (1LL << 31)) sy -= (1LL << 32);

            auto existing = sim.get_cell<fs::Element>({sx, sy});
            if (existing.has_value()) {
                auto base_opt = (*registry).get(existing->get().base_id);
                if (!base_opt.has_value()) continue;
                auto etype = base_opt->get().type;
                // Push displaceable sand out of the way.
                if (etype != fs::ElementType::Body && etype != fs::ElementType::Solid) {
                    fs::Element to_move = existing->get();
                    for (int k = 1; k <= kMaxPush; ++k) {
                        std::int64_t ty = sy + dir_y * static_cast<std::int64_t>(k);
                        if (current_set.contains(encode(sx, ty))) continue;
                        auto target = sim.get_cell<fs::Element>({sx, ty});
                        if (target.has_value()) continue;
                        if (sim.put_cell({sx, ty}, to_move)) {
                            sim.erase_cell({sx, sy});
                            any_displaced = true;
                        }
                        break;
                    }
                }
                // If anything still occupies this cell, respect it — do not overwrite.
                if (sim.get_cell<fs::Element>({sx, sy}).has_value()) continue;
            }

            // Insert blocker without touch — no dirty rect for static body.
            (void)sim.insert_cell({sx, sy}, fs::Element(blocker_elem));
        }

        // Wake every body in this world if any sand was displaced.
        if (any_displaced) {
            for (Entity body_ent : world_children_opt->get().entities()) {
                auto body_opt = bodies.get(body_ent);
                if (!body_opt.has_value()) continue;
                auto&& [body, body_tf] = *body_opt;
                (void)body_tf;
                if (B2_IS_NON_NULL(body.b2_body)) b2Body_SetAwake(body.b2_body, true);
            }
        }

        // ── Step 4: Store current set for next frame ──────────────────────────
        old_cells = current_set;
    }
}

void build_pixel_body_meshes(Commands cmd,
                             ResMut<assets::Assets<mesh::Mesh>> meshes,
                             Query<Item<Entity, Mut<PixelBody>, const Parent&>> bodies,
                             Query<Item<const PixelBodyWorld&>> worlds) {
    for (auto&& [body_ent, body_mut, parent] : bodies.iter()) {
        if (!body_mut.get().mesh_dirty) continue;
        auto& body = body_mut.get_mut();

        // Look up cell_size from the parent PixelBodyWorld so the mesh is built in
        // world (pixel) units, matching the Box2D shapes.
        float cell_size = 1.0f;
        if (auto wopt = worlds.get(parent.entity()); wopt.has_value()) {
            auto&& [w] = *wopt;
            cell_size  = w.cell_size;
        }

        // Build per-cell quads (cell size = cell_size units in body-local space).
        std::vector<glm::vec3> positions;
        std::vector<glm::vec4> colors;
        std::vector<std::uint32_t> indices;
        positions.reserve(body.cells.count() * 4);
        colors.reserve(body.cells.count() * 4);
        indices.reserve(body.cells.count() * 6);
        std::uint32_t base = 0;
        for (auto&& [pos, e] : body.cells.iter()) {
            float x = static_cast<float>(pos[0]) * cell_size;
            float y = static_cast<float>(pos[1]) * cell_size;
            positions.push_back({x, y, 0.0f});
            positions.push_back({x + cell_size, y, 0.0f});
            positions.push_back({x + cell_size, y + cell_size, 0.0f});
            positions.push_back({x, y + cell_size, 0.0f});
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
    app.add_systems(PostUpdate, into(sync_b2_to_transforms, build_pixel_body_meshes)
                                    .set_names(std::array{
                                        "pixelbody sync_from_b2",
                                        "pixelbody build_meshes",
                                    }));
    app.add_systems(time::FixedPreUpdate, into(sync_pixel_body_to_sand).set_name("pixelbody sync_to_sand"));
}

}  // namespace epix::experimental::pixelbody
