#include <gtest/gtest.h>

import std;
import webgpu;
import epix.assets;
import epix.shader;
import epix.extension.grid;
import epix.extension.grid_gpu;

using namespace epix::assets;
using namespace epix::shader;
using namespace epix::ext::grid;
using namespace epix::ext::grid_gpu;

// ===========================================================================
// Shader::from_slang 锟?integration with the shader system
// ===========================================================================

TEST(SvoSlangFromSlang, SetsPath) {
    auto s = Shader::from_slang(std::string(kSvoGridSlangSource), "embedded://epix/shaders/grid/svo.slang");
    EXPECT_EQ(s.path, AssetPath("embedded://epix/shaders/grid/svo.slang"));
}

TEST(SvoSlangFromSlang, SourceIsSlang) {
    auto s = Shader::from_slang(std::string(kSvoGridSlangSource), "embedded://epix/shaders/grid/svo.slang");
    EXPECT_TRUE(s.source.is_slang());
    EXPECT_FALSE(s.source.is_wgsl());
    EXPECT_FALSE(s.source.is_spirv());
}

TEST(SvoSlangFromSlang, SourceTextPreserved) {
    auto s = Shader::from_slang(std::string(kSvoGridSlangSource), "embedded://epix/shaders/grid/svo.slang");
    EXPECT_EQ(s.source.as_str(), kSvoGridSlangSource);
}

// module epix.ext.grid.svo; -> normalized custom path "epix/ext/grid/svo"
TEST(SvoSlangFromSlang, ImportPathIsCustom) {
    auto s = Shader::from_slang(std::string(kSvoGridSlangSource), "embedded://epix/shaders/grid/svo.slang");
    EXPECT_TRUE(s.import_path.is_custom());
}

TEST(SvoSlangFromSlang, ImportPathValue) {
    auto s = Shader::from_slang(std::string(kSvoGridSlangSource), "embedded://epix/shaders/grid/svo.slang");
    EXPECT_EQ(s.import_path.as_custom(), "epix/ext/grid/svo");
}

TEST(SvoSlangFromSlang, NoImports) {
    auto s = Shader::from_slang(std::string(kSvoGridSlangSource), "embedded://epix/shaders/grid/svo.slang");
    EXPECT_TRUE(s.imports.empty());
}

// ===========================================================================
// Shader::preprocess_slang 锟?direct preprocessing
// ===========================================================================

TEST(SvoSlangPreprocess, ImportPathIsCustom) {
    auto [ip, imps] = Shader::preprocess_slang(kSvoGridSlangSource, "embedded://epix/shaders/grid/svo.slang");
    EXPECT_TRUE(ip.is_custom());
}

TEST(SvoSlangPreprocess, ImportPathValue) {
    auto [ip, imps] = Shader::preprocess_slang(kSvoGridSlangSource, "embedded://epix/shaders/grid/svo.slang");
    EXPECT_EQ(ip.as_custom(), "epix/ext/grid/svo");
}

TEST(SvoSlangPreprocess, NoImports) {
    auto [ip, imps] = Shader::preprocess_slang(kSvoGridSlangSource, "embedded://epix/shaders/grid/svo.slang");
    EXPECT_TRUE(imps.empty());
}

// ===========================================================================
// GPU helpers shared across all GPU tests
// ===========================================================================

