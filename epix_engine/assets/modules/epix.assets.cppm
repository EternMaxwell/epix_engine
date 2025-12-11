/**
 * @file epix.assets.cppm
 * @brief Assets module for asset loading and management
 * 
 * This module provides the asset system including:
 * - Asset IDs (typed and untyped)
 * - Asset handles (strong and weak references)
 * - Asset storage and indexing
 * - Asset server for loading
 */

export module epix.assets;

// Standard library
#include <atomic>
#include <concepts>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

// Third-party
#include <uuid.h>
#include <spdlog/spdlog.h>

// Core module
#include <epix/core.hpp>
#include <epix/utils/async.h>

export namespace epix::assets {
    // Forward declarations
    struct AssetIndex;
    struct AssetIndexAllocator;
    struct StrongHandle;
    struct UntypedHandle;
    template <typename T> struct Handle;
    template <typename T> struct AssetId;
    struct UntypedAssetId;
    struct InternalAssetId;
    template <typename T> struct Entry;
    template <typename T> struct AssetStorage;
    template <typename T> struct Assets;
    struct AssetServer;
    struct HandleProvider;
    template <typename T> struct AssetEvent;
    struct AssetPlugin;
    
    // Utility visitor
    template <typename... Ts>
    struct visitor : public Ts... {
        using Ts::operator()...;
    };
    template <typename... Ts>
    visitor(Ts...) -> visitor<Ts...>;
    
    // Constants
    inline constexpr uuids::uuid INVALID_UUID = uuids::uuid::from_string("1038587c-0b8d-4f2e-8a3f-1a2b3c4d5e6f").value();
    
    // Using declarations from utils
    using epix::utils::async::Receiver;
    using epix::utils::async::Sender;
    
    // Asset Index - identifier for assets
    struct AssetIndex {
       private:
        uint32_t index_;
        uint32_t generation_;

       protected:
        AssetIndex(uint32_t index, uint32_t generation) : index_(index), generation_(generation) {}

       public:
        AssetIndex(const AssetIndex&)            = default;
        AssetIndex(AssetIndex&&)                 = default;
        AssetIndex& operator=(const AssetIndex&) = default;
        AssetIndex& operator=(AssetIndex&&)      = default;

        uint32_t index() const { return index_; }
        uint32_t generation() const { return generation_; }

        bool operator==(const AssetIndex& other) const = default;
        bool operator!=(const AssetIndex& other) const = default;

        friend struct StrongHandle;
        template <typename T>
        friend struct Handle;
        friend struct AssetIndexAllocator;
    };
    
    // Asset Index Allocator
    struct AssetIndexAllocator {
       private:
        std::atomic<uint32_t> m_next = 0;
        Sender<AssetIndex> m_free_indices_sender;
        Receiver<AssetIndex> m_free_indices_receiver;
        Receiver<AssetIndex> m_reserved;
        Sender<AssetIndex> m_reserved_sender;

       public:
        AssetIndexAllocator();
        AssetIndexAllocator(const AssetIndexAllocator&)            = delete;
        AssetIndexAllocator(AssetIndexAllocator&&)                 = delete;
        AssetIndexAllocator& operator=(const AssetIndexAllocator&) = delete;
        AssetIndexAllocator& operator=(AssetIndexAllocator&&)      = delete;

        AssetIndex reserve();
        void release(const AssetIndex& index);
        Receiver<AssetIndex> reserved_receiver() const;
    };
    
    // Typed Asset ID
    template <typename T>
    struct AssetId : public std::variant<AssetIndex, uuids::uuid> {
        static AssetId<T> invalid() { return AssetId<T>(INVALID_UUID); }

        bool operator==(const AssetId<T>& other) const {
            return ((const std::variant<AssetIndex, uuids::uuid>&)*this) ==
                   ((const std::variant<AssetIndex, uuids::uuid>&)(other));
        }
        bool operator==(const UntypedAssetId& other) const;

        bool is_uuid() const { return std::holds_alternative<uuids::uuid>(*this); }
        bool is_index() const { return std::holds_alternative<AssetIndex>(*this); }

        std::string to_string() const;
        std::string to_string_short() const;
    };
    
    // Untyped Asset ID
    struct UntypedAssetId {
        std::variant<AssetIndex, uuids::uuid> id;
        epix::meta::type_index type;

        template <typename T>
        UntypedAssetId(const AssetId<T>& id) : id(id), type(epix::meta::type_id<T>{}) {}
        
        template <typename... Args>
        UntypedAssetId(const epix::meta::type_index& type, Args&&... args) : id(std::forward<Args>(args)...), type(type) {}
        
        template <typename T = void>
        static UntypedAssetId invalid(const epix::meta::type_index& type = epix::meta::type_id<T>{});

        template <typename T>
        AssetId<T> typed() const;
        
        template <typename T>
        std::optional<AssetId<T>> try_typed() const;

