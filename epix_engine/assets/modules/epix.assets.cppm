/**
 * @file epix.assets.cppm
 * @brief Assets module for asset loading and management
 */

export module epix.assets;

// Standard library includes
#include <array>
#include <atomic>
#include <concepts>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

// Module imports
#include <epix/core.hpp>

export namespace epix::assets {
    // Asset ID for unique identification
    template <typename T>
    struct AssetId {
        uint64_t id;
        
        AssetId() : id(0) {}
        explicit AssetId(uint64_t i) : id(i) {}
        
        bool operator==(const AssetId& other) const { return id == other.id; }
        bool operator!=(const AssetId& other) const { return id != other.id; }
    };
    
    // Asset handle for referencing assets
    template <typename T>
    struct Handle {
        AssetId<T> id;
        
        Handle() = default;
        explicit Handle(AssetId<T> i) : id(i) {}
        
        bool is_valid() const { return id.id != 0; }
    };
    
    // Asset storage
    template <typename T>
    struct Assets {
        std::unordered_map<uint64_t, T> assets;
        std::atomic<uint64_t> next_id{1};
        
        Handle<T> add(T asset) {
            uint64_t id = next_id.fetch_add(1);
            assets.emplace(id, std::move(asset));
            return Handle<T>(AssetId<T>(id));
        }
        
        std::optional<T*> get(Handle<T> handle) {
            auto it = assets.find(handle.id.id);
            if (it != assets.end()) {
                return &it->second;
            }
            return std::nullopt;
        }
        
        std::optional<const T*> get(Handle<T> handle) const {
            auto it = assets.find(handle.id.id);
            if (it != assets.end()) {
                return &it->second;
            }
            return std::nullopt;
        }
        
        bool contains(Handle<T> handle) const {
            return assets.find(handle.id.id) != assets.end();
        }
        
        void remove(Handle<T> handle) {
            assets.erase(handle.id.id);
        }
        
        static void handle_events(/* params */);
        static void asset_events(/* params */);
    };
    
    // Asset event
    template <typename T>
    struct AssetEvent {
        enum class Type {
            Created,
            Modified,
            Removed,
        };
        
        Type type;
        Handle<T> handle;
    };
    
    // Load context
    struct LoadContext {
        std::filesystem::path base_path;
    };
    
    // Asset server
    struct AssetServer {
        std::filesystem::path asset_root;
        
        AssetServer(std::filesystem::path root = "assets") : asset_root(std::move(root)) {}
        
        template <typename T, typename L>
        void register_loader(const L& loader);
        
        template <typename T>
        void register_assets(const Assets<T>& assets);
        
        template <typename T>
        Handle<T> load(const std::filesystem::path& path);
    };
    
    // Asset plugin
    struct AssetPlugin {
       private:
        std::vector<std::function<void(epix::App&)>> m_assets_inserts;

       public:
        template <typename T>
        AssetPlugin& register_asset() {
            m_assets_inserts.push_back([](epix::App& app) {
                // Asset registration logic
            });
            return *this;
        }
        
        template <typename T>
        AssetPlugin& register_loader(const T& t = T()) {
            m_assets_inserts.push_back([t](epix::App& app) {
                // Loader registration logic
            });
            return *this;
        }
        
        void build(epix::App& app);
        void finish(epix::App& app);
    };
}  // namespace epix::assets

// Hash specializations
export namespace std {
    template <typename T>
    struct hash<epix::assets::AssetId<T>> {
        size_t operator()(const epix::assets::AssetId<T>& id) const {
            return std::hash<uint64_t>()(id.id);
        }
    };
    
    template <typename T>
    struct hash<epix::assets::Handle<T>> {
        size_t operator()(const epix::assets::Handle<T>& handle) const {
            return std::hash<epix::assets::AssetId<T>>()(handle.id);
        }
    };
}
