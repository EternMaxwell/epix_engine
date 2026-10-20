#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/render/sync_world.hpp>
#include <array>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;
namespace camera = epix::camera;
namespace sync = epix::render::sync_world;

enum class MapField { Cascades, Frusta, Visible };
using VisibleMap = decltype(pbr::RenderCascadesVisibleEntities::entities);

class DirectionalExtraction : public testing::Test {
   protected:
    ecs::World main_world{2};
    ecs::World render_world{2};
    ecs::Entity light;
    ecs::Entity render_light;
    std::array<ecs::Entity, 2> meshes;
    std::array<ecs::Entity, 2> render_meshes;
    std::unordered_map<ecs::Entity, ecs::Entity> view_mapping;

    void SetUp() override {
        render_world.insert_resource(epix::app::ExtractedWorld{main_world});
        main_world.init_resource<pbr::PointLightShadowMap>();
        main_world.init_resource<pbr::DirectionalLightShadowMap>();
        main_world.init_resource<pbr::GlobalVisibleClusterableObjects>();
        // Main and render IDs must differ: otherwise an unrekeyed map can pass.
        for (int index = 0; index < 16; ++index) render_world.spawn();
        render_light = render_world.spawn().id();
        for (std::size_t index = 0; index < meshes.size(); ++index) {
            render_meshes[index] = render_world.spawn().id();
            meshes[index] = main_world.spawn(sync::RenderEntity{render_meshes[index]}).id();
        }
        pbr::Cascades cascades;
        camera::CascadesFrusta frusta;
        camera::CascadesVisibleEntities visible;
        // Disjoint view sets prove that breaking one map does not abandon the
        // other two independent loops or the light's final insertion.
        for (int field = 0; field < 3; ++field) {
            for (int index = 0; index < 3; ++index) {
                const auto render_view = render_world.spawn().id();
                const auto view = main_world.spawn(sync::RenderEntity{render_view}).id();
                ASSERT_NE(view, render_view);
                view_mapping.emplace(view, render_view);
                if (field == 0) {
                    cascades.cascades.emplace(view, std::vector{pbr::Cascade{.texel_size = float(index + 1)}});
                } else if (field == 1) {
                    frusta.frusta.emplace(view, std::vector<camera::Frustum>(index + 1));
                } else {
                    visible.entities.emplace(view, std::vector<camera::VisibleMeshEntities>(
                        index + 1, camera::VisibleMeshEntities{.entities = {meshes[0], meshes[1]}}));
                }
            }
        }
        camera::ViewVisibility visibility;
        visibility.set_visible();
        light = main_world.spawn(pbr::DirectionalLight{}, sync::RenderEntity{render_light},
            std::move(cascades), std::move(frusta), std::move(visible), pbr::CascadeShadowConfig{},
            epix::transform::GlobalTransform{}, visibility).id();
    }

    std::vector<ecs::Entity> source_order(MapField field) const {
        std::vector<ecs::Entity> keys;
        const auto append = [&](const auto& map) {
            for (const auto& [key, value] : map) keys.push_back(key);
        };
        const auto entity = main_world.entity(light);
        switch (field) {
            case MapField::Cascades: append(entity.get<pbr::Cascades>()->get().cascades); break;
            case MapField::Frusta: append(entity.get<camera::CascadesFrusta>()->get().frusta); break;
            case MapField::Visible: append(entity.get<camera::CascadesVisibleEntities>()->get().entities); break;
        }
        return keys;
    }

    void extract() {
        auto system = ecs::make_system_unique(pbr::extract_lights);
        system->initialize(render_world);
        ASSERT_TRUE(system->run({}, render_world).has_value());
        system->apply_deferred(render_world);
    }

    void expect_map_keys(MapField field, const std::vector<ecs::Entity>& retained) const {
        const auto extracted = render_world.entity(render_light).get<pbr::ExtractedDirectionalLight>();
        const auto visible = render_world.entity(render_light).get<pbr::RenderCascadesVisibleEntities>();
        ASSERT_TRUE(extracted);
        ASSERT_TRUE(visible);
        const auto check = [&](const auto& map) {
            EXPECT_EQ(map.size(), retained.size());
            for (const auto main_view : retained) {
                const auto render_view = view_mapping.at(main_view);
                using Key = typename std::remove_cvref_t<decltype(map)>::key_type;
                EXPECT_TRUE(map.contains(Key{render_view}));
                EXPECT_FALSE(map.contains(Key{main_view}));
            }
        };
        switch (field) {
            case MapField::Cascades: check(extracted->get().cascades); break;
            case MapField::Frusta: check(extracted->get().frusta); break;
            case MapField::Visible: check(visible->get().entities); break;
        }
    }
};

struct MissingViewCase { MapField field; std::size_t retained; };
class MissingDirectionalView : public DirectionalExtraction,
                               public testing::WithParamInterface<MissingViewCase> {};