        bool is_uuid() const { return std::holds_alternative<uuids::uuid>(id); }
        bool is_index() const { return std::holds_alternative<AssetIndex>(id); }
        const AssetIndex& index() const { return std::get<AssetIndex>(id); }
        const uuids::uuid& uuid() const { return std::get<uuids::uuid>(id); }
        bool operator==(const UntypedAssetId& other) const { return id == other.id && type == other.type; }
        std::string to_string() const;
        std::string to_string_short() const;
    };
    
    // Internal Asset ID
    struct InternalAssetId : std::variant<AssetIndex, uuids::uuid> {
        using std::variant<AssetIndex, uuids::uuid>::variant;
        template <typename T>
        InternalAssetId(const AssetId<T>& id) : std::variant<AssetIndex, uuids::uuid>(id) {}
        InternalAssetId(const UntypedAssetId& id) : std::variant<AssetIndex, uuids::uuid>(id.id) {}

        UntypedAssetId untyped(const epix::meta::type_index& type) const;
        template <typename T>
        AssetId<T> typed() const;
    };
    
    // Strong Handle
    struct DestructionEvent {
        InternalAssetId id;
    };
    
    struct NonCopyNonMove {
        NonCopyNonMove()                                 = default;
        NonCopyNonMove(const NonCopyNonMove&)            = delete;
        NonCopyNonMove(NonCopyNonMove&&)                 = delete;
        NonCopyNonMove& operator=(const NonCopyNonMove&) = delete;
        NonCopyNonMove& operator=(NonCopyNonMove&&)      = delete;
    };
    
    struct StrongHandle : NonCopyNonMove {
        UntypedAssetId id;
        Sender<DestructionEvent> event_sender;
        std::optional<std::filesystem::path> path;
        bool loader_managed;

        StrongHandle(const UntypedAssetId& id,
                     const Sender<DestructionEvent>& event_sender,
                     bool loader_managed                              = false,
                     const std::optional<std::filesystem::path>& path = std::nullopt);
        ~StrongHandle();
    };
    
    // Typed Handle
    template <typename T>
    struct Handle {
       private:
        std::variant<std::shared_ptr<StrongHandle>, AssetId<T>> ref;

       public:
        Handle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
        Handle(const AssetId<T>& id) : ref(id) {}
        Handle(const uuids::uuid& id) : ref(AssetId<T>(id)) {}
        Handle(const UntypedHandle& handle);
        Handle(UntypedHandle&& handle);

        Handle()                               = delete;
        Handle(const Handle& other)            = default;
        Handle(Handle&& other)                 = default;
        Handle& operator=(const Handle& other) = default;
        Handle& operator=(Handle&& other)      = default;

        Handle& operator=(const UntypedHandle& other);
        Handle& operator=(UntypedHandle&& other);
        Handle& operator=(const AssetId<T>& id);
        Handle& operator=(const std::shared_ptr<StrongHandle>& handle);

        bool operator==(const Handle& other) const { return ref == other.ref; }
        bool is_strong() const;
        bool is_weak() const;
        Handle<T> weak() const { return id(); }
        AssetId<T> id() const;
        operator AssetId<T>() const { return id(); }
    };
    
    // Untyped Handle
    struct UntypedHandle {
       private:
        std::variant<std::shared_ptr<StrongHandle>, UntypedAssetId> ref;

       public:
        UntypedHandle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
        UntypedHandle(const UntypedAssetId& id) : ref(id) {}
        template <typename T>
        UntypedHandle(const Handle<T>& handle);

        UntypedHandle()                                = delete;
        UntypedHandle(const UntypedHandle&)            = default;
        UntypedHandle(UntypedHandle&&)                 = default;
        UntypedHandle& operator=(const UntypedHandle&) = default;
        UntypedHandle& operator=(UntypedHandle&&)      = default;

        UntypedHandle& operator=(const std::shared_ptr<StrongHandle>& handle);
        UntypedHandle& operator=(const UntypedAssetId& id);

        bool operator==(const UntypedHandle& other) const { return ref == other.ref; }
        bool is_strong() const;
        bool is_weak() const;
        epix::meta::type_index type() const;
        UntypedAssetId id() const;
        operator UntypedAssetId() const { return id(); }
        UntypedHandle weak() const { return id(); }

        template <typename T>
        std::optional<Handle<T>> try_typed() const;
        
        template <typename T>
        Handle<T> typed() const;
    };
    
    // Handle Provider
    struct HandleProvider {
        AssetIndexAllocator index_allocator;
        Sender<DestructionEvent> event_sender;
        Receiver<DestructionEvent> event_receiver;
        epix::meta::type_index type;

        HandleProvider(const epix::meta::type_index& type);
        HandleProvider(const HandleProvider&)            = delete;
        HandleProvider(HandleProvider&&)                 = delete;
        HandleProvider& operator=(const HandleProvider&) = delete;
        HandleProvider& operator=(HandleProvider&&)      = delete;

