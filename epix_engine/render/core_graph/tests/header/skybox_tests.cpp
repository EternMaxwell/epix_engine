#include <epix/core_graph/skybox.hpp>
#include <epix/image.hpp>
#include <gtest/gtest.h>

using namespace epix::core_graph;

namespace {

/** @brief The component's defaults are Bevy's: the default image, no brightness
 *  scale and no rotation (`skybox/mod.rs:99-107`). */
TEST(Skybox, DefaultsMatchBevy) {
    const Skybox skybox;
    EXPECT_EQ(skybox.image.id(), epix::image::DEFAULT_IMAGE_HANDLE.id());
    EXPECT_FLOAT_EQ(skybox.brightness, 0.0f);
    EXPECT_FLOAT_EQ(skybox.rotation.x, 0.0f);
    EXPECT_FLOAT_EQ(skybox.rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(skybox.rotation.z, 0.0f);
    EXPECT_FLOAT_EQ(skybox.rotation.w, 1.0f);
}

/** @brief The uniforms hold the exposure-scaled brightness and the inverse
 *  rotation as a matrix, and their layout matches Bevy's `ShaderType` for
 *  `{ f32, Mat4 }` (`skybox/mod.rs:114-148`). */
TEST(Skybox, UniformsLayoutMatchesBevyShaderType) {
    EXPECT_EQ(sizeof(SkyboxUniforms), 80u) << "4 bytes of brightness and padding plus a 64-byte Mat4";
    EXPECT_EQ(offsetof(SkyboxUniforms, brightness), 0u);
    EXPECT_EQ(offsetof(SkyboxUniforms, transform), 16u);

    const SkyboxUniforms uniforms{};
    EXPECT_FLOAT_EQ(uniforms.brightness, 1.0f);
    EXPECT_FLOAT_EQ(uniforms.transform[0][0], 1.0f);
}

/** @brief The specialization key is Bevy's `SkyboxPipelineKey`: the HDR flag,
 *  the sample count and the depth format (`skybox/mod.rs:182-187`). */
TEST(Skybox, PipelineKeyComparesAllThreeFields) {
    const SkyboxPipelineKey base{};
    EXPECT_EQ(base, (SkyboxPipelineKey{}));
    EXPECT_NE(base, (SkyboxPipelineKey{.hdr = true}));
    EXPECT_NE(base, (SkyboxPipelineKey{.samples = 4}));
    EXPECT_NE(base, (SkyboxPipelineKey{.depth_format = wgpu::TextureFormat::eDepth32Float}));

    // The key is a hash-map key for `SpecializedRenderPipelines`.
    const std::hash<SkyboxPipelineKey> hash{};
    EXPECT_EQ(hash(base), hash(SkyboxPipelineKey{}));
    EXPECT_NE(hash(base), hash(SkyboxPipelineKey{.hdr = true}));
}

/** @brief `specialize` writes Bevy's descriptor: one bind group, no depth
 *  writes, a `GreaterEqual` depth compare, the key's sample count and the HDR
 *  or default colour format with no blend (`skybox/mod.rs:189-238`). */
TEST(Skybox, SpecializeBuildsBevyDescriptor) {
    const epix::assets::Handle<epix::shader::Shader> shader{
        epix::assets::AssetId<epix::shader::Shader>::invalid()};
    const auto pipeline = SkyboxPipeline::create(shader);

    // `SkyboxPipeline::new`'s layout: the cube texture and sampler, the dynamic
    // view uniform on both stages, and the skybox uniforms (`:156-175`).
    EXPECT_EQ(pipeline.bind_group_layout.label, "skybox_bind_group_layout");
    ASSERT_EQ(pipeline.bind_group_layout.entries.size(), 4u);
    const auto& entries = pipeline.bind_group_layout.entries;
    EXPECT_EQ(entries[0].binding, 0u);
    EXPECT_EQ(entries[0].texture.viewDimension, wgpu::TextureViewDimension::eCube);
    EXPECT_EQ(entries[0].texture.sampleType, wgpu::TextureSampleType::eFloat);
    EXPECT_EQ(entries[1].sampler.type, wgpu::SamplerBindingType::eFiltering);
    EXPECT_EQ(entries[2].buffer.type, wgpu::BufferBindingType::eUniform);
    EXPECT_EQ(entries[2].buffer.hasDynamicOffset, true);
    EXPECT_EQ(entries[2].visibility, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment);
    EXPECT_EQ(entries[3].buffer.minBindingSize, sizeof(SkyboxUniforms));

    const auto descriptor = pipeline.specialize(SkyboxPipelineKey{
        .hdr = false, .samples = 4, .depth_format = wgpu::TextureFormat::eDepth32Float});
    EXPECT_EQ(descriptor.label, "skybox_pipeline");
    ASSERT_EQ(descriptor.layouts.size(), 1u);
    ASSERT_TRUE(descriptor.depth_stencil.has_value());
    EXPECT_EQ(descriptor.depth_stencil->format, wgpu::TextureFormat::eDepth32Float);
    EXPECT_EQ(descriptor.depth_stencil->depthWriteEnabled, wgpu::OptionalBool::eFalse);
    EXPECT_EQ(descriptor.depth_stencil->depthCompare, wgpu::CompareFunction::eGreaterEqual);
    EXPECT_EQ(descriptor.multisample.count, 4u);
    ASSERT_TRUE(descriptor.fragment.has_value());
    ASSERT_EQ(descriptor.fragment->targets.size(), 1u);
    EXPECT_EQ(descriptor.fragment->targets[0].format, epix::image::bevy_default<wgpu::TextureFormat>());
    EXPECT_EQ(descriptor.vertex.entry_point, std::optional<std::string>{"vertex"});
    EXPECT_EQ(descriptor.fragment->entry_point, std::optional<std::string>{"fragment"});

    const auto hdr = pipeline.specialize(SkyboxPipelineKey{
        .hdr = true, .samples = 1, .depth_format = wgpu::TextureFormat::eDepth32Float});
    ASSERT_TRUE(hdr.fragment.has_value());
    ASSERT_EQ(hdr.fragment->targets.size(), 1u);
    EXPECT_EQ(hdr.fragment->targets[0].format, epix::render::view::ViewTarget::TEXTURE_FORMAT_HDR);
    EXPECT_EQ(hdr.multisample.count, 1u);
}

}  // namespace
