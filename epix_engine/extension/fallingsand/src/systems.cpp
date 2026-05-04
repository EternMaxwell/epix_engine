module;
#ifndef EPIX_IMPORT_STD
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <vector>
#endif
#include <spdlog/spdlog.h>

module epix.extension.fallingsand;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import webgpu;

using namespace epix::core;

namespace epix::ext::fallingsand {

// ──────────────────────────────────────────────────────────────────────────────
// Static helpers
// ──────────────────────────────────────────────────────────────────────────────

static mesh::Mesh build_chunk_mesh(const grid::Chunk<kDim>& chunk, float cell_size) {
    // Skip Body-type elements (alpha == 0) — they are invisible placeholders.
    auto visible =
        std::views::filter(chunk.iter<Element>(), [](const auto& pair) { return pair.second.color.a >= 0.001f; });
    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> colors;
    std::vector<std::uint32_t> indices;
    std::uint32_t base = 0;
    for (auto&& [pos, elem] : visible) {
        float x = static_cast<float>(pos[0]) * cell_size;
        float y = static_cast<float>(pos[1]) * cell_size;
        positions.push_back({x, y, 0.0f});
        positions.push_back({x + cell_size, y, 0.0f});
        positions.push_back({x + cell_size, y + cell_size, 0.0f});
        positions.push_back({x, y + cell_size, 0.0f});
        for (int i = 0; i < 4; ++i) colors.push_back(elem.color);
        indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
        base += 4;
    }
    return mesh::Mesh()
        .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
        .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
        .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
        .with_indices<std::uint32_t>(indices);
}

/// Build a debug mesh that highlights Body-type sentinel elements (alpha == 0)
/// with a distinct debug colour.  Used when SandWorld::show_body_debug() is true.
static mesh::Mesh build_chunk_body_debug_mesh(const grid::Chunk<kDim>& chunk, float cell_size) {
    constexpr glm::vec4 kDebugColor{1.0f, 0.15f, 1.0f, 0.65f};  // magenta semi-transparent
    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> colors;
    std::vector<std::uint32_t> indices;
    std::uint32_t base = 0;
    for (auto&& [pos, elem] : chunk.iter<Element>()) {
        if (elem.color.a >= 0.001f) continue;  // only body (invisible) elements
        float x = static_cast<float>(pos[0]) * cell_size;
        float y = static_cast<float>(pos[1]) * cell_size;
        positions.push_back({x, y, 0.0f});
        positions.push_back({x + cell_size, y, 0.0f});
        positions.push_back({x + cell_size, y + cell_size, 0.0f});
        positions.push_back({x, y + cell_size, 0.0f});
        for (int i = 0; i < 4; ++i) colors.push_back(kDebugColor);
        indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
        base += 4;
    }
    return mesh::Mesh()
        .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
        .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
        .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
        .with_indices<std::uint32_t>(indices);
}

static mesh::Mesh build_chunk_outline_mesh(std::size_t chunk_width, float cell_size) {
    float extent = static_cast<float>(chunk_width) * cell_size;
    std::array<glm::vec3, 4> positions{
        {{0.0f, 0.0f, 0.0f}, {extent, 0.0f, 0.0f}, {extent, extent, 0.0f}, {0.0f, extent, 0.0f}}};
    std::array<glm::vec4, 4> colors{glm::vec4(0.95f, 0.95f, 0.95f, 1.0f), glm::vec4(0.95f, 0.95f, 0.95f, 1.0f),
                                    glm::vec4(0.95f, 0.95f, 0.95f, 1.0f), glm::vec4(0.95f, 0.95f, 0.95f, 1.0f)};
    std::array<std::uint16_t, 8> indices{{0, 1, 1, 2, 2, 3, 3, 0}};
    return mesh::Mesh()
        .with_primitive_type(wgpu::PrimitiveTopology::eLineList)
        .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
        .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
        .with_indices<std::uint16_t>(indices);
}
// ──────────────────────────────────────────────────────────────────────────────
// setup_chunk_dirty_rects
// ──────────────────────────────────────────────────────────────────────────────

void setup_chunk_dirty_rects(
    Commands cmd,
    Query<Item<Entity, const Parent&>, Filter<With<grid::Chunk<kDim>, SandChunkPos>, Without<SandChunkDirtyRect>>>
        new_chunks,
    Query<Item<const SandWorld&>> worlds,
    Query<Item<Entity, const SandChunkPos&, Opt<Mut<SandChunkDirtyRect>>, const Parent&>, With<grid::Chunk<kDim>>>
        all_chunks) {
    for (auto&& [entity, parent] : new_chunks.iter()) {
        auto world_opt = worlds.get(parent.entity());
        if (!world_opt.has_value()) continue;
        auto&& [sand_world] = *world_opt;
        auto cw             = static_cast<std::int32_t>(std::size_t(1) << sand_world.chunk_shift());
        cmd.entity(entity).insert(SandChunkDirtyRect::make_full(cw));

        // Wake the 1-cell-thick border of each existing neighboring chunk that
        // faces the newly-added chunk, so settled particles re-evaluate.
        auto self_opt = all_chunks.get(entity);
        if (!self_opt.has_value()) continue;
        auto&& [self_ent, self_pos, self_dr_opt, self_par] = *self_opt;

        // For each cardinal neighbor: find it, touch the facing edge cells.
        //   +x neighbor → its local x=0    column
        //   -x neighbor → its local x=cw-1 column
        //   +y neighbor → its local y=0    row
        //   -y neighbor → its local y=cw-1 row
        const std::array<std::array<std::int32_t, 2>, 4> offsets{{{{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}}}};
        // Two extreme local points to touch for each direction's facing edge.
        const std::array<std::pair<std::int32_t, std::int32_t>, 4> p0{{{0, 0}, {cw - 1, 0}, {0, 0}, {0, cw - 1}}};
        const std::array<std::pair<std::int32_t, std::int32_t>, 4> p1{
            {{0, cw - 1}, {cw - 1, cw - 1}, {cw - 1, 0}, {cw - 1, cw - 1}}};

        for (std::size_t i = 0; i < 4; ++i) {
            std::array<std::int32_t, kDim> npos = {
                self_pos.value[0] + offsets[i][0],
                self_pos.value[1] + offsets[i][1],
            };
            for (auto&& [nent, nchunk_pos, ndr_opt, npar] : all_chunks.iter()) {
                if (npar.entity() != parent.entity()) continue;
                if (nchunk_pos.value != npos) continue;
                if (!ndr_opt.has_value()) break;  // neighbor is also new, skip
                auto& dr = ndr_opt->get_mut();
                dr.touch(p0[i].first, p0[i].second);
                dr.touch(p1[i].first, p1[i].second);
                break;
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// setup_chunk_render_children
// ──────────────────────────────────────────────────────────────────────────────

void setup_chunk_render_children(
    Commands cmd,
    ResMut<assets::Assets<mesh::Mesh>> meshes,
    Query<Item<Entity, const Parent&>, Filter<With<grid::Chunk<kDim>, SandChunkPos>, Without<SandChunkRenderChildren>>>
        new_chunks,
    Query<Entity, With<SandWorld, transform::Transform, MeshBuildByPlugin>> mesh_worlds) {
    for (auto&& [chunk_entity, parent] : new_chunks.iter()) {
        bool parent_has_mesh_build = mesh_worlds.contains(parent.entity());

        if (parent_has_mesh_build) {
            // Spawn an empty mesh child entity now; build_chunk_meshes fills it each tick.
            auto empty_mesh = meshes->emplace(mesh::Mesh{});
            auto mesh_ent   = cmd.entity(chunk_entity)
                                  .spawn(SandChunkMesh{}, mesh::Mesh2d{empty_mesh},
                                         mesh::MeshMaterial2d{
                                             .color      = glm::vec4(1.0f),
                                             .alpha_mode = mesh::MeshAlphaMode2d::Opaque,
                                         },
                                         transform::Transform{.translation = glm::vec3(0.0f)})
                                  .id();
            cmd.entity(chunk_entity)
                .insert(SandChunkRenderChildren{
                    .mesh_entity    = mesh_ent,
                    .outline_entity = std::nullopt,
                });
        } else {
            cmd.entity(chunk_entity)
                .insert(SandChunkRenderChildren{
                    .mesh_entity    = std::nullopt,
                    .outline_entity = std::nullopt,
                });
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// simulate_worlds
// ──────────────────────────────────────────────────────────────────────────────

void simulate_worlds(
    Res<ElementRegistry> registry,
    Query<Item<Entity, Mut<SandWorld>, const transform::Transform&, Opt<const Children&>>, With<SimulatedByPlugin>>
        worlds,
    Query<Item<Mut<grid::Chunk<kDim>>, const SandChunkPos&, Mut<SandChunkDirtyRect>, const Parent&>> all_chunks) {
    for (auto&& [world_entity, sand_world, world_transform, maybe_children] : worlds.iter()) {
        if (!maybe_children.has_value()) continue;
        const auto& child_entities = maybe_children->get().entities();

        if (sand_world.get().paused()) continue;

        auto chunk_range =
            child_entities | std::views::filter([&all_chunks](Entity e) { return all_chunks.get(e).has_value(); }) |
            std::views::transform([&all_chunks](Entity e)
                                      -> std::tuple<Mut<grid::Chunk<kDim>>, const SandChunkPos&, SandChunkDirtyRect&> {
                auto opt                                 = all_chunks.get(e);
                auto&& [chunk, pos, dirty_rect, par_ref] = *opt;
                return {chunk, pos, dirty_rect.get_mut()};
            });

        auto sim_result = SandSimulation::create(sand_world.get_mut(), registry.get(), chunk_range);
        if (!sim_result.has_value()) {
            std::visit(
                [&world_entity](const auto& err) {
                    using T = std::decay_t<decltype(err)>;
                    if constexpr (std::is_same_v<T, sand_sim_error::DuplicateChunkPos>) {
                        spdlog::error(
                            "[FallingSandPlugin] duplicate SandChunkPos ({},{}) under world "
                            "entity index={}. Skipping simulation this tick.",
                            err.pos[0], err.pos[1], world_entity.index);
                    } else {
                        spdlog::error(
                            "[FallingSandPlugin] chunk insert failed for world entity "
                            "index={}. Skipping simulation this tick.",
                            world_entity.index);
                    }
                },
                sim_result.error());
            continue;
        }
        sim_result->step();

        // Per-chunk: count time, swap buffers when threshold reached;
        // if settled, force-sleep all cells (matches old ChunkMap::count_time + swap_area).
        for (Entity ce : child_entities) {
            auto c = all_chunks.get(ce);
            if (!c.has_value()) continue;
            auto&& [chunk, pos, dirty_rect, par] = *c;
            bool settled                         = dirty_rect.get_mut().count_time();
            if (settled) {
                for (auto&& [lpos, elem] : chunk.get_mut().iter_mut<Element>()) {
                    elem.set_freefall(false);
                    elem.velocity = {};
                }
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// sync_chunk_transforms
// ──────────────────────────────────────────────────────────────────────────────

void sync_chunk_transforms(Query<Item<Entity, transform::Transform&, const SandChunkPos&, const Parent&>> chunks,
                           Query<Item<const SandWorld&>, With<SandWorld>> worlds) {
    for (auto&& [chunk_entity, chunk_transform, pos, parent_comp] : chunks.iter()) {
        auto world_data = worlds.get(parent_comp.entity());
        if (!world_data.has_value()) continue;
        auto&& [sand_world] = *world_data;
        float chunk_world_size =
            static_cast<float>(std::size_t(1) << sand_world.chunk_shift()) * sand_world.cell_size();
        chunk_transform.translation.x = static_cast<float>(pos.value[0]) * chunk_world_size;
        chunk_transform.translation.y = static_cast<float>(pos.value[1]) * chunk_world_size;
        chunk_transform.translation.z = 0.0f;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// build_chunk_meshes
// ──────────────────────────────────────────────────────────────────────────────

void build_chunk_meshes(
    Commands cmd,
    Query<Item<Entity, Ref<grid::Chunk<kDim>>, const SandChunkRenderChildren&, Mut<SandChunkDirtyRect>, const Parent&>>
        chunks,
    Query<Item<const SandWorld&>, Filter<With<SandWorld, MeshBuildByPlugin>>> worlds,
    ResMut<assets::Assets<mesh::Mesh>> meshes) {
    for (auto&& [chunk_entity, chunk_ref, render_children, dirty_rect, parent_comp] : chunks.iter()) {
        auto& dr = dirty_rect.get_mut();
        // Skip chunks whose Chunk component was not modified since the last
        // build_chunk_meshes run.
        if (!chunk_ref.is_modified()) continue;

        if (!render_children.mesh_entity.has_value()) continue;
        auto world_data = worlds.get(parent_comp.entity());
        if (!world_data.has_value()) continue;
        auto&& [sand_world] = *world_data;

        auto new_mesh = build_chunk_mesh(chunk_ref.get(), sand_world.cell_size());
        auto handle   = meshes->emplace(std::move(new_mesh));
        cmd.entity(*render_children.mesh_entity).insert(mesh::Mesh2d{handle});
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// build_body_debug_meshes
// ──────────────────────────────────────────────────────────────────────────────

void build_body_debug_meshes(
    Commands cmd,
    Query<Item<Entity, Ref<grid::Chunk<kDim>>, Mut<SandChunkRenderChildren>, Mut<SandChunkDirtyRect>, const Parent&>>
        chunks,
    Query<Item<const SandWorld&, Opt<const SandWorldDebug&>>, Filter<With<SandWorld, MeshBuildByPlugin>>> worlds,
    ResMut<assets::Assets<mesh::Mesh>> meshes) {
    for (auto&& [chunk_entity, chunk_ref, render_children_mut, dirty_rect, parent_comp] : chunks.iter()) {
        auto world_data = worlds.get(parent_comp.entity());
        if (!world_data.has_value()) continue;
        auto&& [sand_world, debug_opt] = *world_data;
        bool show_body_debug           = debug_opt.has_value() && debug_opt->get().show_body_debug;

        auto& rc = render_children_mut.get_mut();
        if (show_body_debug) {
            // Only rebuild when chunk data changed.
            if (!chunk_ref.is_modified()) {
                if (rc.body_debug_entity.has_value()) continue;  // already exists, nothing changed
            }
            auto debug_mesh = build_chunk_body_debug_mesh(chunk_ref.get(), sand_world.cell_size());
            auto handle     = meshes->emplace(std::move(debug_mesh));
            if (rc.body_debug_entity.has_value()) {
                cmd.entity(*rc.body_debug_entity).insert(mesh::Mesh2d{handle});
            } else {
                auto de              = cmd.entity(chunk_entity)
                                           .spawn(SandChunkBodyDebug{}, mesh::Mesh2d{handle},
                                                  mesh::MeshMaterial2d{
                                                      .color      = glm::vec4(1.0f),
                                                      .alpha_mode = mesh::MeshAlphaMode2d::Blend,
                                                  },
                                                  transform::Transform{.translation = glm::vec3(0.0f, 0.0f, -0.3f)})
                                           .id();
                rc.body_debug_entity = de;
            }
        } else if (rc.body_debug_entity.has_value()) {
            cmd.entity(*rc.body_debug_entity).despawn();
            rc.body_debug_entity.reset();
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// update_chunk_outlines
// ──────────────────────────────────────────────────────────────────────────────

void update_chunk_outlines(
    Commands cmd,
    Query<Item<Entity, Mut<SandChunkRenderChildren>, const Parent&>> chunks,
    Query<Item<const SandWorld&, Opt<const SandWorldDebug&>>, With<SandWorld, transform::Transform>> worlds,
    ResMut<assets::Assets<mesh::Mesh>> meshes) {
    for (auto&& [chunk_entity, render_children, parent_comp] : chunks.iter()) {
        auto world_data = worlds.get(parent_comp.entity());
        if (!world_data.has_value()) continue;
        auto&& [sand_world, debug_opt] = *world_data;
        bool show_outline              = debug_opt.has_value() && debug_opt->get().show_chunk_outlines;

        float current_cell_size = sand_world.cell_size();
        auto& rc                = render_children.get_mut();
        std::size_t cwidth      = std::size_t(1) << sand_world.chunk_shift();

        if (show_outline && !rc.outline_entity.has_value()) {
            auto outline_mesh = meshes->emplace(build_chunk_outline_mesh(cwidth, current_cell_size));
            auto oe           = cmd.entity(chunk_entity)
                                    .spawn(SandChunkOutline{}, mesh::Mesh2d{outline_mesh},
                                           mesh::MeshMaterial2d{
                                               .color      = glm::vec4(1.0f, 1.0f, 1.0f, 0.55f),
                                               .alpha_mode = mesh::MeshAlphaMode2d::Blend,
                                           },
                                           transform::Transform{.translation = glm::vec3(0.0f, 0.0f, -0.01f)},
                                           render::camera::RenderLayer::layer(2))
                                    .id();
            rc.outline_entity = oe;
        } else if (!show_outline && rc.outline_entity.has_value()) {
            cmd.entity(*rc.outline_entity).despawn();
            rc.outline_entity.reset();
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// FallingSandPlugin::build
// ──────────────────────────────────────────────────────────────────────────────

void FallingSandPlugin::build(core::App& app) {
    app.add_systems(core::Update,
                    into(setup_chunk_dirty_rects, setup_chunk_render_children)
                        .set_names(std::array{"fallingsand setup_dirty_rects", "fallingsand setup_render_children"}));

    app.add_systems(time::FixedUpdate, into(simulate_worlds).set_name("fallingsand simulate"));

    app.add_systems(time::FixedPostUpdate,
                    into(sync_chunk_transforms, build_chunk_meshes, update_chunk_outlines)
                        .set_names(std::array{"fallingsand sync_transforms", "fallingsand build_meshes",
                                              "fallingsand update_outlines"}));
}

void BodyDebugPlugin::build(core::App& app) {
    app.add_systems(time::FixedPostUpdate, into(build_body_debug_meshes).set_name("fallingsand body_debug_meshes"));
}

}  // namespace epix::ext::fallingsand
