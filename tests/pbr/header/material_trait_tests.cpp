#include <epix/pbr/material.hpp>
#include <epix/render/alpha.hpp>
#include <gtest/gtest.h>

#include <string_view>
#include <type_traits>

namespace pbr    = epix::pbr;
namespace render = epix::render;

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