static std::vector<std::uint32_t> bytes_to_words(std::span<const std::uint8_t> bytes) {
    if (bytes.size() % 4 != 0) return {};
    std::vector<std::uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

struct CompiledShader {
    std::variant<std::vector<std::uint32_t>, std::string> data;
};

struct GpuCtx {
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
};

static std::optional<GpuCtx> make_gpu_ctx() {
    GpuCtx ctx;
    ctx.instance = wgpu::createInstance();
    if (!ctx.instance) return std::nullopt;

    auto try_adapter = [&](wgpu::RequestAdapterOptions opts) { return ctx.instance.requestAdapter(opts); };
    ctx.adapter      = try_adapter(wgpu::RequestAdapterOptions()
                                       .setPowerPreference(wgpu::PowerPreference::eHighPerformance)
                                       .setBackendType(wgpu::BackendType::eVulkan));
    if (!ctx.adapter)
        ctx.adapter =
            try_adapter(wgpu::RequestAdapterOptions().setPowerPreference(wgpu::PowerPreference::eHighPerformance));
    if (!ctx.adapter) ctx.adapter = try_adapter(wgpu::RequestAdapterOptions());
    if (!ctx.adapter) return std::nullopt;

    auto errors = ctx.errors;
    ctx.device  = ctx.adapter.requestDevice(
        wgpu::DeviceDescriptor()
            .setLabel("SvoTestDevice")
            .setDefaultQueue(wgpu::QueueDescriptor().setLabel("SvoTestQueue"))
            .setDeviceLostCallbackInfo(
                wgpu::DeviceLostCallbackInfo()
                    .setMode(wgpu::CallbackMode::eAllowSpontaneous)
                    .setCallback([errors](const wgpu::Device&, wgpu::DeviceLostReason r, wgpu::StringView msg) {
                        errors->push_back(std::string("lost:") + std::string(wgpu::to_string(r)) + ":" +
                                          std::string(std::string_view(msg)));
                    }))
            .setUncapturedErrorCallbackInfo(wgpu::UncapturedErrorCallbackInfo().setCallback(
                [errors](const wgpu::Device&, wgpu::ErrorType t, wgpu::StringView msg) {
                    errors->push_back(std::string("err:") + std::string(wgpu::to_string(t)) + ":" +
                                      std::string(std::string_view(msg)));
                })));
    if (!ctx.device) return std::nullopt;
    ctx.queue = ctx.device.getQueue();
    if (!ctx.queue) return std::nullopt;
    return ctx;
}

// Compile the svo library + a caller shader via ShaderCache.
// Returns the backend-ready shader source for the main shader or nullopt on error.
static std::optional<CompiledShader> compile_svo_caller(std::string caller_source,
                                                        std::string caller_path = "embedded://test/svo_caller.slang") {
    std::array<std::uint8_t, 16> lib_bytes{};
    lib_bytes[0] = 0xAB;
    std::array<std::uint8_t, 16> main_bytes{};
    main_bytes[0] = 0xCD;
    auto lib_id   = AssetId<Shader>(uuids::uuid(lib_bytes));
    auto main_id  = AssetId<Shader>(uuids::uuid(main_bytes));

    std::optional<CompiledShader> compiled_out;
    ShaderCache cache(wgpu::Device{},
                      [&](const wgpu::Device&, const ShaderCacheSource& src,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          if (auto* s = std::get_if<ShaderCacheSource::SpirV>(&src.data)) {
                              compiled_out = CompiledShader{bytes_to_words(s->bytes)};
                          } else if (auto* s = std::get_if<ShaderCacheSource::Wgsl>(&src.data)) {
                              compiled_out = CompiledShader{std::string(s->source)};
                          }
                          return wgpu::ShaderModule{};
                      });

    auto lib_shader = Shader::from_slang(std::string(kSvoGridSlangSource), "embedded://epix/shaders/grid/svo.slang");
    cache.set_shader(lib_id, lib_shader);
    cache.set_shader(main_id, Shader::from_slang(std::move(caller_source), std::move(caller_path)));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    if (!result.has_value()) return std::nullopt;
    if (!compiled_out.has_value()) return std::nullopt;
    if (auto* spirv = std::get_if<std::vector<std::uint32_t>>(&compiled_out->data);
        spirv != nullptr && spirv->empty()) {
        return std::nullopt;
    }
    return compiled_out;
}

static wgpu::ShaderModule create_shader_module_from_compiled(const wgpu::Device& device,
                                                             const CompiledShader& shader,
                                                             std::string_view label) {
    auto desc = wgpu::ShaderModuleDescriptor().setLabel(label);
    if (const auto* spirv = std::get_if<std::vector<std::uint32_t>>(&shader.data)) {
        return device.createShaderModule(desc.setNextInChain(
            wgpu::ShaderSourceSPIRV().setCodeSize(static_cast<std::uint32_t>(spirv->size())).setCode(spirv->data())));
    }

    const auto& wgsl = std::get<std::string>(shader.data);
    return device.createShaderModule(
        desc.setNextInChain(wgpu::ShaderSourceWGSL().setCode(wgpu::StringView(std::string_view(wgsl)))));
}

// ===========================================================================
// ShaderCache integration 鈥?library registered as module
// ===========================================================================

TEST(SvoShaderCache, LibraryRegisters_CompileSucceeds) {
    auto words = compile_svo_caller(R"(
import epix.ext.grid.svo;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain() {}
)");
    EXPECT_TRUE(words.has_value());
}

TEST(SvoShaderCache, LibraryRegistered_ImportPathMatchesCustomName) {
    // The svo library carries import_path "epix/ext/grid/svo".
    // A shader that does `import epix.ext.grid.svo;` must resolve to it.
    std::array<std::uint8_t, 16> lib_bytes{};
    lib_bytes[0] = 0x01;
    std::array<std::uint8_t, 16> main_bytes{};
    main_bytes[0] = 0x02;
    auto lib_id   = AssetId<Shader>(uuids::uuid(lib_bytes));
    auto main_id  = AssetId<Shader>(uuids::uuid(main_bytes));

    int call_count = 0;
    ShaderCache cache(wgpu::Device{},
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      });

    cache.set_shader(lib_id,
                     Shader::from_slang(std::string(kSvoGridSlangSource), "embedded://epix/shaders/grid/svo.slang"));
    cache.set_shader(main_id, Shader::from_slang(R"(
import epix.ext.grid.svo;
[shader("compute")][numthreads(1,1,1)]
void computeMain() {}
)",
                                                 "embedded://test/main.slang"));

    auto r = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(r.has_value()) << r.error().message();
    EXPECT_EQ(call_count, 1);
}

