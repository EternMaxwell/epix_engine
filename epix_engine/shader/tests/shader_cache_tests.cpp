// Tests for ShaderCache:
//   get, set_shader, remove, import resolution, def-keyed caching, pipeline tracking.

#include <gtest/gtest.h>

import std;
import epix.assets;
import epix.shader;

using namespace epix::shader;
using namespace epix::assets;

// ─── Test helpers ─────────────────────────────────────────────────────────

// Construct a typed AssetId from a single discriminating byte (for distinct IDs in tests).
static AssetId<Shader> make_id(std::uint8_t d) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[0] = d;
    return AssetId<Shader>(uuids::uuid(bytes));
}

// A null wgpu::Device is fine for testing cache logic — load_module never executes
// WebGPU API calls in tests, just returns a mock result.
static wgpu::Device null_device() { return wgpu::Device{}; }

// Build a simple WGSL shader with no imports.
static Shader simple_wgsl(std::string src = "fn main() {}", std::string path = "test.wgsl") {
    return Shader::from_wgsl(std::move(src), std::move(path));
}

// ===========================================================================
// Fixture: counter-based mock load_module
// ===========================================================================

class ShaderCacheTest : public ::testing::Test {
   protected:
    int call_count{0};
    std::expected<wgpu::ShaderModule, ShaderCacheError> load_result{wgpu::ShaderModule{}};

    ShaderCache cache{null_device(),
                      [this](const wgpu::Device&,
                             const ShaderCacheSource&,
                             ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return load_result;
                      }};
};

// ===========================================================================
// get — unregistered shader
// ===========================================================================

TEST_F(ShaderCacheTest, Get_UnregisteredShader_ShaderNotLoaded) {
    auto result = cache.get(CachedPipelineId{1}, make_id(1), {});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderNotLoaded>(result.error().data));
    EXPECT_EQ(std::get<ShaderCacheError::ShaderNotLoaded>(result.error().data).id, make_id(1));
}

// ===========================================================================
// set_shader + get — basic success
// ===========================================================================

TEST_F(ShaderCacheTest, SetShader_NewShader_ReturnsEmptyAffected) {
    auto id       = make_id(1);
    auto affected = cache.set_shader(id, simple_wgsl());
    EXPECT_TRUE(affected.empty());
}

TEST_F(ShaderCacheTest, SetShader_Get_NoImports_CallsLoadModule) {
    auto id = make_id(1);
    cache.set_shader(id, simple_wgsl());
    auto result = cache.get(CachedPipelineId{1}, id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
    EXPECT_NE(result.value(), nullptr);
}

TEST_F(ShaderCacheTest, Get_Same_Defs_CacheHit_LoadModuleCalledOnce) {
    auto id = make_id(1);
    cache.set_shader(id, simple_wgsl());
    cache.get(CachedPipelineId{1}, id, {});
    cache.get(CachedPipelineId{2}, id, {});  // same defs
    EXPECT_EQ(call_count, 1);                // load_module only called once
}

TEST_F(ShaderCacheTest, Get_DifferentDefs_CacheMiss_LoadModuleCalledAgain) {
    auto id                         = make_id(1);
    std::vector<ShaderDefVal> defsA = {ShaderDefVal::from_bool("FEAT_A")};
    std::vector<ShaderDefVal> defsB = {ShaderDefVal::from_bool("FEAT_B")};
    cache.set_shader(id, simple_wgsl());
    cache.get(CachedPipelineId{1}, id, defsA);
    cache.get(CachedPipelineId{2}, id, defsB);
    EXPECT_EQ(call_count, 2);  // different def sets → different compile
}

TEST_F(ShaderCacheTest, Get_ReturnsSameSharedPtrForSameDefs) {
    auto id = make_id(1);
    cache.set_shader(id, simple_wgsl());
    auto r1 = cache.get(CachedPipelineId{1}, id, {});
    auto r2 = cache.get(CachedPipelineId{2}, id, {});
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1.value().get(), r2.value().get());  // same pointer from cache
}

