#include <epix/pbr/material.hpp>
#include <epix/render/alpha.hpp>
#include <gtest/gtest.h>
#include <memory>

#include <string_view>
#include <type_traits>

namespace pbr    = epix::pbr;
namespace render = epix::render;
namespace ecs    = epix::ecs;

namespace {
/** @brief A material that overrides nothing: every member must then answer with
 *  Bevy's default. */
struct PlainMaterial : pbr::Material<PlainMaterial> {};

/** @brief A material that overrides everything a material can. */
struct CustomMaterial : pbr::Material<CustomMaterial> {
    using Data = int;

    static std::optional<std::string_view> vertex_shader() { return "custom/vertex.slang"; }
    static std::optional<std::string_view> fragment_shader() { return "custom/fragment.slang"; }
    render::AlphaMode alpha_mode() const { return render::AlphaMode::mask(0.25f); }
    pbr::OpaqueRendererMethod opaque_render_method() const { return pbr::OpaqueRendererMethod::Deferred; }
    float depth_bias() const { return 1.5f; }
    bool reads_view_transmission_texture() const { return true; }
    static bool enable_prepass() { return false; }
    static bool enable_shadows() { return false; }
};
}  // namespace

/** @brief The 3D material trait answers with Bevy's defaults (Bevy `Material`,
 *  `bevy_pbr/src/material.rs:138-273`).
 *
 *  A default in a trait is invisible at the call site, so a port can get one wrong
 *  without any test noticing: `enable_prepass` defaults to **true**, for instance,
 *  which is the opposite of what "prepasses are optional" suggests. This pins the
 *  defaults directly, and needs no device - the trait is entirely static data and
 *  dispatch. */
TEST(MaterialTrait, DefaultsMatchBevyAndOverridesWin) {
    const PlainMaterial plain;
    EXPECT_EQ(plain.alpha_mode(), render::AlphaMode::opaque());
    EXPECT_EQ(plain.opaque_render_method(), pbr::OpaqueRendererMethod::Forward);
    EXPECT_FLOAT_EQ(plain.depth_bias(), 0.0f);
    EXPECT_FALSE(plain.reads_view_transmission_texture());

    EXPECT_EQ(PlainMaterial::vertex_shader(), std::nullopt) << "Bevy's ShaderRef::Default is `nullopt` here";
    EXPECT_EQ(PlainMaterial::fragment_shader(), std::nullopt);
    EXPECT_TRUE(PlainMaterial::enable_prepass()) << "Bevy's default is true, which is easy to invert";
    EXPECT_TRUE(PlainMaterial::enable_shadows());
    EXPECT_EQ(PlainMaterial::prepass_vertex_shader(), std::nullopt);
    EXPECT_EQ(PlainMaterial::deferred_fragment_shader(), std::nullopt);

    // The overrides answer on the material itself. This port's CRTP is static
    // dispatch, so a call through `Material<M>&` reaches the *base* defaults - which
    // is why the pipeline holds `M` and not the base (the 2D counterpart works the
    // same way). Asserting it here keeps the design honest rather than discovering
    // it in a pipeline.
    const CustomMaterial custom;
    EXPECT_EQ(custom.alpha_mode(), render::AlphaMode::mask(0.25f));
    EXPECT_EQ(custom.opaque_render_method(), pbr::OpaqueRendererMethod::Deferred);
    EXPECT_FLOAT_EQ(custom.depth_bias(), 1.5f);
    EXPECT_TRUE(custom.reads_view_transmission_texture());
    const pbr::Material<CustomMaterial>& as_base = custom;
    EXPECT_EQ(as_base.alpha_mode(), render::AlphaMode::opaque())
        << "static dispatch: the base answers with its own default";
    EXPECT_FALSE(CustomMaterial::enable_prepass());
    EXPECT_FALSE(CustomMaterial::enable_shadows());
    EXPECT_EQ(CustomMaterial::vertex_shader(), std::optional<std::string_view>{"custom/vertex.slang"});

    // The associated data type is Bevy's `M::Data`, and absent means monostate.
    static_assert(std::is_same_v<pbr::MaterialData<CustomMaterial>::type, int>);
    static_assert(std::is_same_v<pbr::MaterialData<PlainMaterial>::type, std::monostate>);

    // The key carries the mesh key and that data, and compares by them.
    const pbr::MaterialPipelineKey<CustomMaterial> key{.mesh_key = {}, .bind_group_data = 7};
    const pbr::MaterialPipelineKey<CustomMaterial> same{.mesh_key = {}, .bind_group_data = 7};
    EXPECT_TRUE(key == same);
    EXPECT_EQ(pbr::MATERIAL_BIND_GROUP_INDEX, 2u) << "Bevy's MATERIAL_BIND_GROUP";
}