TEST(SvoShaderCache, LibraryOnly_NoEntryPoints_ReturnsError) {
    // Library-only shaders (no entry points) cannot be compiled to SPIR-V as a
    // root module.  ShaderCache must return a NoEntryPoints error instead of
    // silently succeeding.
    std::array<std::uint8_t, 16> lib_bytes{};
    lib_bytes[0] = 0x03;
    auto lib_id  = AssetId<Shader>(uuids::uuid(lib_bytes));

    ShaderCache cache(
        wgpu::Device{},
        [&](const wgpu::Device&, const ShaderCacheSource&,
            ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> { return wgpu::ShaderModule{}; });

    cache.set_shader(lib_id,
                     Shader::from_slang(std::string(kSvoGridSlangSource), "embedded://epix/shaders/grid/svo.slang"));
    auto r = cache.get(CachedPipelineId{1}, lib_id, {});
    ASSERT_FALSE(r.has_value());
    auto* slang_err = std::get_if<ShaderCacheError::SlangCompileError>(&r.error().data);
    ASSERT_NE(slang_err, nullptr) << "Expected SlangCompileError, got: " << r.error().message();
    EXPECT_EQ(slang_err->stage, ShaderCacheError::SlangCompileError::Stage::NoEntryPoints);
}

TEST(SvoShaderCache, CallerWithoutLibrary_FailsWithImportNotAvailable) {
    std::array<std::uint8_t, 16> main_bytes{};
    main_bytes[0] = 0x04;
    auto main_id  = AssetId<Shader>(uuids::uuid(main_bytes));

    ShaderCache cache(
        wgpu::Device{},
        [&](const wgpu::Device&, const ShaderCacheSource&,
            ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> { return wgpu::ShaderModule{}; });

    cache.set_shader(main_id, Shader::from_slang(R"(
import epix.ext.grid.svo;
[shader("compute")][numthreads(1,1,1)]
void computeMain() {}
)",
                                                 "embedded://test/main.slang"));

    auto r = cache.get(CachedPipelineId{1}, main_id, {});
    // For Slang shaders, a missing import results in a SlangCompileError (not recoverable).
    EXPECT_FALSE(r.has_value());
}

TEST(SvoShaderCache, CallerWithStructAccess_CompileSucceeds) {
    // Caller uses epix::ext::grid::SvoGrid2D inside a compute shader.
    auto words = compile_svo_caller(R"(
import epix.ext.grid.svo;

[[vk::binding(0, 0)]] StructuredBuffer<uint> svo_buf;
[[vk::binding(1, 0)]] RWStructuredBuffer<int> out_result;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain()
{
    epix::ext::grid::SvoGrid2D grid = epix::ext::grid::SvoGrid2D(svo_buf);
    out_result[0] = grid.lookup({0, 0});
}
)");
    EXPECT_TRUE(words.has_value());
}

TEST(SvoShaderCache, CallerWithSvoGrid3D_CompileSucceeds) {
    auto words = compile_svo_caller(R"(
import epix.ext.grid.svo;

[[vk::binding(0, 0)]] StructuredBuffer<uint> svo_buf;
[[vk::binding(1, 0)]] RWStructuredBuffer<int> out_result;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain()
{
    epix::ext::grid::SvoGrid3D grid = epix::ext::grid::SvoGrid3D(svo_buf);
    out_result[0] = grid.lookup({0, 0, 0});
}
)");
    EXPECT_TRUE(words.has_value());
}

TEST(SvoShaderCache, CallerWithContains_CompileSucceeds) {
    auto words = compile_svo_caller(R"(
import epix.ext.grid.svo;

[[vk::binding(0, 0)]] StructuredBuffer<uint> svo_buf;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> out_result;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain()
{
    epix::ext::grid::SvoGrid2D grid = epix::ext::grid::SvoGrid2D(svo_buf);
    out_result[0] = grid.contains({1, 2}) ? 1u : 0u;
}
)");
    EXPECT_TRUE(words.has_value());
}

TEST(SvoShaderCache, CallerWith1D_CompileSucceeds) {
    auto words = compile_svo_caller(R"(
import epix.ext.grid.svo;

[[vk::binding(0, 0)]] StructuredBuffer<uint> svo_buf;
[[vk::binding(1, 0)]] RWStructuredBuffer<int> out_result;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain()
{
    epix::ext::grid::SvoGrid1D grid = epix::ext::grid::SvoGrid1D(svo_buf);
    out_result[0] = grid.lookup({5});
}
)");
    EXPECT_TRUE(words.has_value());
}

// ===========================================================================
// GPU compute 鈥?execute SvoGrid2D lookup with real hardware
// ===========================================================================

// Encode a 2D SVO buffer { (1,2) } and verify the GPU lookup returns 0 (data idx).
TEST(SvoGpuCompute, Lookup2D_KnownCell_ReturnsDataIndex) {
    auto words = compile_svo_caller(R"(
import epix.ext.grid.svo;

[[vk::binding(0, 0)]] StructuredBuffer<uint>  svo_buf;
[[vk::binding(1, 0)]] RWStructuredBuffer<int> out_result;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain()
{
    epix::ext::grid::SvoGrid2D grid = epix::ext::grid::SvoGrid2D(svo_buf);
    out_result[0] = grid.lookup({1, 2});   // present  鈫?0
    out_result[1] = grid.lookup({0, 0});   // absent   鈫?-1
    out_result[2] = grid.data_count();
}
)");
    ASSERT_TRUE(words.has_value()) << "Slang compilation failed";

    auto gpu = make_gpu_ctx();
    if (!gpu.has_value()) GTEST_SKIP() << "No WebGPU adapter available";

    // Build the SVO buffer on the CPU
    tree_extendible_grid<2, int> grid;
    grid.set({1, 2}, 42);
    SvoBuffer svo = svo_upload(grid).value();

    const std::uint32_t svo_bytes    = static_cast<std::uint32_t>(svo.words.size() * sizeof(uint32_t));
    const std::uint32_t result_bytes = 3 * sizeof(std::int32_t);

    // Upload SVO to GPU
    auto svo_buf  = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                 .setLabel("SvoData")
                                                 .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst)
                                                 .setSize(svo_bytes));
    auto out_buf  = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                 .setLabel("SvoOut")
                                                 .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc)
                                                 .setSize(result_bytes));
    auto read_buf = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                 .setLabel("SvoRead")
                                                 .setUsage(wgpu::BufferUsage::eMapRead | wgpu::BufferUsage::eCopyDst)
                                                 .setSize(result_bytes));
    ASSERT_TRUE(svo_buf);
    ASSERT_TRUE(out_buf);
    ASSERT_TRUE(read_buf);

    gpu->queue.writeBuffer(svo_buf, 0, svo.words.data(), svo_bytes);

    auto bgl =
        gpu->device.createBindGroupLayout(wgpu::BindGroupLayoutDescriptor()
                                              .setLabel("SvoLookupBGL")
                                              .setEntries(std::array{
                                                  wgpu::BindGroupLayoutEntry()
                                                      .setBinding(0)
                                                      .setVisibility(wgpu::ShaderStage::eCompute)
                                                      .setBuffer(wgpu::BufferBindingLayout()
                                                                     .setType(wgpu::BufferBindingType::eReadOnlyStorage)
                                                                     .setMinBindingSize(svo_bytes)),
                                                  wgpu::BindGroupLayoutEntry()
                                                      .setBinding(1)
                                                      .setVisibility(wgpu::ShaderStage::eCompute)
                                                      .setBuffer(wgpu::BufferBindingLayout()
                                                                     .setType(wgpu::BufferBindingType::eStorage)
                                                                     .setMinBindingSize(result_bytes)),
                                              }));
    ASSERT_TRUE(bgl);

    auto pl = gpu->device.createPipelineLayout(
        wgpu::PipelineLayoutDescriptor().setLabel("SvoLookupPL").setBindGroupLayouts(std::array{bgl}));
    ASSERT_TRUE(pl);

    auto module = create_shader_module_from_compiled(gpu->device, *words, "SvoLookupModule");
    ASSERT_TRUE(module);

    auto pipeline = gpu->device.createComputePipeline(
        wgpu::ComputePipelineDescriptor()
            .setLabel("SvoLookupPipeline")
            .setLayout(pl)
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(module).setEntryPoint("computeMain")));
    ASSERT_TRUE(pipeline);

    auto bg = gpu->device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("SvoLookupBG")
            .setLayout(bgl)
            .setEntries(std::array{
                wgpu::BindGroupEntry().setBinding(0).setBuffer(svo_buf).setSize(svo_bytes),
                wgpu::BindGroupEntry().setBinding(1).setBuffer(out_buf).setSize(result_bytes),
            }));
    ASSERT_TRUE(bg);

    auto enc = gpu->device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("SvoEnc"));
    {
        auto pass = enc.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bg, std::span<const std::uint32_t>{});
        pass.dispatchWorkgroups(1, 1, 1);
        pass.end();
    }
    enc.copyBufferToBuffer(out_buf, 0, read_buf, 0, result_bytes);
    gpu->queue.submit(enc.finish());

    bool done = false, ok = false;
    (void)read_buf.mapAsync(wgpu::MapMode::eRead, 0, result_bytes,
                            wgpu::BufferMapCallbackInfo()
                                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus s, wgpu::StringView) {
                                    ok   = s == wgpu::MapAsyncStatus::eSuccess;
                                    done = true;
                                })));
    while (!done && gpu->errors->empty()) gpu->device.poll(false);
    ASSERT_TRUE(done) << "GPU buffer map never completed"
                      << (gpu->errors->empty() ? "" : ": " + gpu->errors->front());
    ASSERT_TRUE(ok);

    auto* r = static_cast<const std::int32_t*>(read_buf.getConstMappedRange(0, result_bytes));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r[0], 0);   // (1,2) present  鈫?data index 0
    EXPECT_EQ(r[1], -1);  // (0,0) absent   鈫?-1
    EXPECT_EQ(r[2], 1);   // data_count == 1
    read_buf.unmap();

    EXPECT_TRUE(gpu->errors->empty()) << gpu->errors->front();
}

