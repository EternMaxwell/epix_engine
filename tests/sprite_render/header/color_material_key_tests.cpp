// SRD-112: specialization data is independent of a material's GPU uniform.
#include <gtest/gtest.h>

#include <epix/sprite_render/color_material.hpp>
#include <functional>
#include <type_traits>
#include <variant>

namespace {
namespace sprite_render = epix::sprite_render;
namespace resource      = epix::render::render_resource;
using Material          = sprite_render::ColorMaterial;
using Uniform           = sprite_render::ColorMaterialUniform;
using BindGroup         = resource::AsBindGroup<Material>;
using Key               = sprite_render::Material2dKey<Material>;

auto material_key(const Material& material) {
    return sprite_render::Material2dKey<Material>{
        .mesh_key        = sprite_render::alpha_mode_pipeline_key(material.alpha_mode()),
        .bind_group_data = BindGroup::bind_group_data(material),
    };
}

TEST(ColorMaterialKey, SpecializationDataIsUnitNotTheGpuUniform) {
    EXPECT_TRUE((std::is_same_v<BindGroup::Data, std::monostate>));
    EXPECT_TRUE((std::is_same_v<sprite_render::Material2dData<Material>::type, std::monostate>));
}

TEST(ColorMaterialKey, DifferentTintsAndUvTransformsShareTheirSpecializationKey) {
    auto first                = Material::from_color(epix::color::Color::srgb(1.0f, 0.0f, 0.0f));
    auto second               = Material::from_color(epix::color::Color::srgb(0.0f, 1.0f, 0.0f));
    second.uv_transform[0][0] = 1.5f;
    const auto first_uniform = resource::AsBindGroupShaderType<Material, Uniform>::as_bind_group_shader_type(first, {});
    const auto second_uniform =
        resource::AsBindGroupShaderType<Material, Uniform>::as_bind_group_shader_type(second, {});
    ASSERT_NE(first_uniform.color.x, second_uniform.color.x);
    ASSERT_NE(first_uniform.uv_transform[0].x, second_uniform.uv_transform[0].x);

    const auto first_key  = material_key(first);
    const auto second_key = material_key(second);
    EXPECT_EQ(first_key, second_key);
    EXPECT_EQ(std::hash<Key>{}(first_key), std::hash<Key>{}(second_key));
}

TEST(ColorMaterialKey, MaskCutoffsRemainUniformValuesNotShaderVariants) {
    Material first;
    Material second;
    first.mode               = sprite_render::AlphaMode2dMask{0.25f};
    second.mode              = sprite_render::AlphaMode2dMask{0.75f};
    const auto first_uniform = resource::AsBindGroupShaderType<Material, Uniform>::as_bind_group_shader_type(first, {});
    const auto second_uniform =
        resource::AsBindGroupShaderType<Material, Uniform>::as_bind_group_shader_type(second, {});
    ASSERT_NE(first_uniform.alpha_cutoff, second_uniform.alpha_cutoff);
    EXPECT_EQ(material_key(first), material_key(second));
}

TEST(ColorMaterialKey, DistinctAlphaModesStillHaveDistinctMeshKeys) {
    Material opaque;
    Material masked;
    Material blended;
    opaque.mode  = sprite_render::AlphaMode2dOpaque{};
    masked.mode  = sprite_render::AlphaMode2dMask{0.5f};
    blended.mode = sprite_render::AlphaMode2dBlend{};
    EXPECT_NE(material_key(opaque), material_key(masked));
    EXPECT_NE(material_key(opaque), material_key(blended));
    EXPECT_NE(material_key(masked), material_key(blended));
}
}  // namespace
