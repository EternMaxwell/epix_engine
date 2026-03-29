module;

export module epix.assets:io.embedded;

import std;
import epix.utils;

import :io.memory;
import :io.memory.asset;
import :io.source;
import :path;

namespace epix::assets {

/** @brief The canonical asset source name for embedded (in-binary) assets.
 *  Matches bevy_asset's EMBEDDED constant. */
export inline constexpr std::string_view EMBEDDED = "embedded";

/** @brief A registry for assets embedded directly in the application binary.
 *  Backed by an in-memory directory that serves as the reader for the "embedded" asset source.
 *  Matches bevy_asset's EmbeddedAssetRegistry. */
export struct EmbeddedAssetRegistry {
   private:
    memory::Directory m_dir;

   public:
    /** @brief Construct a new empty registry. */
    EmbeddedAssetRegistry() : m_dir(memory::Directory::create("")) {}

    /** @brief Insert asset data into the embedded registry.
     *  @param full_path  Full filesystem path of the asset (for identification).
     *  @param asset_path Path relative to the "embedded" source root.
     *  @param data       Raw bytes of the asset. */
    void insert_asset(const std::filesystem::path& full_path,
                      const std::filesystem::path& asset_path,
                      std::span<const std::byte> data) {
        auto val = memory::Value::from_shared(std::make_shared<std::vector<std::byte>>(data.begin(), data.end()));
        (void)m_dir.insert_file(asset_path, std::move(val));
    }

    /** @brief Insert asset data from a static/compile-time buffer (zero-copy view).
     *  @param full_path  Full filesystem path of the asset.
     *  @param asset_path Path relative to the "embedded" source root.
     *  @param data       Static byte span (must outlive the registry). */
    void insert_asset_static(const std::filesystem::path& full_path,
                             const std::filesystem::path& asset_path,
                             std::span<const std::byte> data) {
        auto val = memory::Value::from_span(data);
        (void)m_dir.insert_file(asset_path, std::move(val));
    }

    /** @brief Insert metadata for an embedded asset.
     *  @param full_path  Full filesystem path of the asset.
     *  @param asset_path Path relative to the "embedded" source root.
     *  @param meta_data  Raw bytes of the meta file. */
    void insert_meta(const std::filesystem::path& full_path,
                     const std::filesystem::path& asset_path,
                     std::span<const std::byte> meta_data) {
        auto meta_path = std::filesystem::path(asset_path.string() + ".meta");
        auto val =
            memory::Value::from_shared(std::make_shared<std::vector<std::byte>>(meta_data.begin(), meta_data.end()));
        (void)m_dir.insert_file(meta_path, std::move(val));
    }

    /** @brief Remove a previously inserted asset.
     *  @param asset_path Path relative to the "embedded" source root.
     *  @return true if the asset was removed, false if not found. */
    bool remove_asset(const std::filesystem::path& asset_path) { return m_dir.remove_file(asset_path).has_value(); }

    /** @brief Register this embedded registry as an asset source with the given source builders.
     *  Creates a "embedded" source backed by the in-memory directory. */
    void register_source(AssetSourceBuilders& sources) {
        auto dir = m_dir;
        sources.insert(AssetSourceId(std::string(EMBEDDED)),
                       AssetSourceBuilder::create([dir]() -> std::unique_ptr<AssetReader> {
                           return std::make_unique<MemoryAssetReader>(dir);
                       }));
    }

    /** @brief Get the underlying in-memory directory (for advanced use). */
    const memory::Directory& directory() const { return m_dir; }
    memory::Directory& directory() { return m_dir; }
};

}  // namespace assets