// Encode a 3D SVO buffer with two cells and verify both hits and a miss.
TEST(SvoGpuCompute, Lookup3D_TwoCells_HitsAndMiss) {
    auto words = compile_svo_caller(R"(
import epix.ext.grid.svo;

[[vk::binding(0, 0)]] StructuredBuffer<uint>  svo_buf;
[[vk::binding(1, 0)]] RWStructuredBuffer<int> out_result;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain()
{
    epix::ext::grid::SvoGrid3D grid = epix::ext::grid::SvoGrid3D(svo_buf);
    out_result[0] = grid.lookup({0, 0, 0});   // first cell
    out_result[1] = grid.lookup({1, 1, 1});   // second cell
    out_result[2] = grid.lookup({0, 1, 0});   // absent
    out_result[3] = int(grid.data_count());
}
)",
                                    "embedded://test/svo_caller_3d.slang");
    ASSERT_TRUE(words.has_value()) << "Slang compilation failed";

    auto gpu = make_gpu_ctx();
    if (!gpu.has_value()) GTEST_SKIP() << "No WebGPU adapter available";

    tree_extendible_grid<3, int> grid;
    grid.set({0, 0, 0}, 10);
    grid.set({1, 1, 1}, 20);
    SvoBuffer svo = svo_upload(grid).value();

    const std::uint32_t svo_bytes    = static_cast<std::uint32_t>(svo.words.size() * sizeof(uint32_t));
    const std::uint32_t result_bytes = 4 * sizeof(std::int32_t);

    auto svo_buf  = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                 .setLabel("Svo3D")
                                                 .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst)
                                                 .setSize(svo_bytes));
    auto out_buf  = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                 .setLabel("Out3D")
                                                 .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc)
                                                 .setSize(result_bytes));
    auto read_buf = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                 .setLabel("Read3D")
                                                 .setUsage(wgpu::BufferUsage::eMapRead | wgpu::BufferUsage::eCopyDst)
                                                 .setSize(result_bytes));
    ASSERT_TRUE(svo_buf);
    ASSERT_TRUE(out_buf);
    ASSERT_TRUE(read_buf);

    gpu->queue.writeBuffer(svo_buf, 0, svo.words.data(), svo_bytes);

    auto bgl =
        gpu->device.createBindGroupLayout(wgpu::BindGroupLayoutDescriptor().setLabel("BGL3D").setEntries(std::array{
            wgpu::BindGroupLayoutEntry()
                .setBinding(0)
                .setVisibility(wgpu::ShaderStage::eCompute)
                .setBuffer(wgpu::BufferBindingLayout()
                               .setType(wgpu::BufferBindingType::eReadOnlyStorage)
                               .setMinBindingSize(svo_bytes)),
            wgpu::BindGroupLayoutEntry()
                .setBinding(1)
                .setVisibility(wgpu::ShaderStage::eCompute)
                .setBuffer(wgpu::BufferBindingLayout()
                               .setType(wgpu::BufferBindingType::eStorage)
                               .setMinBindingSize(result_bytes)),
        }));
    ASSERT_TRUE(bgl);

    auto pl = gpu->device.createPipelineLayout(wgpu::PipelineLayoutDescriptor().setBindGroupLayouts(std::array{bgl}));
    auto module = create_shader_module_from_compiled(gpu->device, *words, "SvoLookup3DModule");
    ASSERT_TRUE(pl);
    ASSERT_TRUE(module);

    auto pipeline = gpu->device.createComputePipeline(wgpu::ComputePipelineDescriptor().setLayout(pl).setCompute(
        wgpu::ProgrammableStageDescriptor().setModule(module).setEntryPoint("computeMain")));
    ASSERT_TRUE(pipeline);

    auto bg = gpu->device.createBindGroup(wgpu::BindGroupDescriptor().setLayout(bgl).setEntries(std::array{
        wgpu::BindGroupEntry().setBinding(0).setBuffer(svo_buf).setSize(svo_bytes),
        wgpu::BindGroupEntry().setBinding(1).setBuffer(out_buf).setSize(result_bytes),
    }));
    ASSERT_TRUE(bg);

    auto enc = gpu->device.createCommandEncoder();
    {
        auto pass = enc.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bg, std::span<const std::uint32_t>{});
        pass.dispatchWorkgroups(1, 1, 1);
        pass.end();
    }
    enc.copyBufferToBuffer(out_buf, 0, read_buf, 0, result_bytes);
    gpu->queue.submit(enc.finish());

    bool done = false, ok = false;
    (void)read_buf.mapAsync(wgpu::MapMode::eRead, 0, result_bytes,
                            wgpu::BufferMapCallbackInfo()
                                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus s, wgpu::StringView) {
                                    ok   = s == wgpu::MapAsyncStatus::eSuccess;
                                    done = true;
                                })));
    while (!done && gpu->errors->empty()) gpu->device.poll(false);
    ASSERT_TRUE(done) << "GPU buffer map never completed"
                      << (gpu->errors->empty() ? "" : ": " + gpu->errors->front());
    ASSERT_TRUE(ok);

    auto* r = static_cast<const std::int32_t*>(read_buf.getConstMappedRange(0, result_bytes));
    ASSERT_NE(r, nullptr);
    EXPECT_GE(r[0], 0);     // (0,0,0) present 鈥?exact index depends on iter order
    EXPECT_GE(r[1], 0);     // (1,1,1) present
    EXPECT_NE(r[0], r[1]);  // distinct data indices
    EXPECT_EQ(r[2], -1);    // (0,1,0) absent
    EXPECT_EQ(r[3], 2);     // data_count == 2
    read_buf.unmap();

    EXPECT_TRUE(gpu->errors->empty()) << gpu->errors->front();
}

