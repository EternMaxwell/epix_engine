module;
module epix.assets;

import std;

namespace epix::assets {

void EmbeddedAssetRegistry::insert_asset(const std::filesystem::path& asset_path, std::span<const std::byte> data) {
    auto val = memory::Value::from_shared(std::make_shared<std::vector<std::byte>>(data.begin(), data.end()));
    (void)m_dir.insert_file(asset_path, std::move(val));
}

void EmbeddedAssetRegistry::insert_asset_static(const std::filesystem::path& asset_path,
                                                std::span<const std::byte> data) {
    auto val = memory::Value::from_span(data);
    (void)m_dir.insert_file(asset_path, std::move(val));
}

void EmbeddedAssetRegistry::insert_meta(const std::filesystem::path& asset_path, std::span<const std::byte> meta_data) {
    auto meta_path = std::filesystem::path(asset_path.string() + ".meta");
    auto val = memory::Value::from_shared(std::make_shared<std::vector<std::byte>>(meta_data.begin(), meta_data.end()));
    (void)m_dir.insert_file(meta_path, std::move(val));
}

void EmbeddedAssetRegistry::register_source(AssetSourceBuilders& sources) {
    auto dir = m_dir;
    sources.insert(AssetSourceId(std::string(EMBEDDED)),
                   AssetSourceBuilder::create([dir]() -> std::unique_ptr<AssetReader> {
                       return std::make_unique<MemoryAssetReader>(dir);
                   }).with_processed_reader([dir]() -> std::unique_ptr<AssetReader> {
                       // Embedded assets are pre-compiled and treated as pre-processed.
                       // The processed reader reads from the same in-memory directory.
                       return std::make_unique<MemoryAssetReader>(dir);
                   }));
}

}  // namespace epix::assets
