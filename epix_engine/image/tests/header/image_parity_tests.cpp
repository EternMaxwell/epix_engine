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

// Image size accessors and the width/height ratio (Bevy Image::size,
// size_f32, aspect_ratio; image.rs:1198-1216).
TEST(Image, SizeAccessorsMatchBevy) {
    auto image = epix::image::Image::create2d(8, 4, epix::image::Format::RGBA8);
    EXPECT_EQ(image.size(), glm::uvec2(8u, 4u));
    EXPECT_EQ(image.size_f32(), glm::vec2(8.0f, 4.0f));
    EXPECT_FLOAT_EQ(image.aspect_ratio(), 2.0f);

    // Bevy panics on a zero dimension; Epix reports the same contract as an exception.
    auto degenerate = epix::image::Image::create2d(0, 4, epix::image::Format::RGBA8);
    EXPECT_THROW(degenerate.aspect_ratio(), std::invalid_argument);
}

// pixel_data_offset is row-major within each layer and rejects out-of-range
// coordinates or formats with no known pixel size (Bevy Image::pixel_data_offset,
// image.rs:1431).
TEST(Image, PixelDataOffsetIsRowMajorPerLayer) {
    auto image                  = epix::image::Image::create3d(4, 3, 2, epix::image::Format::RGBA8);
    constexpr std::size_t pixel = 4;
    EXPECT_EQ(image.pixel_data_offset({0u, 0u, 0u}).value(), 0u);
    EXPECT_EQ(image.pixel_data_offset({3u, 0u, 0u}).value(), 3u * pixel);
    EXPECT_EQ(image.pixel_data_offset({0u, 1u, 0u}).value(), 4u * pixel);
    EXPECT_EQ(image.pixel_data_offset({0u, 0u, 1u}).value(), 4u * 3u * pixel);

    EXPECT_FALSE(image.pixel_data_offset({4u, 0u, 0u}).has_value());
    EXPECT_FALSE(image.pixel_data_offset({0u, 3u, 0u}).has_value());
    EXPECT_FALSE(image.pixel_data_offset({0u, 0u, 2u}).has_value());
}

// A grey image carries one byte per pixel, which halves the stride.
TEST(Image, PixelDataOffsetRespectsGreyFormat) {
    auto image = epix::image::Image::create2d(8, 2, epix::image::Format::Grey8);
    EXPECT_EQ(image.pixel_data_offset({1u, 0u, 0u}).value(), 1u);
    EXPECT_EQ(image.pixel_data_offset({0u, 1u, 0u}).value(), 8u);
}

// Image::is_compressed reports block-compressed formats (Bevy Image::is_compressed,
// image.rs:1412).
TEST(Image, IsCompressedMatchesBevy) {
    EXPECT_FALSE(epix::image::Image::create2d(2, 2, epix::image::Format::RGBA8).is_compressed());
    auto compressed                      = epix::image::Image::create2d(2, 2, epix::image::Format::RGBA8);
    compressed.texture_descriptor.format = wgpu::TextureFormat::eBC7RGBAUnorm;
    EXPECT_TRUE(compressed.is_compressed());
    compressed.texture_descriptor.format = wgpu::TextureFormat::eETC2RGBA8Unorm;
    EXPECT_TRUE(compressed.is_compressed());
}

// resize replaces the size and zero-extends or truncates the payload without
// scaling (Bevy Image::resize, image.rs:1222).
TEST(Image, ResizeReplacesSizeAndReallocsData) {
    auto image = epix::image::Image::create2d(2, 2, epix::image::Format::RGBA8);
    ASSERT_EQ(image.raw_view().size(), 2u * 2u * 4u);
    // A written pixel survives a grow unchanged, because resize never scales.
    ASSERT_TRUE(image.write(1, 1, 0, std::array{1.0f, 0.5f, 0.25f, 1.0f}).has_value());
    const auto before = image.raw_view()[static_cast<std::size_t>((1 * 2 + 1) * 4)];
    EXPECT_EQ(before, std::byte(255));

    image.resize_size(wgpu::Extent3D{4, 3, 1});
    EXPECT_EQ(image.width(), 4u);
    EXPECT_EQ(image.height(), 3u);
    EXPECT_EQ(image.raw_view().size(), 4u * 3u * 4u);
    // The existing bytes stay at their original offsets in the grown buffer.
    EXPECT_EQ(image.raw_view()[static_cast<std::size_t>((1 * 2 + 1) * 4)], before);

    image.resize_size(wgpu::Extent3D{1, 1, 1});
    EXPECT_EQ(image.width(), 1u);
    EXPECT_EQ(image.raw_view().size(), 4u);
}

