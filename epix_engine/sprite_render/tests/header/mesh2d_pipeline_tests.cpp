#include <gtest/gtest.h>

#include <cstdint>
#include <epix/sprite_render/mesh2d_pipeline.hpp>
#include <webgpu/webgpu.hpp>

using namespace epix::sprite_render;
namespace render_resource = epix::render::render_resource;

// Bevy's `GpuArrayBuffer::<T>::batch_size` (mesh.rs:322-324) only produces a
// batch size for the dynamic-uniform fallback used when the adapter has no
// storage buffers; the storage-buffer path has no batch size at all.
TEST(Mesh2dPipeline, PerObjectBufferBatchSizeFollowsStorageBufferSupport) {
    wgpu::Limits with_storage{};
    with_storage.maxStorageBuffersPerShaderStage = 8;
    EXPECT_FALSE(mesh2d_per_object_buffer_batch_size(with_storage).has_value());

    wgpu::Limits without_storage{};
    without_storage.maxStorageBuffersPerShaderStage = 0;
    without_storage.maxUniformBufferBindingSize     = 65536;
    const auto batch                                = mesh2d_per_object_buffer_batch_size(without_storage);
    ASSERT_TRUE(batch.has_value());
    EXPECT_EQ(*batch, render_resource::BatchedUniformBuffer<Mesh2dUniform>::batch_size(without_storage));
    // The fallback packs as many 96-byte elements as the binding allows.
    EXPECT_EQ(*batch, 65536u / sizeof(Mesh2dUniform));
}

// `init_mesh_2d_pipeline` creates both layouts against the render device; the
// layout contents are exercised for real in step 5, when the draw commands bind
// the view and mesh groups.
TEST(Mesh2dPipeline, CreatesBothBindGroupLayoutsOnAGpuDevice) {
    auto instance = wgpu::createInstance();
    if (!instance) GTEST_SKIP() << "no WebGPU instance available";
    auto adapter = instance.requestAdapter(wgpu::RequestAdapterOptions().setBackendType(wgpu::BackendType::eVulkan));
    if (!adapter) GTEST_SKIP() << "no Vulkan adapter available";
    auto device = adapter.requestDevice(wgpu::DeviceDescriptor{});
    if (!device) GTEST_SKIP() << "no WebGPU device available";

    const auto view_layout = create_mesh2d_view_layout(device);
    const auto mesh_layout = create_mesh2d_mesh_layout(device);
    EXPECT_TRUE(view_layout);
    EXPECT_TRUE(mesh_layout);

    // The mesh layout follows the adapter's storage-buffer capability, which is
    // what `per_object_buffer_batch_size` reports on.
    wgpu::Limits limits{};
    device.getLimits(&limits);
    EXPECT_EQ(mesh2d_per_object_buffer_batch_size(limits).has_value(),
              limits.maxStorageBuffersPerShaderStage == 0);
}