// Verify that an empty grid produces data_count==0 and all lookups return -1.
TEST(SvoGpuCompute, EmptyGrid_LookupReturnsMinusOne) {
    auto words = compile_svo_caller(R"(
import epix.ext.grid.svo;

[[vk::binding(0, 0)]] StructuredBuffer<uint>  svo_buf;
[[vk::binding(1, 0)]] RWStructuredBuffer<int> out_result;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain()
{
    epix::ext::grid::SvoGrid2D grid = epix::ext::grid::SvoGrid2D(svo_buf);
    out_result[0] = grid.lookup({0, 0});
    out_result[1] = int(grid.data_count());
}
)",
                                    "embedded://test/svo_caller_empty.slang");
    ASSERT_TRUE(words.has_value()) << "Slang compilation failed";

    auto gpu = make_gpu_ctx();
    if (!gpu.has_value()) GTEST_SKIP() << "No WebGPU adapter available";

    tree_extendible_grid<2, int> empty_grid;
    SvoBuffer svo = svo_upload(empty_grid).value();

    // Ensure minimum buffer size of 16 bytes (WebGPU minimum storage binding)
    const std::uint32_t svo_bytes =
        static_cast<std::uint32_t>(std::max(svo.words.size() * sizeof(uint32_t), std::size_t{16}));
    const std::uint32_t result_bytes = 2 * sizeof(std::int32_t);

    // Pad the SVO words if needed
    std::vector<uint32_t> padded_words = svo.words;
    while (padded_words.size() * sizeof(uint32_t) < svo_bytes) padded_words.push_back(0u);

    auto svo_buf  = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                 .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst)
                                                 .setSize(svo_bytes));
    auto out_buf  = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                 .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc)
                                                 .setSize(result_bytes));
    auto read_buf = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                 .setUsage(wgpu::BufferUsage::eMapRead | wgpu::BufferUsage::eCopyDst)
                                                 .setSize(result_bytes));
    ASSERT_TRUE(svo_buf);
    ASSERT_TRUE(out_buf);
    ASSERT_TRUE(read_buf);

    gpu->queue.writeBuffer(svo_buf, 0, padded_words.data(), svo_bytes);

    auto bgl = gpu->device.createBindGroupLayout(wgpu::BindGroupLayoutDescriptor().setEntries(std::array{
        wgpu::BindGroupLayoutEntry()
            .setBinding(0)
            .setVisibility(wgpu::ShaderStage::eCompute)
            .setBuffer(wgpu::BufferBindingLayout()
                           .setType(wgpu::BufferBindingType::eReadOnlyStorage)
                           .setMinBindingSize(svo_bytes)),
        wgpu::BindGroupLayoutEntry()
            .setBinding(1)
            .setVisibility(wgpu::ShaderStage::eCompute)
            .setBuffer(
                wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eStorage).setMinBindingSize(result_bytes)),
    }));
    ASSERT_TRUE(bgl);

    auto pl = gpu->device.createPipelineLayout(wgpu::PipelineLayoutDescriptor().setBindGroupLayouts(std::array{bgl}));
    auto module = create_shader_module_from_compiled(gpu->device, *words, "SvoEmptyModule");
    ASSERT_TRUE(pl);
    ASSERT_TRUE(module);

    auto pipeline = gpu->device.createComputePipeline(wgpu::ComputePipelineDescriptor().setLayout(pl).setCompute(
        wgpu::ProgrammableStageDescriptor().setModule(module).setEntryPoint("computeMain")));
    ASSERT_TRUE(pipeline);

    auto bg = gpu->device.createBindGroup(wgpu::BindGroupDescriptor().setLayout(bgl).setEntries(std::array{
        wgpu::BindGroupEntry().setBinding(0).setBuffer(svo_buf).setSize(svo_bytes),
        wgpu::BindGroupEntry().setBinding(1).setBuffer(out_buf).setSize(result_bytes),
    }));
    ASSERT_TRUE(bg);

    auto enc = gpu->device.createCommandEncoder();
    {
        auto pass = enc.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bg, std::span<const std::uint32_t>{});
        pass.dispatchWorkgroups(1, 1, 1);
        pass.end();
    }
    enc.copyBufferToBuffer(out_buf, 0, read_buf, 0, result_bytes);
    gpu->queue.submit(enc.finish());

    bool done = false, ok = false;
    (void)read_buf.mapAsync(wgpu::MapMode::eRead, 0, result_bytes,
                            wgpu::BufferMapCallbackInfo()
                                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus s, wgpu::StringView) {
                                    ok   = s == wgpu::MapAsyncStatus::eSuccess;
                                    done = true;
                                })));
    while (!done && gpu->errors->empty()) gpu->device.poll(false);
    ASSERT_TRUE(done) << "GPU buffer map never completed"
                      << (gpu->errors->empty() ? "" : ": " + gpu->errors->front());
    ASSERT_TRUE(ok);

    auto* r = static_cast<const std::int32_t*>(read_buf.getConstMappedRange(0, result_bytes));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r[0], -1);  // empty 鈫?-1
    EXPECT_EQ(r[1], 0);   // data_count == 0
    read_buf.unmap();

    EXPECT_TRUE(gpu->errors->empty()) << gpu->errors->front();
}

