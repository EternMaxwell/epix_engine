#include <gtest/gtest.h>

import std;
import epix.assets;
import epix.core;
import epix.shader;

using namespace epix::assets;
using namespace epix::core;
using namespace epix::shader;

namespace {

bool wait_for_loaded(App& app,
                     const AssetServer& server,
                     const UntypedAssetId& id,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        auto state = server.get_load_state(id);
        if (state.has_value() && std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state)) return false;
        if (server.is_loaded_with_dependencies(id)) return true;
        app.run_schedule(Last);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

}  // namespace

// ── Source::SlangIr predicates ──────────────────────────────────────────────

TEST(SlangIrSource, IsSlangIrTrue) {
    auto src = Source::slang_ir({0x42, 0xFF});
    EXPECT_TRUE(src.is_slang_ir());
}

TEST(SlangIrSource, OtherPredicatesFalse) {
    auto src = Source::slang_ir({1, 2, 3});
    EXPECT_FALSE(src.is_wgsl());
    EXPECT_FALSE(src.is_slang());
    EXPECT_FALSE(src.is_spirv());
}

TEST(SlangIrSource, BytesPreserved) {
    std::vector<std::uint8_t> bytes = {0xDE, 0xAD, 0xBE, 0xEF};
    auto src                        = Source::slang_ir(bytes);
    ASSERT_TRUE(src.is_slang_ir());
    const auto& stored = std::get<Source::SlangIr>(src.data).bytes;
    EXPECT_EQ(stored, bytes);
}

// ── Shader::from_slang_ir ───────────────────────────────────────────────────

TEST(SlangIrShader, PathPreserved) {
    std::vector<std::uint8_t> bytes = {1, 2, 3};
    AssetPath path("shaders/my_module.slang-module");
    auto shader = Shader::from_slang_ir(bytes, path);
    EXPECT_EQ(shader.path, path);
    EXPECT_TRUE(shader.source.is_slang_ir());
}

TEST(SlangIrShader, ImportsEmpty) {
    auto shader = Shader::from_slang_ir({0x00}, AssetPath("mod.slang-module"));
    EXPECT_TRUE(shader.imports.empty());
}

// ── ShaderLoader: raw .slang-module file ────────────────────────────────────

TEST(SlangIrLoader, LoadsRawSlangModuleFile) {
    auto source_dir                  = memory::Directory::create({});
    std::vector<std::uint8_t> raw_ir = {0x01, 0x02, 0x03, 0x04};
    std::vector<std::byte> raw_bytes(raw_ir.size());
    std::transform(raw_ir.begin(), raw_ir.end(), raw_bytes.begin(),
                   [](std::uint8_t b) { return static_cast<std::byte>(b); });
    (void)source_dir.insert_file(
        "my_module.slang-module",
        memory::Value::from_shared(std::make_shared<std::vector<std::byte>>(std::move(raw_bytes))));

    App app = App::create();
    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Unprocessed;
    plugin.register_asset_source(AssetSourceId{},
                                 AssetSourceBuilder::create([source_dir]() -> std::unique_ptr<AssetReader> {
                                     return std::make_unique<MemoryAssetReader>(source_dir);
                                 }));
    plugin.build(app);
    ShaderPlugin{}.build(app);

    app.run_schedule(Startup);
    auto& server = app.resource<AssetServer>();
    auto& assets = app.resource<Assets<Shader>>();
    auto handle  = server.load<Shader>(AssetPath("my_module.slang-module"));

    ASSERT_TRUE(wait_for_loaded(app, server, handle.id()));
    auto shader = assets.get(handle.id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_TRUE(shader->get().source.is_slang_ir());
    const auto& stored = std::get<Source::SlangIr>(shader->get().source.data).bytes;
    EXPECT_EQ(stored, raw_ir);
}

// ── Import path variants: asset-path import vs custom-name import ───────────

TEST(SlangIrShader, ImportPathDefaultsToAssetPath) {
    AssetPath path("utils/math.slang-module");
    auto shader = Shader::from_slang_ir({1}, path);
    EXPECT_TRUE(shader.import_path.is_asset_path());
    EXPECT_FALSE(shader.import_path.is_custom());
}

TEST(SlangIrShader, ImportPathCanBeOverriddenToCustomName) {
    AssetPath path("providers/utility_math.slang-module");
    auto shader        = Shader::from_slang_ir({1, 2}, path);
    shader.import_path = ShaderImport::custom(std::filesystem::path("utility/math"));
    EXPECT_TRUE(shader.import_path.is_custom());
    EXPECT_FALSE(shader.import_path.is_asset_path());
    EXPECT_EQ(shader.import_path.as_custom(), "utility/math");
}

TEST(SlangIrImportPath, AssetPathViaShaderImport) {
    AssetPath path("shaders/math.slang-module");
    auto shader = Shader::from_slang_ir({0x01}, path);
    EXPECT_TRUE(shader.import_path.is_asset_path());
    EXPECT_FALSE(shader.import_path.is_custom());
}

TEST(SlangIrImportPath, CustomNameViaShaderImport) {
    // A SlangIr shader can also be registered with a custom import name via
    // ShaderImport — verify the ShaderImport::custom factory works independently.
    auto imp = ShaderImport::custom(std::filesystem::path("utility/math"));
    EXPECT_TRUE(imp.is_custom());
    EXPECT_FALSE(imp.is_asset_path());
    EXPECT_EQ(imp.as_custom(), "utility/math");
}
