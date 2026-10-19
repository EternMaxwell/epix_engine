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
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <thread>

namespace {
namespace render = epix::render;
namespace assets = epix::assets;

struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} io_task_pool_init;

TEST(MathsGpu, ImportedLibraryMatchesBevyMatrixAndScalarArithmetic) {
    auto app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{}, epix::camera::CameraPlugin{}, assets::AssetPlugin{},
                    epix::image::ImagePlugin{}, epix::mesh::MeshPlugin{}, epix::transform::TransformPlugin{},
                    epix::shader::ShaderPlugin{}, render::FrameCountPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    static constexpr std::string_view source = R"slang(
import epix.maths;
using namespace epix::maths;
struct Inputs {
    float4x4 matrix;
    float4 affine_rows[3];
    float4 normal_packed[2];
    float4 affine_columns[4];
    float4 affine2_columns[3];
    float4 position;
    float4 normal;
    float4 basis;
    float4 extras;
    float4 color;
    float4 ray;
};
[[vk::binding(0, 0)]] ConstantBuffer<Inputs> inputs;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> results;
[shader("compute")]
[numthreads(1, 1, 1)]
void main() {
    float3x4 packed = float3x4(inputs.affine_rows[0], inputs.affine_rows[1], inputs.affine_rows[2]);
    float4x3 affine = float4x3(inputs.affine_columns[0].xyz, inputs.affine_columns[1].xyz,
                             inputs.affine_columns[2].xyz, inputs.affine_columns[3].xyz);
    float4x3 inverse_affine = inverse_affine3(affine);
    float3x3 linear = float3x3(inputs.matrix[0].xyz, inputs.matrix[1].xyz, inputs.matrix[2].xyz);
    results[0] = mul(affine3_to_square(packed), inputs.position);
    results[1] = float4(mul(mat2x4_f32_to_mat3x3_unpack(
        float2x4(inputs.normal_packed[0], inputs.normal_packed[1]), inputs.extras.x), inputs.normal.xyz), 0);
    results[2] = float4(mul(affine3_to_mat3x3(affine), inputs.normal.xyz), 0);
    results[3] = float4(inverse_affine[0] * inputs.position.x + inverse_affine[1] * inputs.position.y +
        inverse_affine[2] * inputs.position.z + inverse_affine[3] * inputs.position.w, inputs.position.w);
    results[4] = float4(mul(inverse_mat3x3(linear), inputs.normal.xyz), 0);
    results[5] = float4(mul(mat4x4_to_mat3x3(inputs.matrix), inputs.normal.xyz), 0);
    results[6] = float4(mul(orthonormalize(inputs.basis.xyz), inputs.normal.xyz), 0);
    results[7] = float4(mul(affine2_to_square(float3x2(inputs.affine2_columns[0].xy,
        inputs.affine2_columns[1].xy, inputs.affine2_columns[2].xy)), float3(inputs.position.xy, 1)), 1);
    results[8] = float4(fast_acos(inputs.extras.y), fast_acos_4(inputs.extras.y),
        fast_atan2(inputs.extras.z, inputs.extras.y), copysign(inputs.normal.x, inputs.extras.z));
    results[9] = float4(powsafe(inputs.color.xyz, inputs.extras.w), 0);
    results[10] = float4(project_onto(inputs.normal.xyz, inputs.basis.xyz), 0);
    results[11] = float4(ray_sphere_intersect(inputs.ray.x, inputs.ray.y, inputs.ray.z),
        float(sphere_intersects_plane_half_space(inputs.normal, inputs.position, inputs.ray.w)), 0);
    results[12] = float4(copysign(inputs.normal.x, inputs.color.w),
        fast_atan2(inputs.extras.z, -inputs.extras.y), fast_atan2(-inputs.extras.z, inputs.extras.y),
        fast_atan2(-inputs.extras.z, -inputs.extras.y));
}
)slang";
    app.resource_mut<assets::EmbeddedAssetRegistry>().insert_asset_static(
        "tests/render/maths_oracle.slang",
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(source.data()), source.size()});
    const auto& asset_server = app.resource<assets::AssetServer>();
    const auto shader = asset_server.load<epix::shader::Shader>("embedded://tests/render/maths_oracle.slang");
    const auto maths = asset_server.load<epix::shader::Shader>("embedded://epix/shaders/maths.slang");
    const auto pump = [&] {
        app.run_schedule(epix::app::PreUpdate);
        app.run_schedule(epix::app::PostUpdate);
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    };
    const auto loaded = [&] { return asset_server.is_loaded_with_dependencies(shader.id()) &&
                                     asset_server.is_loaded_with_dependencies(maths.id()); };
    for (int attempt = 0; attempt < 10000 && !loaded(); ++attempt) pump();
    ASSERT_TRUE(loaded());
    auto& world = app.get_sub_app_mut(render::Render)->get().world_mut();
    const auto& device = world.resource<wgpu::Device>();
    const auto& queue = world.resource<wgpu::Queue>();
    auto& server = app.resource<render::PipelineServer>();
    struct Inputs {
        glm::mat4 matrix{1.0f};
        std::array<glm::vec4, 3> affine_rows{};
        std::array<glm::vec4, 2> normal_packed{};
        std::array<glm::vec4, 4> affine_columns{};
        std::array<glm::vec4, 3> affine2_columns{{{1.2f, 0.4f, 0, 0}, {-0.3f, 0.8f, 0, 0}, {2, -1, 0, 0}}};
        glm::vec4 position{0.25f, -0.5f, -2, 1};
        glm::vec4 normal{0.3f, 0.7f, 0.2f, 0.5f};
        glm::vec4 basis{};
        glm::vec4 extras{};
        glm::vec4 color{-0.2f, 0.7f, -1.2f, -0.0f};
        glm::vec4 ray{3, -1, 1, 0.5f};
    };
    static_assert(sizeof(Inputs) == 352);
    using Values = std::array<glm::vec4, 13>;
    const auto input_buffer = device.createBuffer(wgpu::BufferDescriptor().setSize(sizeof(Inputs))
        .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst));
    const auto output = device.createBuffer(wgpu::BufferDescriptor().setSize(sizeof(Values))
        .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc));
    const auto staging = device.createBuffer(wgpu::BufferDescriptor().setSize(sizeof(Values))
        .setUsage(wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eMapRead));
    const auto entry = [](std::uint32_t binding, wgpu::BufferBindingType type, std::uint64_t size) {
        return wgpu::BindGroupLayoutEntry().setBinding(binding).setVisibility(wgpu::ShaderStage::eCompute)
            .setBuffer(wgpu::BufferBindingLayout().setType(type).setMinBindingSize(size));
    };
    const auto layout = render::BindGroupLayoutDescriptor::create("maths arithmetic buffers",
        std::array{entry(0, wgpu::BufferBindingType::eUniform, sizeof(Inputs)),
                   entry(1, wgpu::BufferBindingType::eStorage, sizeof(Values))});
    const auto id = server.queue_compute_pipeline(render::ComputePipelineDescriptor{
        .label = "shared maths arithmetic oracle", .layouts = {layout}, .shader = shader, .entry_point = "main"});
    for (int attempt = 0; attempt < 10000 && !server.get_compute_pipeline(id); ++attempt) pump();
    const auto pipeline = server.get_compute_pipeline(id);
    ASSERT_TRUE(pipeline);
    const auto group = device.createBindGroup(wgpu::BindGroupDescriptor()
        .setLayout(server.get_bind_group_layout(layout))
        .setEntries(std::array{wgpu::BindGroupEntry().setBinding(0).setBuffer(input_buffer).setSize(sizeof(Inputs)),
                               wgpu::BindGroupEntry().setBinding(1).setBuffer(output).setSize(sizeof(Values))}));
    const auto atan_reference = [](float y, float x) {
        const float ratio = std::min(std::abs(x), std::abs(y)) / std::max(std::abs(x), std::abs(y));
        const float square = ratio * ratio;
        float value = ((0.0872929f * square - 0.301895f) * square + 1.0f) * ratio;
        if (std::abs(y) > std::abs(x)) value = std::numbers::pi_v<float> * 0.5f - value;
        if (x < 0) value = std::numbers::pi_v<float> - value;
        return y > 0 ? value : -value;
    };
    for (int variant = 0; variant < 6; ++variant) {
        SCOPED_TRACE(variant);
        Inputs inputs;
        inputs.matrix = glm::translate(glm::mat4{1.0f}, glm::vec3{3, -2, 5}) *
            glm::rotate(glm::mat4{1.0f}, 0.45f, glm::normalize(glm::vec3{1, 2, 3})) *
            glm::scale(glm::mat4{1.0f}, glm::vec3{variant == 1 ? -1.7f : 1.7f, 0.6f, 2.3f});
        const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3{inputs.matrix}));
        for (int row = 0; row < 3; ++row)
            inputs.affine_rows[row] = {inputs.matrix[0][row], inputs.matrix[1][row],
                                        inputs.matrix[2][row], inputs.matrix[3][row]};
        for (int col = 0; col < 4; ++col) inputs.affine_columns[col] = inputs.matrix[col];
        inputs.normal_packed = {glm::vec4{normal_matrix[0], normal_matrix[1].x},
                                 glm::vec4{normal_matrix[1].y, normal_matrix[1].z,
                                            normal_matrix[2].x, normal_matrix[2].y}};
        inputs.basis = glm::vec4{glm::normalize(glm::vec3{0.3f, -0.4f, variant == 1 ? -0.5f : 0.5f}), 0};
        inputs.extras = {normal_matrix[2].z, variant == 0 ? 0.4f : (variant == 1 ? -0.6f : 0.0f),
                         variant == 2 ? -0.7f : 0.9f, 1.7f};
        if (variant == 3 || variant == 4) {
            inputs.extras.y = variant == 3 ? 1.0f : -1.0f;
            inputs.extras.z = 0.0f;
        }
        if (variant == 5) {
            inputs.basis = {0.6f, 0.8f, -0.0f, 0};
            inputs.extras.z = 0.7f;
        }
        if (variant == 2) inputs.ray = {3, 0, 1, 0.5f};  // No sphere intersection.
        queue.writeBuffer(input_buffer, 0, &inputs, sizeof(inputs));
        const auto encoder = device.createCommandEncoder();
        const auto pass = encoder.beginComputePass();
        pass.setPipeline(pipeline->get().pipeline());
        pass.setBindGroup(0, group, std::span<const std::uint32_t>{});
        pass.dispatchWorkgroups(1, 1, 1);
        pass.end();
        encoder.copyBufferToBuffer(output, 0, staging, 0, sizeof(Values));
        queue.submit(encoder.finish());
        struct Result { bool completed = false; bool mapped = false; Values values{}; };
        const auto result = std::make_shared<Result>();
        staging.mapAsync(wgpu::MapMode::eRead, 0, sizeof(Values), wgpu::BufferMapCallbackInfo()
            .setMode(wgpu::CallbackMode::eAllowProcessEvents)
            .setCallback(wgpu::BufferMapCallback([result, staging](wgpu::MapAsyncStatus status, wgpu::StringView) {
                result->completed = true;
                if (status != wgpu::MapAsyncStatus::eSuccess) return;
                std::memcpy(result->values.data(), staging.getConstMappedRange(0, sizeof(Values)), sizeof(Values));
                staging.unmap();
                result->mapped = true;
            })));
        for (int attempt = 0; attempt < 1000 && !result->completed; ++attempt) device.poll(wgpu::Bool(true));
        ASSERT_TRUE(result->mapped);
        const glm::vec3 n{inputs.normal}, z{inputs.basis};
        const float sign = std::copysign(1.0f, z.z);
        const float a = -1.0f / (sign + z.z), b = z.x * z.y * a;
        const glm::vec3 bx{1 + sign * z.x * z.x * a, sign * b, -sign * z.x};
        const glm::vec3 by{b, sign + z.y * z.y * a, -z.y};
        const float x = inputs.extras.y, abs_x = std::abs(x), y = inputs.extras.z;
        const float acos_base = (-0.156583f * abs_x + std::numbers::pi_v<float> * 0.5f) * std::sqrt(1 - abs_x);
        const float acos4_base = (((-0.0187293f * abs_x + 0.0742610f) * abs_x - 0.2121144f) * abs_x + 1.5707288f) *
                                 std::sqrt(1 - abs_x);
        const auto inverse_position = glm::inverse(inputs.matrix) * inputs.position;
        glm::vec2 intersections{-1.0f};
        const float discriminant = inputs.ray.x * inputs.ray.x * (inputs.ray.y * inputs.ray.y - 1) +
                                     inputs.ray.z * inputs.ray.z;
        if (discriminant >= 0) intersections = {-inputs.ray.x * inputs.ray.y - std::sqrt(discriminant),
                                                  -inputs.ray.x * inputs.ray.y + std::sqrt(discriminant)};
        const Values expected{
            inputs.matrix * inputs.position, glm::vec4{normal_matrix * n, 0},
            glm::vec4{glm::mat3{inputs.matrix} * n, 0}, glm::vec4{glm::vec3{inverse_position}, inputs.position.w},
            glm::vec4{glm::inverse(glm::mat3{inputs.matrix}) * n, 0}, glm::vec4{glm::mat3{inputs.matrix} * n, 0},
            glm::vec4{bx * n.x + by * n.y + z * n.z, 0},
            glm::vec4{1.2f * inputs.position.x - 0.3f * inputs.position.y + 2,
                       0.4f * inputs.position.x + 0.8f * inputs.position.y - 1, 1, 1},
            glm::vec4{x >= 0 ? acos_base : std::numbers::pi_v<float> - acos_base,
                       x >= 0 ? acos4_base : std::numbers::pi_v<float> - acos4_base,
                       atan_reference(y, x), std::copysign(n.x, y)},
            glm::vec4{-std::pow(0.2f, 1.7f), std::pow(0.7f, 1.7f), -std::pow(1.2f, 1.7f), 0},
            glm::vec4{z * glm::dot(n, z) / glm::dot(z, z), 0},
            glm::vec4{intersections, glm::dot(inputs.normal, inputs.position) + inputs.ray.w > 0 ? 1.0f : 0.0f, 0},
            glm::vec4{-n.x, atan_reference(y, -x), atan_reference(-y, x), atan_reference(-y, -x)}};
        constexpr std::array names{"affine3 square", "packed normal", "affine3 linear", "inverse affine",
                                   "inverse 3x3", "4x4 linear", "orthonormal basis", "affine2 square",
                                   "acos/atan/sign", "signed pow", "projection", "sphere queries", "atan quadrants"};
        for (std::size_t index = 0; index < expected.size(); ++index) {
            SCOPED_TRACE(names[index]);
            for (int component = 0; component < 4; ++component)
                EXPECT_NEAR(result->values[index][component], expected[index][component], 2e-4f);
        }
    }
}
}  // namespace
