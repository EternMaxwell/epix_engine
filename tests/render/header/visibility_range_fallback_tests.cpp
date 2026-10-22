#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>

#include <cstring>
#include <memory>
#include <vector>

namespace {
namespace render = epix::render;
namespace ecs = epix::ecs;

struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPoolBuilder{}.num_threads(4).build());
    }
} io_task_pool_init;

class VisibilityRangeFallback : public testing::TestWithParam<bool> {
   protected:
    epix::app::App app = epix::app::App::create();
    ecs::World* world = nullptr;

    void SetUp() override {
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{}, epix::camera::CameraPlugin{}, epix::assets::AssetPlugin{},
                        epix::image::ImagePlugin{}, epix::mesh::MeshPlugin{}, epix::shader::ShaderPlugin{},
                        render::FrameCountPlugin{});
        render::WgpuSettings settings;
        if (GetParam()) {
            auto constrained = settings.limits;
            constrained.maxStorageBuffersPerShaderStage = 0;
            settings.constrained_limits = constrained;
        }
        ASSERT_NO_THROW(app.add_plugins(render::RenderPlugin{
            .render_creation = render::RenderCreation::automatic(std::move(settings))}));
        world = &app.get_sub_app_mut(render::Render)->get().world_mut();
        render::view::RenderVisibilityRanges ranges;
        ranges.buffer = render::render_resource::BufferVec<glm::vec4>{wgpu::BufferUsage::eStorage |
            wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopySrc};
        world->insert_resource(std::move(ranges));
        wgpu::Limits limits{};
        world->resource<wgpu::Device>().getLimits(&limits);
        ASSERT_EQ(limits.maxStorageBuffersPerShaderStage < render::view::VISIBILITY_RANGES_STORAGE_BUFFER_COUNT, GetParam());
    }

    auto& ranges() { return world->resource_mut<render::view::RenderVisibilityRanges>(); }
    void write() {
        auto system = ecs::make_system_unique(render::view::detail::write_render_visibility_ranges);
        system->initialize(*world);
        ASSERT_TRUE(system->run({}, *world));
    }

    std::vector<glm::vec4> uploaded(std::size_t count) {
        const auto& device = world->resource<wgpu::Device>();
        const auto size = count * sizeof(glm::vec4);
        const auto staging = device.createBuffer(wgpu::BufferDescriptor{}.setSize(size)
            .setUsage(wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eMapRead));
        const auto encoder = device.createCommandEncoder();
        encoder.copyBufferToBuffer(*ranges().buffer.buffer(), 0, staging, 0, size);
        world->resource<wgpu::Queue>().submit(encoder.finish());
        struct Result { std::vector<glm::vec4> values; bool done = false; bool mapped = false; };
        const auto result = std::make_shared<Result>();
        result->values.resize(count);
        staging.mapAsync(wgpu::MapMode::eRead, 0, size, wgpu::BufferMapCallbackInfo{}
            .setMode(wgpu::CallbackMode::eAllowProcessEvents)
            .setCallback(wgpu::BufferMapCallback{[result, staging, size](wgpu::MapAsyncStatus status, wgpu::StringView) {
                result->done = true;
                if (status != wgpu::MapAsyncStatus::eSuccess) return;
                std::memcpy(result->values.data(), staging.getConstMappedRange(0, size), size);
                staging.unmap();
                result->mapped = true;
            }}));
        for (unsigned attempt = 0; attempt < 1000 && !result->done; ++attempt) device.poll(wgpu::Bool{true});
        EXPECT_TRUE(result->mapped);
        return result->values;
    }
};

TEST_P(VisibilityRangeFallback, EmptyDirtyBufferUsesNativeMinimumExtent) {
    write();
    const auto count = GetParam() ? render::view::VISIBILITY_RANGE_UNIFORM_BUFFER_SIZE : 1u;
    ASSERT_EQ(ranges().buffer.len(), count);
    ASSERT_NE(ranges().buffer.buffer(), nullptr);
    EXPECT_EQ(ranges().buffer.buffer()->getSize(), count * sizeof(glm::vec4));
    for (const auto& value : uploaded(count)) EXPECT_EQ(value, glm::vec4(0.0f));
    EXPECT_FALSE(ranges().buffer_dirty);
}

TEST_P(VisibilityRangeFallback, ShortBufferPreservesPrefixAndZeroPadsOnlyUniformPath) {
    for (unsigned i = 0; i < 3; ++i) ranges().buffer.push(glm::vec4(static_cast<float>(i + 1)));
    write();
    const auto count = GetParam() ? render::view::VISIBILITY_RANGE_UNIFORM_BUFFER_SIZE : 3u;
    ASSERT_EQ(ranges().buffer.len(), count);
    ASSERT_NE(ranges().buffer.buffer(), nullptr);
    const auto values = uploaded(count);
    for (unsigned i = 0; i < 3; ++i) EXPECT_EQ(values[i], glm::vec4(static_cast<float>(i + 1)));
    for (std::size_t i = 3; i < values.size(); ++i) EXPECT_EQ(values[i], glm::vec4(0.0f));
}

TEST_P(VisibilityRangeFallback, OversizedBufferTruncatesOnlyUniformPath) {
    for (unsigned i = 0; i < 80; ++i) ranges().buffer.push(glm::vec4(static_cast<float>(i + 1)));
    write();
    const auto count = GetParam() ? render::view::VISIBILITY_RANGE_UNIFORM_BUFFER_SIZE : 80u;
    ASSERT_EQ(ranges().buffer.len(), count);
    ASSERT_NE(ranges().buffer.buffer(), nullptr);
    const auto values = uploaded(count);
    for (std::size_t i = 0; i < values.size(); ++i) EXPECT_EQ(values[i], glm::vec4(static_cast<float>(i + 1)));
}

TEST_P(VisibilityRangeFallback, CleanBufferDoesNotResizeOrAllocate) {
    ranges().buffer.push(glm::vec4(7.0f));
    ranges().buffer_dirty = false;
    write();
    EXPECT_EQ(ranges().buffer.len(), 1u);
    EXPECT_EQ(ranges().buffer.buffer(), nullptr);
}

INSTANTIATE_TEST_SUITE_P(ActualDevice, VisibilityRangeFallback, testing::Values(false, true),
    [](const testing::TestParamInfo<bool>& value) { return value.param ? "Uniform" : "Storage"; });
}  // namespace
