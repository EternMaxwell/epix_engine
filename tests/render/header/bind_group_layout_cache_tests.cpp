#include <gtest/gtest.h>

#include <cstdint>
#include <epix/render.hpp>
#include <epix/render/bindless.hpp>
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

// A device that can hold a bindless layout at all. `make_vulkan_device` requests no
// features, so `hasFeature` is false on it by construction and a binding-array layout
// cannot exist there - which is a property of the request, not of the hardware.
std::optional<wgpu::Device> make_bindless_vulkan_device() {
    auto instance = wgpu::createInstance();
    if (!instance) return std::nullopt;
    auto adapter = instance.requestAdapter(wgpu::RequestAdapterOptions().setBackendType(wgpu::BackendType::eVulkan));
    if (!adapter) return std::nullopt;

    // The two features a bindless layout needs, requested explicitly: asking for every
    // adapter feature fails device creation, which is why the engine requests a curated
    // set too.
    const std::vector<wgpu::FeatureName> required_features{wgpu::FeatureName::eTextureBindingArray,
                                                           wgpu::FeatureName::eSampledTextureAndStorageBufferArrayNonUniformIndexing};

    // The WebGPU default limits, with only the binding-array room raised from the
    // adapter: the default cap of zero is what would reject a counted array, and the
    // adapter's raw limits are not all requestable verbatim.
    // As the engine's device does: the binding-array limits are wgpu-native extras on the
    // required-limits chain, and without them wgpu aborts on a counted array layout
    // instead of failing (its default is zero). 2048 covers the nine arrays of 64 below.
    wgpu::NativeLimits native_limits;
    native_limits.maxBindingArrayElementsPerShaderStage        = 2048;
    native_limits.maxBindingArraySamplerElementsPerShaderStage = 2048;
    wgpu::Limits limits;
    limits.setNextInChain(native_limits);

    auto device = adapter.requestDevice(
        wgpu::DeviceDescriptor{}.setRequiredFeatures(required_features).setRequiredLimits(limits));
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

// BND-1 — the bindless tables, conversions and ranges (`bindless.rs:43-53`,
// `:219-219`, `:308-374`). Values only, but kept beside BND-2 because the two are the
// same surface and the second needs the same vocabulary.
TEST(Bindless, BindingNumbersTypesAndRanges) {
    using render_resource::bindless_binding_number;
    using render_resource::bindless_resource_type_from;
    using render_resource::BindlessIndexRange;
    using render_resource::BindlessResourceType;
    using render_resource::BindlessSlabResourceLimit;

    // The table is what the shader's `bindless.slang` declarations match, so a wrong or
    // misordered entry binds an array the shader does not read.
    ASSERT_NE(bindless_binding_number(BindlessResourceType::Texture2d), nullptr);
    EXPECT_EQ(bindless_binding_number(BindlessResourceType::Texture2d)->number, 5u);
    EXPECT_EQ(bindless_binding_number(BindlessResourceType::SamplerComparison)->number, 3u);
    EXPECT_EQ(bindless_binding_number(BindlessResourceType::TextureCubeArray)->number, 9u);
    EXPECT_EQ(bindless_binding_number(BindlessResourceType::Buffer), nullptr)
        << "a buffer has its own binding number rather than a shared array";
    EXPECT_EQ(bindless_binding_number(BindlessResourceType::None), nullptr);

    EXPECT_EQ(bindless_resource_type_from(wgpu::TextureViewDimension::eCube), BindlessResourceType::TextureCube);
    EXPECT_EQ(bindless_resource_type_from(wgpu::SamplerBindingType::eNonFiltering),
              BindlessResourceType::SamplerNonFiltering);

    // Half-open, as a Rust range is: the end is one past the last index, which is the
    // difference between reading a table's last entry and reading past it.
    const BindlessIndexRange range{.start = {4}, .end = {7}};
    EXPECT_TRUE(range.contains({4}));
    EXPECT_TRUE(range.contains({6}));
    EXPECT_FALSE(range.contains({7}));
    EXPECT_EQ(range.length(), 3u);

    EXPECT_EQ(BindlessSlabResourceLimit{}.resolve(), render_resource::AUTO_BINDLESS_SLAB_RESOURCE_LIMIT);
    EXPECT_EQ((BindlessSlabResourceLimit{.custom = 64u}).resolve(), 64u);
}

// BND-2 — the shared bindless layout entries (`bindless.rs:227-306`), and whether a
// device will actually take them.
//
// The values can be checked without a device; usability cannot. Whether wgpu accepts a
// layout whose entries carry counts through the chained extras, and a storage buffer
// sized to the index table, is a question only a real device answers - the port's own
// builder notes that wgpu-native decides "declared as a single item" from those extras
// alone, so an entry that looks right on paper can still be a layout creation failure.
TEST(Bindless, SharedLayoutEntriesBuildARealBindGroupLayout) {
    const auto entries =
        render_resource::create_bindless_bind_group_layout_entries(/*bindless_index_table_length=*/8,
                                                                   /*bindless_slab_resource_limit=*/64,
                                                                   render_resource::BindingNumber{0});
    ASSERT_EQ(entries.size(), 10u) << "the index table plus the nine common binding arrays";
    EXPECT_EQ(entries[0].binding, 0u) << "the index table sits at the caller's binding number";

    // Each array's binding number has to be the one `BINDING_NUMBERS` records, because
    // the shader declares its arrays at those numbers: a layout that disagreed would
    // bind array 5 where the shader reads array 7, with nothing to report at either end.
    for (std::uint32_t binding = 1; binding <= 9; ++binding) {
        EXPECT_EQ(entries[binding].binding, binding);
        EXPECT_EQ(render_resource::BINDING_NUMBERS[binding - 1].second.number, binding);
    }

    // Every array is declared with the slab's resource limit as its count - the widest
    // it can need to be, since the limit is reached only when every bindless resource is
    // of one kind. The index table itself is a single item, sized in u32s.
    for (std::uint32_t binding = 1; binding <= 9; ++binding) {
        EXPECT_EQ(entries[binding].bindingArraySize, 64u) << "array at binding " << binding;
    }
    EXPECT_EQ(entries[0].bindingArraySize, 0u);
    EXPECT_EQ(entries[0].buffer.minBindingSize, 8u * sizeof(std::uint32_t));

    // A caller that moved its index table elsewhere, and one with a smaller slab.
    const auto moved =
        render_resource::create_bindless_bind_group_layout_entries(8, 64, render_resource::BindingNumber{3});
    ASSERT_EQ(moved.size(), 10u);
    EXPECT_EQ(moved[0].binding, 3u);
    const auto limited =
        render_resource::create_bindless_bind_group_layout_entries(4, 3, render_resource::BindingNumber{0});
    for (std::uint32_t binding = 1; binding <= 9; ++binding) {
        EXPECT_EQ(limited[binding].bindingArraySize, 3u) << "array at binding " << binding;
    }
    EXPECT_EQ(limited[0].buffer.minBindingSize, 4u * sizeof(std::uint32_t));

    // The part that needs the device and the server: these entries have to become a bind
    // group layout wgpu accepts, which is what makes the surface usable rather than
    // merely well-formed.
    // The part that needs the device and the server, which is the only way to know these
    // entries are usable rather than merely well-formed: wgpu decides from the chained
    // counts whether an entry is a binding array at all, and a counted layout on a device
    // that never asked for binding-array room aborts inside wgpu rather than reporting.
    // A counted-array layout is created and interned here, as the engine's device now can.
    auto device = make_bindless_vulkan_device();
    if (!device) GTEST_SKIP() << "no Vulkan device with binding arrays available";

    PipelineServer server{*device};
    const auto layout = server.get_bind_group_layout(BindGroupLayoutDescriptor::create("bindless_layout", entries));
    EXPECT_TRUE(layout) << "wgpu has to accept the entries, counts and index table included";
    const auto again = server.get_bind_group_layout(BindGroupLayoutDescriptor::create("bindless_layout", entries));
    EXPECT_TRUE(again);
    EXPECT_EQ(layout.id(), again.id()) << "and the server interns it like any other layout";
}