/** @brief The prepared-material properties default the way Bevy's do (Bevy
 *  `MaterialProperties`, `bevy_pbr/src/material.rs:1510-1585`).
 *
 *  These are the values the pipeline reads for every material that overrides
 *  nothing, so a wrong default here changes how an entire class of materials is
 *  drawn - the same reason the trait's own defaults are pinned one test above. */
TEST(MaterialTrait, PreparedPropertiesDefaultToForwardOpaque) {
    const pbr::MaterialProperties properties;
    EXPECT_EQ(properties.render_method, pbr::OpaqueRendererMethod::Forward);
    EXPECT_EQ(properties.alpha_mode, render::AlphaMode::opaque());
    EXPECT_EQ(properties.render_phase_type, pbr::RenderPhaseType::Opaque);
    EXPECT_FLOAT_EQ(properties.depth_bias, 0.0f);
    EXPECT_FALSE(properties.reads_view_transmission_texture) << "a field on the properties, not the trait's accessor";
    EXPECT_FALSE(properties.bindless) << "bindless is decided from platform support, not assumed";
    EXPECT_TRUE(properties.draw_functions.empty());
    EXPECT_TRUE(properties.shaders.empty());

    // The prepared material ties a binding to those properties.
    auto shared = std::make_shared<pbr::MaterialProperties>();
    const pbr::PreparedMaterial prepared{.binding = pbr::MaterialBindingId{}, .properties = shared};
    ASSERT_TRUE(prepared.properties != nullptr);
    EXPECT_EQ(prepared.properties.get(), shared.get()) << "the properties are shared, as Bevy shares them in an Arc";
}

/** @brief The material pipeline cache is non-generic and starts empty (Bevy
 *  `SpecializedMaterialPipelineCache` and `SpecializedMaterialViewPipelineCache`,
 *  `material.rs:952-967`).
 *
 *  Bevy 0.18's 3D cache holds no material type where the 2D counterpart is generic
 *  over it - the ids it stores are already specialized - and it is keyed by the
 *  retained view and then by the material's main entity, holding each entity's
 *  pipeline id with the tick it was specialized at. */
TEST(MaterialTrait, PipelineCacheIsNonGenericAndKeyedPerEntity) {
    pbr::SpecializedMaterialPipelineCache cache;
    EXPECT_TRUE(cache.map.empty()) << "a fresh cache holds no view";

    pbr::SpecializedMaterialViewPipelineCache view_cache;
    EXPECT_TRUE(view_cache.map.empty());
    EXPECT_TRUE((std::is_same_v<decltype(view_cache.map),
                                render::sync_world::MainEntityHashMap<std::pair<ecs::Tick, render::CachedRenderPipelineId>>>))
        << "an entity maps to its pipeline id and the tick it was specialized at";

    view_cache.map[render::sync_world::MainEntity{ecs::Entity::from_index(7)}] = {ecs::Tick{3},
                                                                                render::CachedRenderPipelineId{9}};
    ASSERT_EQ(view_cache.map.size(), 1u);
    EXPECT_EQ(view_cache.map.begin()->second.first.get(), 3u);
}

/** @brief The erased material key keeps the payload's type, hash and equality
 *  (Bevy `ErasedMaterialKey`, `material.rs:1430-1445`).
 *
 *  One specialization cache serves every material type, so this is what keeps two
 *  types with equal bytes apart: the type is part of the key, not only the value.
 *  Bevy stores the `TypeId`, a precomputed hash and the boxed value; a key that got
 *  any of the three wrong would silently share a pipeline between materials. */