// ===========================================================================
// Pipeline tracking
// ===========================================================================

TEST_F(ShaderCacheTest, SetShader_Update_ReturnsAffectedPipelines) {
    auto id = make_id(1);
    cache.set_shader(id, simple_wgsl("fn v1() {}"));
    cache.get(CachedPipelineId{10}, id, {});
    cache.get(CachedPipelineId{20}, id, {});

    // Update the shader — both pipelines should be invalidated
    auto affected = cache.set_shader(id, simple_wgsl("fn v2() {}"));
    EXPECT_EQ(affected.size(), 2u);
    EXPECT_TRUE(std::ranges::contains(affected, CachedPipelineId{10}));
    EXPECT_TRUE(std::ranges::contains(affected, CachedPipelineId{20}));
}

TEST_F(ShaderCacheTest, SetShader_Update_ClearsCache_LoadModuleCalledAgain) {
    auto id = make_id(1);
    cache.set_shader(id, simple_wgsl("fn v1() {}"));
    cache.get(CachedPipelineId{1}, id, {});
    EXPECT_EQ(call_count, 1);

    cache.set_shader(id, simple_wgsl("fn v2() {}"));
    cache.get(CachedPipelineId{1}, id, {});
    EXPECT_EQ(call_count, 2);
}

TEST_F(ShaderCacheTest, MultiplePipelinesForDifferentDefs_AllTracked) {
    auto id = make_id(1);
    cache.set_shader(id, simple_wgsl());
    std::vector<ShaderDefVal> da = {ShaderDefVal::from_bool("A")};
    std::vector<ShaderDefVal> db = {ShaderDefVal::from_bool("B")};
    cache.get(CachedPipelineId{1}, id, da);
    cache.get(CachedPipelineId{2}, id, db);

    auto affected = cache.set_shader(id, simple_wgsl("fn v2() {}"));
    EXPECT_EQ(affected.size(), 2u);
}

// ===========================================================================
// remove
// ===========================================================================

TEST_F(ShaderCacheTest, Remove_ReturnsAffectedPipelines) {
    auto id = make_id(1);
    cache.set_shader(id, simple_wgsl());
    cache.get(CachedPipelineId{99}, id, {});
    auto affected = cache.remove(id);
    EXPECT_TRUE(std::ranges::contains(affected, CachedPipelineId{99}));
}

TEST_F(ShaderCacheTest, Remove_ThenGet_ShaderNotLoaded) {
    auto id = make_id(1);
    cache.set_shader(id, simple_wgsl());
    cache.remove(id);
    auto result = cache.get(CachedPipelineId{1}, id, {});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderNotLoaded>(result.error().data));
}

TEST_F(ShaderCacheTest, Remove_UnknownId_ReturnsEmpty) {
    auto affected = cache.remove(make_id(42));
    EXPECT_TRUE(affected.empty());
}

TEST_F(ShaderCacheTest, Remove_ThenReAdd_WorksAgain) {
    auto id = make_id(1);
    cache.set_shader(id, simple_wgsl("fn v1() {}"));
    cache.get(CachedPipelineId{1}, id, {});
    cache.remove(id);

    // Re-add and use again
    cache.set_shader(id, simple_wgsl("fn v2() {}"));
    auto result = cache.get(CachedPipelineId{2}, id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 2);  // once after first get, once after re-add get
}

// ===========================================================================
// load_module error propagation
// ===========================================================================

TEST_F(ShaderCacheTest, LoadModuleError_PropagatedFromGet) {
    load_result = std::unexpected(ShaderCacheError::create_module_failed("naga parse error"));
    auto id     = make_id(1);
    cache.set_shader(id, simple_wgsl());
    auto result = cache.get(CachedPipelineId{1}, id, {});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::CreateShaderModule>(result.error().data));
    EXPECT_EQ(std::get<ShaderCacheError::CreateShaderModule>(result.error().data).wgpu_message, "naga parse error");
}

// ===========================================================================
// AssetPath import resolution — dependency registered BEFORE main
// ===========================================================================

