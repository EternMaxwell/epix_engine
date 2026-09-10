#include <gtest/gtest.h>

#include <array>
#include <epix/image.hpp>

// ImageSamplerDescriptor mirrors bevy_image (image.rs:758-842): linear()/nearest()
// factories, set_filter, set_address_mode, wgpu-default descriptor values.
TEST(ImageSamplerDescriptor, FactoriesAndMutators) {
    auto linear = epix::image::ImageSamplerDescriptor::linear();
    EXPECT_EQ(linear.mag_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(linear.min_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(linear.mipmap_filter, epix::image::ImageFilterMode::Linear);

    auto nearest = epix::image::ImageSamplerDescriptor::nearest();
    EXPECT_EQ(nearest.mag_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(nearest.min_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(nearest.mipmap_filter, epix::image::ImageFilterMode::Nearest);

    // Defaults match wgpu SamplerDescriptor::default() (Bevy
    // ImageSamplerDescriptor::default, image.rs:784-800).
    epix::image::ImageSamplerDescriptor def;
    EXPECT_EQ(def.address_mode_u, epix::image::ImageAddressMode::ClampToEdge);
    EXPECT_EQ(def.address_mode_v, epix::image::ImageAddressMode::ClampToEdge);
    EXPECT_EQ(def.address_mode_w, epix::image::ImageAddressMode::ClampToEdge);
    EXPECT_EQ(def.mag_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(def.min_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(def.mipmap_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(def.lod_min_clamp, 0.0f);
    EXPECT_EQ(def.lod_max_clamp, 32.0f);
    EXPECT_FALSE(def.compare.has_value());
    EXPECT_EQ(def.anisotropy_clamp, 1);
    EXPECT_FALSE(def.border_color.has_value());

    // set_filter / set_address_mode mutate all three axes (Bevy
    // ImageSamplerDescriptor::set_filter / set_address_mode).
    def.set_filter(epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(def.mag_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(def.min_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(def.mipmap_filter, epix::image::ImageFilterMode::Linear);
    def.set_address_mode(epix::image::ImageAddressMode::MirrorRepeat);
    EXPECT_EQ(def.address_mode_u, epix::image::ImageAddressMode::MirrorRepeat);
    EXPECT_EQ(def.address_mode_v, epix::image::ImageAddressMode::MirrorRepeat);
    EXPECT_EQ(def.address_mode_w, epix::image::ImageAddressMode::MirrorRepeat);

    // set_anisotropic_filter forces linear filters, which anisotropy requires
    // (Bevy ImageSamplerDescriptor::set_anisotropic_filter, image.rs:848-854).
    epix::image::ImageSamplerDescriptor anisotropic;
    anisotropic.set_anisotropic_filter(8);
    EXPECT_EQ(anisotropic.mag_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(anisotropic.min_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(anisotropic.mipmap_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(anisotropic.anisotropy_clamp, 8);

    // as_wgpu carries every field across to the WebGPU descriptor (Bevy
    // ImageSamplerDescriptor::as_wgpu, image.rs:856-870).
    epix::image::ImageSamplerDescriptor to_wgpu;
    to_wgpu.label = "wgpu sampler";
    to_wgpu.set_filter(epix::image::ImageFilterMode::Linear);
    to_wgpu.set_address_mode(epix::image::ImageAddressMode::MirrorRepeat);
    to_wgpu.lod_min_clamp      = 1.5f;
    to_wgpu.lod_max_clamp      = 9.5f;
    to_wgpu.compare            = epix::image::ImageCompareFunction::LessEqual;
    const auto wgpu_descriptor = to_wgpu.as_wgpu();
    EXPECT_EQ(wgpu_descriptor.addressModeU, wgpu::AddressMode::eMirrorRepeat);
    EXPECT_EQ(wgpu_descriptor.addressModeV, wgpu::AddressMode::eMirrorRepeat);
    EXPECT_EQ(wgpu_descriptor.addressModeW, wgpu::AddressMode::eMirrorRepeat);
    EXPECT_EQ(wgpu_descriptor.magFilter, wgpu::FilterMode::eLinear);
    EXPECT_EQ(wgpu_descriptor.minFilter, wgpu::FilterMode::eLinear);
    EXPECT_EQ(wgpu_descriptor.mipmapFilter, wgpu::MipmapFilterMode::eLinear);
    EXPECT_FLOAT_EQ(wgpu_descriptor.lodMinClamp, 1.5f);
    EXPECT_FLOAT_EQ(wgpu_descriptor.lodMaxClamp, 9.5f);
    EXPECT_EQ(wgpu_descriptor.maxAnisotropy, 1u);
    EXPECT_EQ(wgpu_descriptor.compare, wgpu::CompareFunction::eLessEqual);
}

// ImageSampler carries its descriptor in the Descriptor alternative, exactly as
// Bevy's enum does (Bevy ImageSampler, image.rs:618-660).
TEST(ImageSampler, PayloadCarryingAlternatives) {
    epix::image::ImageSampler default_sampler;
    EXPECT_TRUE(default_sampler.is_default());
    EXPECT_EQ(default_sampler.descriptor(), nullptr);

    // linear()/nearest() are Descriptor alternatives with the matching filters.
    const auto linear = epix::image::ImageSampler::linear();
    ASSERT_NE(linear.descriptor(), nullptr);
    EXPECT_EQ(linear.descriptor()->min_filter, epix::image::ImageFilterMode::Linear);
    const auto nearest = epix::image::ImageSampler::nearest();
    ASSERT_NE(nearest.descriptor(), nullptr);
    EXPECT_EQ(nearest.descriptor()->mag_filter, epix::image::ImageFilterMode::Nearest);

    // get_or_init_descriptor promotes Default to Descriptor and returns the payload.
    auto& initialized = default_sampler.get_or_init_descriptor();
    EXPECT_FALSE(default_sampler.is_default());
    ASSERT_NE(default_sampler.descriptor(), nullptr);
    EXPECT_EQ(&initialized, default_sampler.descriptor());
    // A second call returns the same descriptor rather than resetting it.
    initialized.anisotropy_clamp = 4;
    EXPECT_EQ(default_sampler.get_or_init_descriptor().anisotropy_clamp, 4);
}

// Image::sampler defaults to the payloadless ImageSampler::Default alternative and
// switches to the descriptor-carrying alternative once a descriptor is set
// (Bevy Image::sampler, image.rs:599-624).
TEST(Image, SamplerAccessors) {
    auto image = epix::image::Image::create2d(4, 4, epix::image::Format::RGBA8);
    EXPECT_TRUE(image.sampler().is_default());
    EXPECT_EQ(image.sampler_descriptor(), nullptr);

    auto descriptor = epix::image::ImageSamplerDescriptor::linear();
    image.set_sampler_descriptor(descriptor);
    ASSERT_NE(image.sampler_descriptor(), nullptr);
    EXPECT_EQ(image.sampler_descriptor()->min_filter, epix::image::ImageFilterMode::Linear);

    // get_or_init on the image follows the same promotion rule.
    auto& mutated = image.get_or_init_sampler_descriptor();
    mutated.label = "image sampler";
    EXPECT_EQ(image.sampler_descriptor()->label, "image sampler");

    image.set_sampler(epix::image::ImageSampler{});
    EXPECT_TRUE(image.sampler().is_default());
    EXPECT_EQ(image.sampler_descriptor(), nullptr);
}

// Image keeps Bevy's Option<Vec<u8>> distinction when render-only pixel data
// is transferred: metadata stays in the source and an empty present payload
// remains distinguishable from an absent payload.
TEST(Image, TakeDataPreservesMetadataAndPresence) {
    auto source = epix::image::Image::create3d(2, 3, 4, epix::image::Format::RGBA8);
    source.set_usage(epix::assets::RenderAssetUsages::RENDER_WORLD);
    source.set_copy_on_resize(true);
    auto descriptor  = epix::image::ImageSamplerDescriptor::nearest();
    descriptor.label = "take-data sampler";
    source.set_sampler_descriptor(std::move(descriptor));
    ASSERT_TRUE(source.has_data());
    ASSERT_EQ(source.raw_view().size(), 2u * 3u * 4u * 4u);

    auto extracted = source.take_data();
    EXPECT_FALSE(source.has_data());
    EXPECT_TRUE(source.raw_view().empty());
    EXPECT_EQ(source.width(), 2u);
    EXPECT_EQ(source.height(), 3u);
    EXPECT_EQ(source.depth(), 4u);
    EXPECT_EQ(source.format(), epix::image::Format::RGBA8);
    EXPECT_EQ(source.usage(), epix::assets::RenderAssetUsages::RENDER_WORLD);
    EXPECT_TRUE(source.copy_on_resize());
    ASSERT_NE(source.sampler_descriptor(), nullptr);
    EXPECT_EQ(source.sampler_descriptor()->label, "take-data sampler");

    EXPECT_TRUE(extracted.has_data());
    EXPECT_EQ(extracted.raw_view().size(), 2u * 3u * 4u * 4u);
    EXPECT_EQ(extracted.width(), source.width());
    EXPECT_EQ(extracted.height(), source.height());
    EXPECT_EQ(extracted.depth(), source.depth());
    ASSERT_NE(extracted.sampler_descriptor(), nullptr);
    EXPECT_EQ(extracted.sampler_descriptor()->label, "take-data sampler");

    auto empty = epix::image::Image::create2d(0, 0, epix::image::Format::RGBA8);
    ASSERT_TRUE(empty.has_data());
    ASSERT_TRUE(empty.raw_view().empty());
    auto extracted_empty = empty.take_data();
    EXPECT_TRUE(extracted_empty.has_data());
    EXPECT_TRUE(extracted_empty.raw_view().empty());
    EXPECT_FALSE(empty.has_data());
}