// resize_in_place keeps pixel data anchored at the top-left and fills growth with
// zeroes (Bevy Image::resize_in_place, image.rs:1252).
TEST(Image, ResizeInPlaceKeepsTopLeftPixels) {
    // Grey8 has a one-byte stride, which makes the row arithmetic easy to assert.
    auto image = epix::image::Image::create2d(2, 2, epix::image::Format::Grey8);
    ASSERT_TRUE(image.write(0, 0, std::array{1.0f}).has_value());
    ASSERT_TRUE(image.write(1, 0, std::array{0.5f}).has_value());
    ASSERT_TRUE(image.write(0, 1, std::array{0.25f}).has_value());

    image.resize_in_place(wgpu::Extent3D{3, 3, 1});
    EXPECT_EQ(image.width(), 3u);
    EXPECT_EQ(image.height(), 3u);
    ASSERT_EQ(image.raw_view().size(), 9u);
    // The three written pixels keep their relative brightness at their original
    // top-left offsets; the exact byte value depends on the format's rounding.
    EXPECT_GT(image.raw_view()[0], image.raw_view()[1]);
    EXPECT_GT(image.raw_view()[1], image.raw_view()[3]);
    // The grown region is zero-filled.
    EXPECT_EQ(image.raw_view()[2], std::byte(0));
    EXPECT_EQ(image.raw_view()[4], std::byte(0));

    // Shrinking clips the image instead of scaling it.
    const auto first_pixel  = image.raw_view()[0];
    const auto second_pixel = image.raw_view()[1];
    image.resize_in_place(wgpu::Extent3D{2, 1, 1});
    EXPECT_EQ(image.raw_view().size(), 2u);
    EXPECT_EQ(image.raw_view()[0], first_pixel);
    EXPECT_EQ(image.raw_view()[1], second_pixel);
}

// A resize without a CPU payload defers to the GPU copy path, exactly as Bevy
// does (Bevy Image::resize_in_place, image.rs:1258-1261).
TEST(Image, ResizeInPlaceWithoutDataEnablesCopyOnResize) {
    auto image = epix::image::Image::create2d(4, 4, epix::image::Format::RGBA8);
    image.set_copy_on_resize(false);
    auto extracted = image.take_data();
    ASSERT_FALSE(image.has_data());

    image.resize_in_place(wgpu::Extent3D{8, 8, 1});
    EXPECT_EQ(image.width(), 8u);
    EXPECT_TRUE(image.copy_on_resize());
}

// reinterpret_size only succeeds when the total pixel count is unchanged
// (Bevy Image::reinterpret_size, image.rs:1233).
TEST(Image, ReinterpretSizeRequiresEqualVolume) {
    auto image = epix::image::Image::create2d(4, 4, epix::image::Format::RGBA8);

    const auto same_volume = image.reinterpret_size(wgpu::Extent3D{2, 8, 1});
    ASSERT_TRUE(same_volume.has_value());
    EXPECT_EQ(image.width(), 2u);
    EXPECT_EQ(image.height(), 8u);

    const auto mismatched = image.reinterpret_size(wgpu::Extent3D{9, 9, 1});
    ASSERT_FALSE(mismatched.has_value());
    const auto* incompatible =
        std::get_if<epix::image::TextureReinterpretationError::IncompatibleSizes>(&mismatched.error().value);
    ASSERT_NE(incompatible, nullptr);
    EXPECT_EQ(incompatible->old.width, 2u);
    EXPECT_EQ(incompatible->old.height, 8u);
    EXPECT_EQ(incompatible->new_size.width, 9u);
    // A failed reinterpretation leaves the size untouched.
    EXPECT_EQ(image.width(), 2u);
    EXPECT_EQ(image.height(), 8u);
}

// reinterpret_stacked_2d_as_array splits a stacked 2D image into array layers and
// reports Bevy's typed errors otherwise (Bevy
// Image::reinterpret_stacked_2d_as_array, image.rs:1300).
TEST(Image, ReinterpretStacked2dAsArrayMatchesBevyErrors) {
    auto stacked     = epix::image::Image::create2d(4, 8, epix::image::Format::RGBA8);
    const auto split = stacked.reinterpret_stacked_2d_as_array(4);
    ASSERT_TRUE(split.has_value());
    EXPECT_EQ(stacked.width(), 4u);
    EXPECT_EQ(stacked.height(), 2u);
    EXPECT_EQ(stacked.layers(), 4u);
    EXPECT_EQ(stacked.type(), epix::image::ImageType::e2DArray);

    // A height that is not divisible by the layer count is rejected.
    auto uneven              = epix::image::Image::create2d(4, 6, epix::image::Format::RGBA8);
    const auto uneven_result = uneven.reinterpret_stacked_2d_as_array(4);
    ASSERT_FALSE(uneven_result.has_value());
    const auto* not_divisible = std::get_if<epix::image::TextureReinterpretationError::HeightNotDivisibleByLayers>(
        &uneven_result.error().value);
    ASSERT_NE(not_divisible, nullptr);
    EXPECT_EQ(not_divisible->height, 6u);
    EXPECT_EQ(not_divisible->layer_count, 4u);

    // A non-2D image is rejected with WrongDimension.
    auto volumetric            = epix::image::Image::create3d(2, 2, 2, epix::image::Format::RGBA8);
    const auto wrong_dimension = volumetric.reinterpret_stacked_2d_as_array(2);
    ASSERT_FALSE(wrong_dimension.has_value());
    EXPECT_NE(std::get_if<epix::image::TextureReinterpretationError::WrongDimension>(&wrong_dimension.error().value),
              nullptr);

    // An image that already has layers is rejected with InvalidLayerCount.
    const auto already_layered = stacked.reinterpret_stacked_2d_as_array(2);
    ASSERT_FALSE(already_layered.has_value());
    EXPECT_NE(std::get_if<epix::image::TextureReinterpretationError::InvalidLayerCount>(&already_layered.error().value),
              nullptr);
}
