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
    auto positions_view = std::views::join(std::views::transform(
        std::views::elements<0>(chunk.iter<Element>()), [cell_size](std::array<std::uint32_t, kDim> pos) {
            float x = static_cast<float>(pos[0]) * cell_size;
            float y = static_cast<float>(pos[1]) * cell_size;
            return std::array<glm::vec3, 4>{{{x, y, 0.0f},
                                             {x + cell_size, y, 0.0f},
                                             {x + cell_size, y + cell_size, 0.0f},
                                             {x, y + cell_size, 0.0f}}};
        }));
    auto colors_view    = std::views::join(std::views::transform(chunk.iter<Element>(), [](auto&& pair) {
        auto&& [pos, elem] = pair;
        return std::array<glm::vec4, 4>{elem.color, elem.color, elem.color, elem.color};
    }));
    auto indices_view =
        std::views::join(std::views::transform(std::views::enumerate(chunk.iter<Element>()), [](auto&& indexed_pair) {
            auto&& [i, pair]   = indexed_pair;
            std::uint32_t base = static_cast<std::uint32_t>(i) * 4;
            return std::array<std::uint32_t, 6>{{base, base + 1, base + 2, base + 2, base + 3, base}};
        }));
    return mesh::Mesh()
        .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
        .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions_view)
        .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors_view)
        .with_indices<std::uint32_t>(indices_view);
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
        const std::array<std::array<std::int32_t, 2>, 4> offsets{{
            {{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}}
        }};
        // Two extreme local points to touch for each direction's facing edge.
        const std::array<std::pair<std::int32_t, std::int32_t>, 4> p0{{{0, 0}, {cw-1, 0}, {0, 0}, {0, cw-1}}};
        const std::array<std::pair<std::int32_t, std::int32_t>, 4> p1{{{0, cw-1}, {cw-1, cw-1}, {cw-1, 0}, {cw-1, cw-1}}};

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
            std::views::transform(
                [&all_chunks](Entity e) -> std::tuple<grid::Chunk<kDim>&, const SandChunkPos&, SandChunkDirtyRect&> {
                    auto opt                                 = all_chunks.get(e);
                    auto&& [chunk, pos, dirty_rect, par_ref] = *opt;
                    return {chunk.get_mut(), pos, dirty_rect.get_mut()};
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
                dirty_rect.get_mut().needs_rebuild = true;
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
    Query<
        Item<Entity, const grid::Chunk<kDim>&, const SandChunkRenderChildren&, Mut<SandChunkDirtyRect>, const Parent&>>
        chunks,
    Query<Item<const SandWorld&>, Filter<With<SandWorld, MeshBuildByPlugin>>> worlds,
    ResMut<assets::Assets<mesh::Mesh>> meshes) {
    for (auto&& [chunk_entity, chunk, render_children, dirty_rect, parent_comp] : chunks.iter()) {
        auto& dr = dirty_rect.get_mut();
        if (!dr.active() && !dr.needs_rebuild) continue;  // skip chunks that haven't changed
        dr.needs_rebuild = false;
        // Note: dirty rects are reset by simulate_worlds at the start of each tick.

        if (!render_children.mesh_entity.has_value()) continue;
        auto world_data = worlds.get(parent_comp.entity());
        if (!world_data.has_value()) continue;
        auto&& [sand_world] = *world_data;

        auto new_mesh = build_chunk_mesh(chunk, sand_world.cell_size());
        auto handle   = meshes->emplace(std::move(new_mesh));
        cmd.entity(*render_children.mesh_entity).insert(mesh::Mesh2d{handle});
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// update_chunk_outlines
// ──────────────────────────────────────────────────────────────────────────────

void update_chunk_outlines(Commands cmd,
                           Query<Item<Entity, Mut<SandChunkRenderChildren>, const Parent&>> chunks,
                           Query<Item<const SandWorld&>, With<SandWorld, transform::Transform>> worlds,
                           ResMut<assets::Assets<mesh::Mesh>> meshes) {
    for (auto&& [chunk_entity, render_children, parent_comp] : chunks.iter()) {
        auto world_data = worlds.get(parent_comp.entity());
        if (!world_data.has_value()) continue;
        auto&& [sand_world] = *world_data;

        float current_cell_size = sand_world.cell_size();
        auto& rc                = render_children.get_mut();
        bool show_outline       = sand_world.show_chunk_outlines();
        std::size_t cwidth      = std::size_t(1) << sand_world.chunk_shift();

        bool cell_size_changed =
            rc.outline_entity.has_value() && std::abs(rc.outline_cell_size - current_cell_size) > 1e-6f;

        if (cell_size_changed && rc.outline_entity.has_value()) {
            cmd.entity(*rc.outline_entity).despawn();
            rc.outline_entity.reset();
        }

        if (show_outline && !rc.outline_entity.has_value()) {
            auto outline_mesh    = meshes->emplace(build_chunk_outline_mesh(cwidth, current_cell_size));
            auto oe              = cmd.entity(chunk_entity)
                                       .spawn(SandChunkOutline{}, mesh::Mesh2d{outline_mesh},
                                              mesh::MeshMaterial2d{
                                                  .color      = glm::vec4(1.0f, 1.0f, 1.0f, 0.55f),
                                                  .alpha_mode = mesh::MeshAlphaMode2d::Blend,
                                              },
                                              transform::Transform{.translation = glm::vec3(0.0f, 0.0f, 0.01f)},
                                              render::camera::RenderLayer::layer(2))
                                       .id();
            rc.outline_entity    = oe;
            rc.outline_cell_size = current_cell_size;
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

}  // namespace epix::ext::fallingsand