        UntypedHandle reserve();
        std::shared_ptr<StrongHandle> get_handle(const InternalAssetId& id,
                                                 bool loader_managed,
                                                 const std::optional<std::filesystem::path>& path);
        std::shared_ptr<StrongHandle> reserve(bool loader_managed, const std::optional<std::filesystem::path>& path);
    };
    
    // Asset Storage Entry
    template <typename T>
    struct Entry {
        std::optional<T> asset = std::nullopt;
        uint32_t generation    = 0;
    };
    
    // Asset Errors
    struct IndexOutOfBound {
        uint32_t index;
    };
    struct SlotEmpty {
        uint32_t index;
    };
    struct GenMismatch {
        uint32_t index;
        uint32_t current_gen;
        uint32_t expected_gen;
    };
    using AssetNotPresent = std::variant<AssetIndex, uuids::uuid>;
    using AssetError = std::variant<IndexOutOfBound, SlotEmpty, GenMismatch, AssetNotPresent>;
    
    // Asset Storage
    template <typename T>
    struct AssetStorage {
       private:
        std::vector<std::optional<Entry<T>>> m_storage;
        uint32_t m_size;

       public:
        AssetStorage() : m_size(0) {}
        AssetStorage(const AssetStorage&)            = delete;
        AssetStorage(AssetStorage&&)                 = delete;
        AssetStorage& operator=(const AssetStorage&) = delete;
        AssetStorage& operator=(AssetStorage&&)      = delete;

        uint32_t size() const { return m_size; }
        bool empty() const { return m_size == 0; }

        void resize_slots(uint32_t index);
        std::expected<Entry<T>*, AssetError> get_entry(const AssetIndex& index);
        std::expected<const Entry<T>*, AssetError> get_entry(const AssetIndex& index) const;
        
        template <typename... Args>
            requires std::constructible_from<T, Args...>
        std::expected<bool, AssetError> insert(const AssetIndex& index, Args&&... args);
        
        std::expected<T, AssetError> remove(const AssetIndex& index);
        std::expected<T*, AssetError> get(const AssetIndex& index);
        std::expected<const T*, AssetError> get(const AssetIndex& index) const;
    };
    
    // Assets container
    template <typename T>
    struct Assets {
        HandleProvider handle_provider;
        AssetStorage<T> storage;
        std::unordered_map<uuids::uuid, AssetIndex> uuid_map;
        
        Assets();
        
        Handle<T> add(T asset);
        Handle<T> add(uuids::uuid uuid, T asset);
        std::expected<T*, AssetError> get(const AssetId<T>& id);
        std::expected<const T*, AssetError> get(const AssetId<T>& id) const;
        std::expected<T*, AssetError> get(const Handle<T>& handle);
        std::expected<const T*, AssetError> get(const Handle<T>& handle) const;
        bool contains(const AssetId<T>& id) const;
        std::expected<T, AssetError> remove(const AssetId<T>& id);
        
        static void handle_events(/* params */);
        static void asset_events(/* params */);
    };
    
    // Asset Event
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
    
    // Load Context
    struct LoadContext {
        std::filesystem::path base_path;
    };
    
    // Asset Server
    struct AssetServer {
        std::filesystem::path asset_root;
        // Internal implementation details...
        
        AssetServer(std::filesystem::path root = "assets");
        
        template <typename T, typename L>
        void register_loader(const L& loader);
        
        template <typename T>
        void register_assets(const Assets<T>& assets);
        
        template <typename T>
        Handle<T> load(const std::filesystem::path& path);
    };
    
    // Asset Plugin
    struct AssetPlugin {
       private:
        std::vector<std::function<void(epix::App&)>> m_assets_inserts;

       public:
        template <typename T>
        AssetPlugin& register_asset();
        
        template <typename T>
        AssetPlugin& register_loader(const T& t = T());
        
        void build(epix::App& app);
        void finish(epix::App& app);
    };
    
}  // namespace epix::assets

// Hash specializations
export namespace std {
    template <typename T>
    struct hash<epix::assets::AssetId<T>> {
        size_t operator()(const epix::assets::AssetId<T>& id) const;
    };
    
    template <>
    struct hash<epix::assets::UntypedAssetId> {
        size_t operator()(const epix::assets::UntypedAssetId& id) const;
    };
    
    // Formatter support
    template <typename T>
    struct formatter<epix::assets::AssetId<T>> {
        template <typename ParseContext>
        constexpr auto parse(ParseContext& ctx) {
            return ctx.begin();
        }

        template <typename FormatContext>
        auto format(const epix::assets::AssetId<T>& id, FormatContext& ctx) const;
    };
    
    template <>
    struct formatter<epix::assets::UntypedAssetId> {
        template <typename ParseContext>
        constexpr auto parse(ParseContext& ctx) {
            return ctx.begin();
        }

        template <typename FormatContext>
        auto format(const epix::assets::UntypedAssetId& id, FormatContext& ctx) const;
    };
}  // namespace std