TEST_P(MissingDirectionalView, StopsOnlyTheAffectedMapAtItsFirstMissingMapping) {
    const auto [field, retained] = GetParam();
    // Inspect the actual stored map's iteration order. Rust and C++ hash maps
    // need not share ordering, but both must stop at their first missing view.
    auto keys = source_order(field);
    ASSERT_EQ(keys.size(), 3u);
    main_world.entity_mut(keys[retained]).remove<sync::RenderEntity>();
    ASSERT_NO_FATAL_FAILURE(extract());
    for (const auto current : {MapField::Cascades, MapField::Frusta, MapField::Visible}) {
        auto expected = source_order(current);
        if (current == field) expected.resize(retained);
        expect_map_keys(current, expected);
    }
}

std::string missing_view_case_name(const testing::TestParamInfo<MissingViewCase>& info) {
    const std::array names{"Cascades", "Frusta", "Visible"};
    return std::string(names[static_cast<std::size_t>(info.param.field)]) + std::to_string(info.param.retained);
}

INSTANTIATE_TEST_SUITE_P(AllMaps, MissingDirectionalView, testing::Values(
    MissingViewCase{MapField::Cascades, 0}, MissingViewCase{MapField::Cascades, 1},
    MissingViewCase{MapField::Cascades, 2}, MissingViewCase{MapField::Frusta, 0},
    MissingViewCase{MapField::Frusta, 1}, MissingViewCase{MapField::Frusta, 2},
    MissingViewCase{MapField::Visible, 0}, MissingViewCase{MapField::Visible, 1},
    MissingViewCase{MapField::Visible, 2}), missing_view_case_name);

TEST_F(DirectionalExtraction, FullyMappedViewsRetainTheirCascadePayloads) {
    ASSERT_NO_FATAL_FAILURE(extract());
    for (const auto field : {MapField::Cascades, MapField::Frusta, MapField::Visible})
        expect_map_keys(field, source_order(field));
    const auto& out = render_world.entity(render_light).get<pbr::ExtractedDirectionalLight>()->get();
    for (const auto& [main_view, cascades] : main_world.entity(light).get<pbr::Cascades>()->get().cascades) {
        const auto& result = out.cascades.at(view_mapping.at(main_view));
        ASSERT_EQ(result.size(), cascades.size());
        EXPECT_FLOAT_EQ(result[0].texel_size, cascades[0].texel_size);
    }
    for (const auto& [main_view, frusta] : main_world.entity(light).get<camera::CascadesFrusta>()->get().frusta)
        EXPECT_EQ(out.frusta.at(view_mapping.at(main_view)).size(), frusta.size());
}

TEST_F(DirectionalExtraction, CascadeVisibleMapIsKeyedByRawRenderEntity) {
    using Expected = std::unordered_map<ecs::Entity, std::vector<pbr::RenderVisibleMeshEntities>>;
    EXPECT_TRUE((std::is_same_v<VisibleMap, Expected>));
}

TEST_F(DirectionalExtraction, MissingVisibleMeshKeepsItsMainIdentityAndPlaceholderSlot) {
    const auto missing_mesh = main_world.spawn().id();
    auto& views = main_world.entity_mut(light).get_mut<camera::CascadesVisibleEntities>()->get_mut().entities;
    const auto main_view = views.begin()->first;
    auto& list = views.begin()->second[0].entities;
    list.insert(list.begin() + 1, missing_mesh);
    ASSERT_NO_FATAL_FAILURE(extract());
    const auto& visible = render_world.entity(render_light).get<pbr::RenderCascadesVisibleEntities>()->get();
    const auto& result = visible.entities.at(VisibleMap::key_type{view_mapping.at(main_view)})[0].entities;
    ASSERT_EQ(result.size(), 3u) << "An unmapped mesh is a placeholder slot, not a dropped row";
    EXPECT_EQ(result[0], (std::pair{render_meshes[0], sync::MainEntity{meshes[0]}}));
    EXPECT_EQ(result[1], (std::pair{ecs::Entity::PLACEHOLDER, sync::MainEntity{missing_mesh}}));
    EXPECT_EQ(result[2], (std::pair{render_meshes[1], sync::MainEntity{meshes[1]}}));
}

TEST_F(DirectionalExtraction, VisibilityLossRemovesBothExtractedComponents) {
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_TRUE(render_world.entity(render_light).get<pbr::ExtractedDirectionalLight>());
    main_world.entity_mut(light).insert(camera::ViewVisibility::HIDDEN);
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_FALSE(render_world.entity(render_light).get<pbr::ExtractedDirectionalLight>());
    EXPECT_FALSE(render_world.entity(render_light).get<pbr::RenderCascadesVisibleEntities>());
    const auto main = render_world.entity(render_light).get<sync::MainEntity>();
    ASSERT_TRUE(main);
    EXPECT_EQ(main->get().id(), light);
}
}  // namespace