// Large-scale 3D SVO test: randomly generated grid, batch GPU lookup vs. CPU ground truth.
// The shader uses three bindings:
//   0 鈥?SvoBuffer (StructuredBuffer<uint>)
//   1 鈥?queries   (StructuredBuffer<int>, packed 3 ints per query: x, y, z)
//   2 鈥?results   (RWStructuredBuffer<int>)
// One workgroup (numthreads 1,1,1) is dispatched per query.
TEST(SvoGpuCompute, LargeScale3D_RandomGrid_BatchLookup) {
    auto words = compile_svo_caller(R"(
import epix.ext.grid.svo;

[[vk::binding(0, 0)]] StructuredBuffer<uint>  svo_buf;
[[vk::binding(1, 0)]] StructuredBuffer<int>   queries;  // packed x,y,z per entry
[[vk::binding(2, 0)]] RWStructuredBuffer<int> results;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain(uint3 dtid : SV_DispatchThreadID)
{
    epix::ext::grid::SvoGrid3D grid = epix::ext::grid::SvoGrid3D(svo_buf);
    uint i     = dtid.x;
    uint base  = i * 3u;
    results[i] = grid.lookup({queries[base], queries[base + 1u], queries[base + 2u]});
}
)",
                                    "embedded://test/svo_large_scale.slang");
    ASSERT_TRUE(words.has_value()) << "Slang compilation failed";

    auto gpu = make_gpu_ctx();
    if (!gpu.has_value()) GTEST_SKIP() << "No WebGPU adapter available";

    // ---- Build random 3D grid (fixed seed for reproducibility) ----
    std::mt19937 rng(0xBEEFu);
    std::uniform_int_distribution<int> coord(-32, 31);  // 64^3 region

    tree_extendible_grid<3, int> grid;
    while (grid.count() < 200) {
        std::array<std::int32_t, 3> pos = {coord(rng), coord(rng), coord(rng)};
        if (grid.contains(pos)) continue;
        grid.set(pos, static_cast<int>(grid.count()));
    }

    // ---- Build CPU expected map: position 鈫?data_index ----
    std::map<std::array<std::int32_t, 3>, int> expected;
    int data_idx = 0;
    for (const auto& pos : grid.iter_pos()) expected[pos] = data_idx++;

    // ---- Build query list ----
    // Hit queries:  every inserted position (200 hits)
    // Miss queries: 200 positions with x = 200+i, guaranteed outside [-32,31]
    std::vector<std::int32_t> query_flat;  // 3 ints per query
    std::vector<int> query_expect;         // expected GPU result per query
    for (const auto& pos : grid.iter_pos()) {
        query_flat.push_back(pos[0]);
        query_flat.push_back(pos[1]);
        query_flat.push_back(pos[2]);
        query_expect.push_back(expected[pos]);
    }
    for (int i = 0; i < 200; ++i) {
        query_flat.push_back(200 + i);
        query_flat.push_back(0);
        query_flat.push_back(0);
        query_expect.push_back(-1);
    }
    const std::uint32_t N_Q = static_cast<std::uint32_t>(query_expect.size());

    // ---- GPU buffers ----
    SvoBuffer svo                     = svo_upload(grid).value();
    const std::uint32_t svo_bytes     = static_cast<std::uint32_t>(svo.words.size() * sizeof(std::uint32_t));
    const std::uint32_t queries_bytes = N_Q * 3u * sizeof(std::int32_t);
    const std::uint32_t results_bytes = N_Q * sizeof(std::int32_t);

    auto svo_buf   = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("LargeSvo")
                                                  .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst)
                                                  .setSize(svo_bytes));
    auto query_buf = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("LargeQueries")
                                                  .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst)
                                                  .setSize(queries_bytes));
    auto out_buf   = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("LargeOut")
                                                  .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc)
                                                  .setSize(results_bytes));
    auto read_buf  = gpu->device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("LargeRead")
                                                  .setUsage(wgpu::BufferUsage::eMapRead | wgpu::BufferUsage::eCopyDst)
                                                  .setSize(results_bytes));
    ASSERT_TRUE(svo_buf);
    ASSERT_TRUE(query_buf);
    ASSERT_TRUE(out_buf);
    ASSERT_TRUE(read_buf);

    gpu->queue.writeBuffer(svo_buf, 0, svo.words.data(), svo_bytes);
    gpu->queue.writeBuffer(query_buf, 0, query_flat.data(), queries_bytes);

    auto bgl =
        gpu->device.createBindGroupLayout(wgpu::BindGroupLayoutDescriptor()
                                              .setLabel("LargeBGL")
                                              .setEntries(std::array{
                                                  wgpu::BindGroupLayoutEntry()
                                                      .setBinding(0)
                                                      .setVisibility(wgpu::ShaderStage::eCompute)
                                                      .setBuffer(wgpu::BufferBindingLayout()
                                                                     .setType(wgpu::BufferBindingType::eReadOnlyStorage)
                                                                     .setMinBindingSize(svo_bytes)),
                                                  wgpu::BindGroupLayoutEntry()
                                                      .setBinding(1)
                                                      .setVisibility(wgpu::ShaderStage::eCompute)
                                                      .setBuffer(wgpu::BufferBindingLayout()
                                                                     .setType(wgpu::BufferBindingType::eReadOnlyStorage)
                                                                     .setMinBindingSize(queries_bytes)),
                                                  wgpu::BindGroupLayoutEntry()
                                                      .setBinding(2)
                                                      .setVisibility(wgpu::ShaderStage::eCompute)
                                                      .setBuffer(wgpu::BufferBindingLayout()
                                                                     .setType(wgpu::BufferBindingType::eStorage)
                                                                     .setMinBindingSize(results_bytes)),
                                              }));
    ASSERT_TRUE(bgl);

    auto pl = gpu->device.createPipelineLayout(
        wgpu::PipelineLayoutDescriptor().setLabel("LargePL").setBindGroupLayouts(std::array{bgl}));
    auto module = create_shader_module_from_compiled(gpu->device, *words, "LargeModule");
    ASSERT_TRUE(pl);
    ASSERT_TRUE(module);

    auto pipeline = gpu->device.createComputePipeline(
        wgpu::ComputePipelineDescriptor()
            .setLabel("LargePipeline")
            .setLayout(pl)
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(module).setEntryPoint("computeMain")));
    ASSERT_TRUE(pipeline);

    auto bg = gpu->device.createBindGroup(
        wgpu::BindGroupDescriptor().setLabel("LargeBG").setLayout(bgl).setEntries(std::array{
            wgpu::BindGroupEntry().setBinding(0).setBuffer(svo_buf).setSize(svo_bytes),
            wgpu::BindGroupEntry().setBinding(1).setBuffer(query_buf).setSize(queries_bytes),
            wgpu::BindGroupEntry().setBinding(2).setBuffer(out_buf).setSize(results_bytes),
        }));
    ASSERT_TRUE(bg);

    auto enc = gpu->device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LargeEnc"));
    {
        auto pass = enc.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bg, std::span<const std::uint32_t>{});
        pass.dispatchWorkgroups(N_Q, 1, 1);
        pass.end();
    }
    enc.copyBufferToBuffer(out_buf, 0, read_buf, 0, results_bytes);
    gpu->queue.submit(enc.finish());

    bool done = false, ok = false;
    (void)read_buf.mapAsync(wgpu::MapMode::eRead, 0, results_bytes,
                            wgpu::BufferMapCallbackInfo()
                                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus s, wgpu::StringView) {
                                    ok   = s == wgpu::MapAsyncStatus::eSuccess;
                                    done = true;
                                })));
    while (!done && gpu->errors->empty()) gpu->device.poll(false);
    ASSERT_TRUE(done) << "GPU buffer map never completed"
                      << (gpu->errors->empty() ? "" : ": " + gpu->errors->front());
    ASSERT_TRUE(ok);

    auto* r = static_cast<const std::int32_t*>(read_buf.getConstMappedRange(0, results_bytes));
    ASSERT_NE(r, nullptr);

    int hit_failures = 0, miss_failures = 0;
    for (std::uint32_t i = 0; i < N_Q; ++i) {
        if (r[i] != query_expect[i]) {
            if (i < 200u)
                ++hit_failures;
            else
                ++miss_failures;
        }
    }
    EXPECT_EQ(hit_failures, 0) << hit_failures << " of 200 hit queries returned wrong data index";
    EXPECT_EQ(miss_failures, 0) << miss_failures << " of 200 miss queries did not return -1";

    read_buf.unmap();
    EXPECT_TRUE(gpu->errors->empty()) << gpu->errors->front();
}