TEST(MaterialTrait, ErasedKeyComparesTypeAndValue) {
    const pbr::ErasedMaterialKey same_a{7};
    const pbr::ErasedMaterialKey same_b{7};
    const pbr::ErasedMaterialKey other_value{8};
    const pbr::ErasedMaterialKey other_type{7u};

    EXPECT_EQ(same_a, same_b);
    EXPECT_EQ(same_a.hash(), same_b.hash()) << "the hash is precomputed from the value, as Bevy's is";
    EXPECT_NE(same_a, other_value);
    EXPECT_NE(same_a, other_type) << "same numeric value, different type: still a different key";
    EXPECT_TRUE(same_a.type_id() == same_b.type_id());
    EXPECT_FALSE(same_a.type_id() == other_type.type_id());
}

/** @brief The erased pipeline key carries the mesh half, the erased material key and
 *  the material type (Bevy `ErasedMaterialPipelineKey`, `material.rs:438-443`).
 *
 *  The two are not redundant and this pins that: `material_key` identifies the
 *  bind-group data a material family specializes on, `type_id` the material type it
 *  belongs to. A key that dropped either would send a material to a pipeline built
 *  for another. */
TEST(MaterialTrait, ErasedPipelineKeyComparesAllThreeParts) {
    const pbr::ErasedMaterialKey material_key{11};
    const pbr::ErasedMaterialKey other_material_key{12};

    const pbr::ErasedMaterialPipelineKey base{
        .mesh_key = {}, .material_key = material_key, .type_id = epix::meta::type_id<PlainMaterial>()};
    const pbr::ErasedMaterialPipelineKey same{
        .mesh_key = {}, .material_key = material_key, .type_id = epix::meta::type_id<PlainMaterial>()};
    const pbr::ErasedMaterialPipelineKey other_material{
        .mesh_key = {}, .material_key = other_material_key, .type_id = epix::meta::type_id<PlainMaterial>()};
    const pbr::ErasedMaterialPipelineKey other_type{
        .mesh_key = {}, .material_key = material_key, .type_id = epix::meta::type_id<CustomMaterial>()};

    EXPECT_EQ(base, same);
    EXPECT_EQ(std::hash<pbr::ErasedMaterialPipelineKey>{}(base),
              std::hash<pbr::ErasedMaterialPipelineKey>{}(same));
    EXPECT_NE(base, other_material) << "the erased bind-group data is part of the key";
    EXPECT_NE(base, other_type) << "so is the material type, which Bevy keeps beside it";
}

/** @brief A prepared material's shader list is read back by label (Bevy
 *  `MaterialProperties::get_shader`, `material.rs:1478-1490`).
 *
 *  The specialization applies these overrides, so a lookup that missed - or that
 *  answered with the wrong slot - would silently specialize a material against the
 *  default mesh shader. The list holds one entry per label, and the labels are
 *  distinct types, so asking for the other one must answer nothing. */
TEST(MaterialTrait, ShaderLookupIsByLabel) {
    const auto vertex   = epix::assets::Handle<epix::shader::Shader>{
        epix::assets::AssetId<epix::shader::Shader>::invalid()};
    pbr::MaterialProperties properties;
    properties.shaders.emplace_back(
        render::label::intern(render::phase::ShaderLabel{pbr::MaterialVertexShader{}}), vertex);

    ASSERT_NE(properties.get_shader<pbr::MaterialVertexShader>(), nullptr);
    EXPECT_TRUE(*properties.get_shader<pbr::MaterialVertexShader>() == vertex);
    EXPECT_EQ(properties.get_shader<pbr::MaterialFragmentShader>(), nullptr)
        << "a label with no entry answers nothing rather than the first shader in the list";
}

/** @brief The material specializer is usable by the mesh specialization cache (Bevy
 *  `SpecializedMeshPipeline for MaterialPipelineSpecializer`, `material.rs:456`).
 *
 *  The cache keys on `S::Key` and calls `specialize(key, layout)`, so this is what
 *  makes the erased specializer usable at all: a key type or signature that drifted
 *  would fail here rather than in a system no test drives. Checked at compile time,
 *  since the pipeline it produces needs a device. */
static_assert(render::SpecializedMeshPipeline<pbr::MaterialPipelineSpecializer>);
static_assert(std::is_same_v<pbr::MaterialPipelineSpecializer::Key, pbr::ErasedMaterialPipelineKey>);