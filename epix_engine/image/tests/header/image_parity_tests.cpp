#include <gtest/gtest.h>
#include <epix/image.hpp>
#include <array>

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
}

// Image::sampler defaults to ImageSampler::Default and switches to Descriptor
// when a custom descriptor is set (Bevy Image::sampler, image.rs:599-624).
TEST(Image, SamplerAccessors) {
    auto image = epix::image::Image::create2d(4, 4, epix::image::Format::RGBA8);
    EXPECT_EQ(image.sampler(), epix::image::ImageSampler::Default);
    EXPECT_EQ(image.sampler_descriptor().min_filter, epix::image::ImageFilterMode::Nearest);

    auto descriptor = epix::image::ImageSamplerDescriptor::linear();
    image.set_sampler_descriptor(descriptor);
    EXPECT_EQ(image.sampler(), epix::image::ImageSampler::Descriptor);
    EXPECT_EQ(image.sampler_descriptor().min_filter, epix::image::ImageFilterMode::Linear);

    image.set_sampler(epix::image::ImageSampler::Default);
    EXPECT_EQ(image.sampler(), epix::image::ImageSampler::Default);
}