TEST_F(ShaderCacheTest, AssetPathImport_DependencyBeforeMain_GetSucceeds) {
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    // Dependency registered with AssetPath import_path (no #define_import_path)
    auto dep = Shader::from_wgsl("fn dep_fn() {}", "dep.wgsl");
    cache.set_shader(dep_id, dep);

    // Main shader imports dep via AssetPath (quoted path matches dep.wgsl)
    auto main_shader = Shader::from_wgsl("#import \"dep.wgsl\"\nfn main_fn() {}", "main.wgsl");
    cache.set_shader(main_id, main_shader);

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

// ===========================================================================
// AssetPath import resolution — dependency registered AFTER main (waiting_on_import)
// ===========================================================================

TEST_F(ShaderCacheTest, AssetPathImport_DependencyAfterMain_BlockedThenResolved) {
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    // Set main FIRST (before dependency is known)
    cache.set_shader(main_id, Shader::from_wgsl("#import \"dep.wgsl\"\nfn main_fn() {}", "main.wgsl"));

    // get() should fail — import not yet available
    auto blocked = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_FALSE(blocked.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderImportNotYetAvailable>(blocked.error().data));

    // Now register dependency with matching AssetPath
    cache.set_shader(dep_id, Shader::from_wgsl("fn dep_fn() {}", "dep.wgsl"));

    // get() should now succeed
    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST_F(ShaderCacheTest, AssetPathImport_DependencyUpdate_InvalidatesMainPipeline) {
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    cache.set_shader(dep_id, Shader::from_wgsl("fn dep_fn() {}", "dep.wgsl"));
    cache.set_shader(main_id, Shader::from_wgsl("#import \"dep.wgsl\"\nfn main_fn() {}", "main.wgsl"));

    cache.get(CachedPipelineId{5}, main_id, {});

    // Update dep → should invalidate main's pipeline 5
    auto affected = cache.set_shader(dep_id, Shader::from_wgsl("fn dep_fn_v2() {}", "dep.wgsl"));
    EXPECT_TRUE(std::ranges::contains(affected, CachedPipelineId{5}));
}

// ===========================================================================
// Shader defs merging: caller defs override shader's own defs
// ===========================================================================

TEST_F(ShaderCacheTest, ShaderOwnDefs_MergedWithCallerDefs_CallerWins) {
    auto id = make_id(1);
    // Shader has own_def = "VALUE=1"; caller overrides it with "VALUE=2"
    std::vector<ShaderDefVal> own_defs = {ShaderDefVal::from_int("VALUE", 1)};
    auto shader = Shader::from_wgsl_with_defs("#if VALUE == 2\nfn caller_wins() {}\n#endif\nfn always() {}\n",
                                              "test.wgsl", own_defs);
    cache.set_shader(id, shader);

    // Pass caller def that overrides: VALUE=2
    std::vector<ShaderDefVal> caller_defs = {ShaderDefVal::from_int("VALUE", 2)};
    auto result                           = cache.get(CachedPipelineId{1}, id, caller_defs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST_F(ShaderCacheTest, ShaderOwnDefs_AppliedWhenCallerProvidesNone) {
    auto id                            = make_id(1);
    std::vector<ShaderDefVal> own_defs = {ShaderDefVal::from_bool("OWN_FLAG")};
    auto shader = Shader::from_wgsl_with_defs("#ifdef OWN_FLAG\nfn own_path() {}\n#endif\n", "test.wgsl", own_defs);
    cache.set_shader(id, shader);

    auto result = cache.get(CachedPipelineId{1}, id, {});
    ASSERT_TRUE(result.has_value());
    // No error means compilation proceeded (OWN_FLAG applied → own_path included in output)
    EXPECT_EQ(call_count, 1);
}

// ===========================================================================
// remove dependency — main shader becomes blocked again
// ===========================================================================

TEST_F(ShaderCacheTest, RemoveDependency_MainShaderBlockedAgain) {
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    cache.set_shader(dep_id, Shader::from_wgsl("fn dep_fn() {}", "dep.wgsl"));
    cache.set_shader(main_id, Shader::from_wgsl("#import \"dep.wgsl\"\nfn main_fn() {}", "main.wgsl"));
    cache.get(CachedPipelineId{1}, main_id, {});

    cache.remove(dep_id);

    // Main is still registered but dep is gone → compose will fail
    auto result = cache.get(CachedPipelineId{2}, main_id, {});
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// SpirV shaders
// ===========================================================================

TEST_F(ShaderCacheTest, SpirvShader_GetSucceeds_NoImportsNeeded) {
    auto id                            = make_id(1);
    std::vector<std::uint8_t> bytecode = {0x03, 0x02, 0x23, 0x07, 0x00};
    cache.set_shader(id, Shader::from_spirv(bytecode, "shader.spv"));
    auto result = cache.get(CachedPipelineId{1}, id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST_F(ShaderCacheTest, SpirvShader_NoAssetPathImports_GetNeverBlocked) {
    auto id = make_id(1);
    // SpirV shaders have no imports by contract
    auto shader = Shader::from_spirv({0x01, 0x02}, "s.spv");
    EXPECT_TRUE(shader.imports.empty());
    cache.set_shader(id, shader);
    auto result = cache.get(CachedPipelineId{1}, id, {});
    ASSERT_TRUE(result.has_value());
}

// ===========================================================================
// Slang shaders
// ===========================================================================

// Build a simple Slang compute shader.
static Shader simple_slang(std::string src  = "[shader(\"compute\")]\n[numthreads(1,1,1)]\nvoid computeMain() {}",
                           std::string path = "test.slang") {
    return Shader::from_slang(std::move(src), std::move(path));
}

TEST_F(ShaderCacheTest, SlangShader_GetSucceeds) {
    auto id = make_id(1);
    cache.set_shader(id, simple_slang());
    auto result = cache.get(CachedPipelineId{1}, id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST_F(ShaderCacheTest, SlangShader_CacheHit_LoadModuleCalledOnce) {
    auto id = make_id(1);
    cache.set_shader(id, simple_slang());
    cache.get(CachedPipelineId{1}, id, {});
    cache.get(CachedPipelineId{2}, id, {});
    EXPECT_EQ(call_count, 1);
}

TEST_F(ShaderCacheTest, SlangShader_DifferentDefs_CacheMiss) {
    auto id                         = make_id(1);
    std::vector<ShaderDefVal> defsA = {ShaderDefVal::from_bool("FEAT_A")};
    std::vector<ShaderDefVal> defsB = {ShaderDefVal::from_bool("FEAT_B")};
    cache.set_shader(id, simple_slang());
    cache.get(CachedPipelineId{1}, id, defsA);
    cache.get(CachedPipelineId{2}, id, defsB);
    EXPECT_EQ(call_count, 2);
}

TEST_F(ShaderCacheTest, SlangShader_Update_InvalidatesPipeline) {
    auto id = make_id(1);
    cache.set_shader(id, simple_slang("[shader(\"compute\")]\n[numthreads(1,1,1)]\nvoid computeMain() { /* v1 */ }"));
    cache.get(CachedPipelineId{10}, id, {});
    auto affected = cache.set_shader(
        id, simple_slang("[shader(\"compute\")]\n[numthreads(1,1,1)]\nvoid computeMain() { /* v2 */ }"));
    EXPECT_TRUE(std::ranges::contains(affected, CachedPipelineId{10}));
}

TEST_F(ShaderCacheTest, SlangShader_OwnDefs_MergedWithCaller) {
    auto id     = make_id(1);
    auto shader = Shader::from_slang_with_defs("[shader(\"compute\")]\n[numthreads(1,1,1)]\nvoid computeMain() {}",
                                               "test.slang", {ShaderDefVal::from_int("VALUE", 1)});
    cache.set_shader(id, shader);
    // Caller overrides VALUE — different merged-defs → separate cache entry.
    std::vector<ShaderDefVal> caller_defs = {ShaderDefVal::from_int("VALUE", 2)};
    cache.get(CachedPipelineId{1}, id, {});           // uses shader's own defs only
    cache.get(CachedPipelineId{2}, id, caller_defs);  // caller overrides VALUE
    EXPECT_EQ(call_count, 2);                         // different merged defs → two compiles
}

TEST_F(ShaderCacheTest, SlangShader_ReturnsSamePointerForSameDefs) {
    auto id = make_id(1);
    cache.set_shader(id, simple_slang());
    auto r1 = cache.get(CachedPipelineId{1}, id, {});
    auto r2 = cache.get(CachedPipelineId{2}, id, {});
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1.value().get(), r2.value().get());
}

TEST_F(ShaderCacheTest, SlangShader_Import_ResolvesFromCache) {
    auto util_id = make_id(1);
    auto main_id = make_id(2);
    // Utility module — no entry point needed.
    cache.set_shader(util_id, Shader::from_slang("float4 getColor() { return float4(1, 0, 0, 1); }", "utility.slang"));
    // Main shader imports the utility module.
    cache.set_shader(main_id, Shader::from_slang("import utility;\n\n"
                                                 "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                 "void computeMain() { float4 c = getColor(); }",
                                                 "main.slang"));
    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

// ===========================================================================
// Slang — import resolved via import_path (AssetPath), not shader.path
// ===========================================================================

TEST_F(ShaderCacheTest, SlangShader_Import_ResolvedByImportPath_AssetPath) {
    auto util_id = make_id(1);
    auto main_id = make_id(2);

    // Utility module lives at an internal VFS path, but its import_path is "utility.slang"
    auto util = Shader::from_slang("float4 getColor() { return float4(1, 0, 0, 1); }", "vfs/internal/utility.slang");
    util.import_path = ShaderImport::asset_path("utility.slang");
    cache.set_shader(util_id, util);

    // Main shader does `import utility;` → Slang calls loadFile("utility.slang")
    cache.set_shader(main_id, Shader::from_slang("import utility;\n\n"
                                                 "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                 "void computeMain() { float4 c = getColor(); }",
                                                 "main.slang"));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST_F(ShaderCacheTest, SlangShader_Import_ResolvedByImportPath_Custom) {
    auto util_id = make_id(1);
    auto main_id = make_id(2);

    // Utility module with an explicit import_path (like `module utility;`)
    auto util        = Shader::from_slang("float4 getColor() { return float4(1, 0, 0, 1); }", "some/other/path.slang");
    util.import_path = ShaderImport::asset_path("utility.slang");
    cache.set_shader(util_id, util);

    // Main shader imports "utility" → loadFile("utility.slang") → matches custom import_path
    cache.set_shader(main_id, Shader::from_slang("import utility;\n\n"
                                                 "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                 "void computeMain() { float4 c = getColor(); }",
                                                 "main.slang"));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

// ===========================================================================
// WGSL — import resolved via explicit import_path (different from file path)
// ===========================================================================

TEST_F(ShaderCacheTest, WgslImport_ResolvedByExplicitAssetPath) {
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    // Dependency has a VFS file path but import_path is set to a short name
    auto dep        = Shader::from_wgsl("fn dep_fn() {}", "vfs/internal/dep.wgsl");
    dep.import_path = ShaderImport::asset_path("dep.wgsl");
    cache.set_shader(dep_id, dep);

    // Main shader imports "dep.wgsl" via AssetPath
    cache.set_shader(main_id, Shader::from_wgsl("#import \"dep.wgsl\"\nfn main_fn() {}", "main.wgsl"));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST_F(ShaderCacheTest, WgslImport_ResolvedByCustomImportPath) {
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    // Dependency with #define_import_path and a VFS file path
    auto dep = Shader::from_wgsl("#define_import_path my::utils\nfn dep_fn() {}", "vfs/dep.wgsl");
    // preprocess sets import_path = ShaderImport::custom("my::utils")
    cache.set_shader(dep_id, dep);

    // Main imports via custom path
    cache.set_shader(main_id, Shader::from_wgsl("#import my::utils\nfn main_fn() {}", "main.wgsl"));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST_F(ShaderCacheTest, WgslImport_AssetPath_DependencyAfterMain_Resolves) {
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    // Register main FIRST (dependency not yet available)
    cache.set_shader(main_id, Shader::from_wgsl("#import \"dep.wgsl\"\nfn main_fn() {}", "main.wgsl"));

    auto blocked = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_FALSE(blocked.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderImportNotYetAvailable>(blocked.error().data));

    // Now register dependency with explicit import_path (different from path)
    auto dep        = Shader::from_wgsl("fn dep_fn() {}", "vfs/deep/dep.wgsl");
    dep.import_path = ShaderImport::asset_path("dep.wgsl");
    cache.set_shader(dep_id, dep);

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

// ===========================================================================
// ShaderCacheError: factory methods
// ===========================================================================

TEST(ShaderCacheError, NotLoaded_HasCorrectId) {
    auto id  = make_id(7);
    auto err = ShaderCacheError::not_loaded(id);
    ASSERT_TRUE(std::holds_alternative<ShaderCacheError::ShaderNotLoaded>(err.data));
    EXPECT_EQ(std::get<ShaderCacheError::ShaderNotLoaded>(err.data).id, id);
}

TEST(ShaderCacheError, ImportNotAvailable_IsCorrectVariant) {
    auto err = ShaderCacheError::import_not_available();
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderImportNotYetAvailable>(err.data));
}

TEST(ShaderCacheError, CreateModuleFailed_StoresMessage) {
    auto err = ShaderCacheError::create_module_failed("webgpu error");
    ASSERT_TRUE(std::holds_alternative<ShaderCacheError::CreateShaderModule>(err.data));
    EXPECT_EQ(std::get<ShaderCacheError::CreateShaderModule>(err.data).wgpu_message, "webgpu error");
}

TEST(ShaderCacheError, ProcessError_IsCorrectVariant) {
    ComposeError ce{ComposeError::ImportNotFound{"missing"}};
    auto err = ShaderCacheError::process_error(ce);
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ProcessShaderError>(err.data));
}

// ===========================================================================
// CachedPipelineId
// ===========================================================================

TEST(CachedPipelineId, GetReturnsConstructedValue) { EXPECT_EQ(CachedPipelineId{42}.get(), 42u); }

TEST(CachedPipelineId, TwoDistinctIds_NotEqual) { EXPECT_NE(CachedPipelineId{1}, CachedPipelineId{2}); }

TEST(CachedPipelineId, SameValue_Equal) { EXPECT_EQ(CachedPipelineId{100}, CachedPipelineId{100}); }

// ===========================================================================
// Dual-name import: shader with custom name is also reachable by asset path
// ===========================================================================

class DualNameTest : public ::testing::Test {
   protected:
    int call_count{0};
    ShaderCache cache{null_device(),
                      [this](const wgpu::Device&,
                             const ShaderCacheSource&,
                             ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};
};

TEST_F(DualNameTest, Wgsl_ImportByAssetPath_WhenDepHasCustomName) {
    // dep.wgsl has #define_import_path my::utils — its import_path is Custom("my::utils")
    // But main.wgsl imports it by asset path: #import "dep.wgsl"
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    cache.set_shader(dep_id, Shader::from_wgsl("#define_import_path my::utils\nfn dep_fn() {}", "dep.wgsl"));
    cache.set_shader(main_id, Shader::from_wgsl("#import \"dep.wgsl\"\nfn main_fn() {}", "main.wgsl"));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value()) << "Should resolve dep by asset path even though dep has a custom import_path";
    EXPECT_EQ(call_count, 1);
}

TEST_F(DualNameTest, Wgsl_ImportByCustomName_WhenDepHasCustomName) {
    // Same dep, but main imports by custom name — should also work
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    cache.set_shader(dep_id, Shader::from_wgsl("#define_import_path my::utils\nfn dep_fn() {}", "dep.wgsl"));
    cache.set_shader(main_id, Shader::from_wgsl("#import my::utils\nfn main_fn() {}", "main.wgsl"));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST_F(DualNameTest, Wgsl_BothNamesWork_SameShader) {
    // Two main shaders import the same dep by different names — both should work
    auto dep_id   = make_id(1);
    auto main1_id = make_id(2);
    auto main2_id = make_id(3);

    cache.set_shader(dep_id, Shader::from_wgsl("#define_import_path my::utils\nfn dep_fn() {}", "dep.wgsl"));
    cache.set_shader(main1_id, Shader::from_wgsl("#import \"dep.wgsl\"\nfn main1() {}", "main1.wgsl"));
    cache.set_shader(main2_id, Shader::from_wgsl("#import my::utils\nfn main2() {}", "main2.wgsl"));

    auto r1 = cache.get(CachedPipelineId{1}, main1_id, {});
    auto r2 = cache.get(CachedPipelineId{2}, main2_id, {});
    ASSERT_TRUE(r1.has_value()) << "Import by asset path";
    ASSERT_TRUE(r2.has_value()) << "Import by custom name";
    EXPECT_EQ(call_count, 2);
}

TEST_F(DualNameTest, Wgsl_DualName_DependencyAfterMain_Resolves) {
    // Main registered first, dep comes later — both names should resolve waiters
    auto dep_id   = make_id(1);
    auto main1_id = make_id(2);
    auto main2_id = make_id(3);

    cache.set_shader(main1_id, Shader::from_wgsl("#import \"dep.wgsl\"\nfn main1() {}", "main1.wgsl"));
    cache.set_shader(main2_id, Shader::from_wgsl("#import my::utils\nfn main2() {}", "main2.wgsl"));

    // Both should be blocked
    EXPECT_FALSE(cache.get(CachedPipelineId{1}, main1_id, {}).has_value());
    EXPECT_FALSE(cache.get(CachedPipelineId{2}, main2_id, {}).has_value());

    // Register dep — should resolve both waiters
    cache.set_shader(dep_id, Shader::from_wgsl("#define_import_path my::utils\nfn dep_fn() {}", "dep.wgsl"));

    EXPECT_TRUE(cache.get(CachedPipelineId{1}, main1_id, {}).has_value());
    EXPECT_TRUE(cache.get(CachedPipelineId{2}, main2_id, {}).has_value());
}

TEST_F(DualNameTest, Slang_ImportByAssetPath_WhenDepHasModuleDecl) {
    // utility.slang has `module utility;` → import_path = AssetPath("utility.slang")
    // This is actually the same as the file path, so no dual name needed.
    // But let's test with a different internal path to verify dual-name works.
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    auto dep        = Shader::from_slang("float4 getColor() { return float4(1,0,0,1); }", "vfs/internal/util.slang");
    dep.import_path = ShaderImport::asset_path(AssetPath("utility.slang"));
    cache.set_shader(dep_id, dep);

    // Main imports "utility" → loadFile("utility.slang")
    cache.set_shader(main_id, Shader::from_slang("import utility;\n\n"
                                                 "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                 "void computeMain() { float4 c = getColor(); }",
                                                 "main.slang"));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST_F(DualNameTest, Remove_CleansUpBothNames) {
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);

    cache.set_shader(dep_id, Shader::from_wgsl("#define_import_path my::utils\nfn dep_fn() {}", "dep.wgsl"));
    cache.set_shader(main_id, Shader::from_wgsl("#import \"dep.wgsl\"\nfn main_fn() {}", "main.wgsl"));

    // Works before removal
    ASSERT_TRUE(cache.get(CachedPipelineId{1}, main_id, {}).has_value());

    // Remove dep — both names should be cleaned up
    cache.remove(dep_id);

    // Re-registering main should Block since dep is gone
    cache.set_shader(main_id, Shader::from_wgsl("#import \"dep.wgsl\"\nfn main_fn() {}", "main.wgsl"));
    EXPECT_FALSE(cache.get(CachedPipelineId{2}, main_id, {}).has_value());
}
