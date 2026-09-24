#include <gtest/gtest.h>

#include <cstdint>
#include <epix/render.hpp>
#include <vector>
#include <webgpu/webgpu.hpp>

using namespace epix::render;

namespace {

std::vector<wgpu::BindGroupLayoutEntry> uniform_entries(bool dynamic_offset, std::uint64_t min_binding_size) {
    return {wgpu::BindGroupLayoutEntry()
                .setBinding(0)
                .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
                .setBuffer(wgpu::BufferBindingLayout()
                               .setType(wgpu::BufferBindingType::eUniform)
                               .setHasDynamicOffset(wgpu::Bool(dynamic_offset))
                               .setMinBindingSize(min_binding_size))};
}

std::optional<wgpu::Device> make_vulkan_device() {
    auto instance = wgpu::createInstance();
    if (!instance) return std::nullopt;
    auto adapter = instance.requestAdapter(wgpu::RequestAdapterOptions().setBackendType(wgpu::BackendType::eVulkan));
    if (!adapter) return std::nullopt;
    auto device = adapter.requestDevice(wgpu::DeviceDescriptor{});
    if (!device) return std::nullopt;
    return device;
}

}  // namespace

// Bevy's builder carries a binding-array count (`count(NonZeroU32)`), and `build`
// puts it on the entry's binding-array size
// (render_resource/bind_group_layout_entries.rs:130-155). No count means the binding
// is not an array.
TEST(BindGroupLayoutEntryBuilder, CarriesTheBindingArrayCount) {
    using render_resource::BindGroupLayoutEntries;
    using render_resource::binding_types::sampler;
    using render_resource::binding_types::texture_2d;

    const auto plain = texture_2d(wgpu::TextureSampleType::eFloat).build(4u, wgpu::ShaderStage::eFragment);
    EXPECT_EQ(plain.binding, 4u);
    EXPECT_EQ(plain.bindingArraySize, 0u);

    const auto array = texture_2d(wgpu::TextureSampleType::eFloat).count(4).build(5u, wgpu::ShaderStage::eFragment);
    EXPECT_EQ(array.binding, 5u);
    EXPECT_EQ(array.bindingArraySize, 4u);
    EXPECT_EQ(array.texture.sampleType, wgpu::TextureSampleType::eFloat);

    // Visibility and count chain in either order, and a collection with its own default
    // visibility only fills the gap the entry leaves.
    const auto chained = BindGroupLayoutEntries<>::single(
        wgpu::ShaderStage::eVertex,
        texture_2d(wgpu::TextureSampleType::eFloat).visibility(wgpu::ShaderStage::eFragment).count(8u));
    EXPECT_EQ(chained[0].visibility, wgpu::ShaderStage::eFragment);
    EXPECT_EQ(chained[0].bindingArraySize, 8u);
    const auto inherited = BindGroupLayoutEntries<>::single(wgpu::ShaderStage::eVertex,
                                                            sampler(wgpu::SamplerBindingType::eFiltering).count(3u));
    EXPECT_EQ(inherited[0].visibility, wgpu::ShaderStage::eVertex) << "the count does not disturb visibility";
    EXPECT_EQ(inherited[0].bindingArraySize, 3u);
}

// Bevy derives PartialEq/Eq/Hash over the label and the entries
// (render_resource/pipeline.rs:90-104), which is what lets its cache intern
// layouts by content instead of by identity.
TEST(BindGroupLayoutDescriptor, EqualityAndHashFollowLabelAndEntries) {
    const auto base            = BindGroupLayoutDescriptor::create("layout_a", uniform_entries(false, 64));
    const auto same            = BindGroupLayoutDescriptor::create("layout_a", uniform_entries(false, 64));
    const auto other_label     = BindGroupLayoutDescriptor::create("layout_b", uniform_entries(false, 64));
    const auto other_entry     = BindGroupLayoutDescriptor::create("layout_a", uniform_entries(true, 64));
    const auto other_size      = BindGroupLayoutDescriptor::create("layout_a", uniform_entries(false, 128));

    EXPECT_TRUE(base == same);
    EXPECT_EQ(base.hash(), same.hash());
    EXPECT_FALSE(base == other_label);
    EXPECT_FALSE(base == other_entry);
    EXPECT_FALSE(base == other_size);
}

TEST(BindGroupLayoutCache, InternsEqualDescriptorsToTheSameNativeLayout) {
    auto device = make_vulkan_device();
    if (!device) GTEST_SKIP() << "no Vulkan device available";

    BindGroupLayoutCache cache;
    const auto first =
        cache.get(*device, BindGroupLayoutDescriptor::create("layout_a", uniform_entries(false, 64)));
    const auto second =
        cache.get(*device, BindGroupLayoutDescriptor::create("layout_a", uniform_entries(false, 64)));
    EXPECT_EQ(first.id(), second.id());
    EXPECT_EQ(cache.size(), 1u);

    const auto different =
        cache.get(*device, BindGroupLayoutDescriptor::create("layout_a", uniform_entries(true, 64)));
    EXPECT_NE(first.id(), different.id());
    EXPECT_EQ(cache.size(), 2u);
}

// The public entry point every pass uses to build bind groups in Bevy is
// `PipelineCache::get_bind_group_layout`.
TEST(PipelineServer, GetBindGroupLayoutInternsThroughTheServer) {
    auto device = make_vulkan_device();
    if (!device) GTEST_SKIP() << "no Vulkan device available";

    PipelineServer server{*device};
    const auto descriptor = BindGroupLayoutDescriptor::create("layout_a", uniform_entries(false, 64));
    const auto first      = server.get_bind_group_layout(descriptor);
    const auto second     = server.get_bind_group_layout(descriptor);
    EXPECT_TRUE(first);
    EXPECT_EQ(first.id(), second.id());
}
