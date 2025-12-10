/**
 * @file epix.assets.cppm
 * @brief C++20 module interface for the asset management system.
 *
 * This module provides asset loading, storage, and management capabilities.
 */
module;

#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// Third-party headers that cannot be modularized
#include <uuid.h>

export module epix.assets;

export import epix.core;

export namespace epix::assets {

/**
 * @brief Visitor helper for std::variant.
 */
template <typename... Ts>
struct visitor : public Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
visitor(Ts...) -> visitor<Ts...>;

/**
 * @brief Index-based asset identifier with generation counter.
 */
struct AssetIndex {
   private:
    uint32_t _index      = 0;
    uint32_t _generation = 0;

   public:
    AssetIndex() = default;
    AssetIndex(uint32_t index, uint32_t generation) : _index(index), _generation(generation) {}

    uint32_t index() const { return _index; }
    uint32_t generation() const { return _generation; }

    bool operator==(const AssetIndex& other) const = default;
};

/// Invalid UUID constant for asset IDs
inline constexpr uuids::uuid INVALID_UUID = uuids::uuid::from_string("1038587c-0b8d-4f2e-8a3f-1a2b3c4d5e6f").value();

/**
 * @brief Typed asset identifier.
 * @tparam T The asset type.
 */
template <typename T>
struct AssetId : public std::variant<AssetIndex, uuids::uuid> {
    using std::variant<AssetIndex, uuids::uuid>::variant;

    static AssetId<T> invalid() { return AssetId<T>(INVALID_UUID); }

    bool operator==(const AssetId<T>& other) const {
        return ((const std::variant<AssetIndex, uuids::uuid>&)*this) ==
               ((const std::variant<AssetIndex, uuids::uuid>&)(other));
    }

    bool is_uuid() const { return std::holds_alternative<uuids::uuid>(*this); }
    bool is_index() const { return std::holds_alternative<AssetIndex>(*this); }

    std::string to_string() const {
        return std::format(
            "AssetId<{}>({})", epix::core::meta::type_id<T>::name(),
            std::visit(visitor{[](const AssetIndex& index) {
                                   return std::format("AssetIndex(index={}, generation={})", index.index(),
                                                      index.generation());
                               },
                               [](const uuids::uuid& id) { return std::format("UUID({})", uuids::to_string(id)); }},
                       *this));
    }
};

/**
 * @brief Untyped asset identifier with runtime type information.
 */
struct UntypedAssetId {
    std::variant<AssetIndex, uuids::uuid> id;
    epix::core::meta::type_index type;

    template <typename T>
    UntypedAssetId(const AssetId<T>& id) : id(id), type(epix::core::meta::type_id<T>{}) {}

    template <typename... Args>
    UntypedAssetId(const epix::core::meta::type_index& type, Args&&... args)
        : id(std::forward<Args>(args)...), type(type) {}

    template <typename T = void>
    static UntypedAssetId invalid(const epix::core::meta::type_index& type = epix::core::meta::type_id<T>{}) {
        return UntypedAssetId(type, INVALID_UUID);
    }

    template <typename T>
    std::optional<AssetId<T>> try_typed() const {
        if (type != epix::core::meta::type_id<T>{}) {
            return std::nullopt;
        }
        return std::make_optional<AssetId<T>>(id);
    }

    bool is_uuid() const { return std::holds_alternative<uuids::uuid>(id); }
    bool is_index() const { return std::holds_alternative<AssetIndex>(id); }
    const AssetIndex& index() const { return std::get<AssetIndex>(id); }
    const uuids::uuid& uuid() const { return std::get<uuids::uuid>(id); }
    bool operator==(const UntypedAssetId& other) const { return id == other.id && type == other.type; }
};

/**
 * @brief Handle to an asset.
 * @tparam T The asset type.
 */
template <typename T>
struct Handle {
   private:
    AssetId<T> _id;

   public:
    Handle() : _id(AssetId<T>::invalid()) {}
    Handle(const AssetId<T>& id) : _id(id) {}

    const AssetId<T>& id() const { return _id; }
    bool is_valid() const { return _id != AssetId<T>::invalid(); }

    bool operator==(const Handle<T>& other) const { return _id == other._id; }
};

/**
 * @brief Asset event types.
 */
template <typename T>
struct AssetEvent {
    enum class Type { Added, Modified, Removed, LoadedWithDependencies };
    Type type;
    AssetId<T> id;
};

/**
 * @brief Asset storage container.
 * @tparam T The asset type.
 */
template <typename T>
struct Assets {
    // Implementation details omitted - see header for full implementation
    static void handle_events(epix::core::ResMut<Assets<T>> assets);
    static void asset_events(epix::core::ResMut<Assets<T>> assets,
                             epix::core::EventWriter<AssetEvent<T>> writer);
};

/**
 * @brief Load context for asset loaders.
 */
struct LoadContext {
    std::filesystem::path asset_path;
};

/**
 * @brief Asset server for loading and managing assets.
 */
struct AssetServer {
    template <typename T>
    void register_assets(const Assets<T>& assets);

    template <typename T>
    void register_loader(const T& loader);

    template <typename T>
    Handle<T> load(const std::filesystem::path& path);

    static AssetServer from_world(epix::core::World& world);
};

/**
 * @brief Plugin for the asset system.
 */
struct AssetPlugin {
    template <typename T>
    AssetPlugin& register_asset();

    template <typename T>
    AssetPlugin& register_loader(const T& t = T());

    void build(epix::core::App& app);
    void finish(epix::core::App& app);
};

}  // namespace epix::assets

// Hash specializations
export template <typename T>
struct std::hash<epix::assets::AssetId<T>> {
    size_t operator()(const epix::assets::AssetId<T>& id) const {
        return std::visit(
            epix::assets::visitor{[](const epix::assets::AssetIndex& index) {
                                      return std::hash<uint64_t>()((static_cast<uint64_t>(index.index()) << 32) |
                                                                   static_cast<uint64_t>(index.generation()));
                                  },
                                  [](const uuids::uuid& id) { return std::hash<uuids::uuid>()(id); }},
            id);
    }
};

export template <>
struct std::hash<epix::assets::UntypedAssetId> {
    size_t operator()(const epix::assets::UntypedAssetId& id) const {
        size_t type_hash = std::hash<epix::core::meta::type_index>()(id.type);
        size_t id_hash   = std::visit(
            epix::assets::visitor{[](const epix::assets::AssetIndex& index) {
                                      return std::hash<uint64_t>()((static_cast<uint64_t>(index.index()) << 32) |
                                                                   static_cast<uint64_t>(index.generation()));
                                  },
                                  [](const uuids::uuid& id) { return std::hash<uuids::uuid>()(id); }},
            id.id);
        return type_hash ^ (id_hash + 0x9e3779b9 + (type_hash << 6) + (type_hash >> 2));
    }
};

export template <typename T>
struct std::hash<epix::assets::Handle<T>> {
    size_t operator()(const epix::assets::Handle<T>& handle) const {
        return std::hash<epix::assets::AssetId<T>>()(handle.id());
    }
};
