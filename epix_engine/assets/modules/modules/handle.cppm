module;

#include <cassert>

export module epix.assets:handle;

import std;

import :id;

namespace assets {
using core::Sender;
using core::Receiver;
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
export struct UntypedHandle;
export template <typename T>
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

    Handle& operator=(const AssetId<T>& id) {
        ref = id;
        return *this;
    }
    Handle& operator=(const std::shared_ptr<StrongHandle>& handle) {
        assert(handle != nullptr || handle->id.type == meta::type_id<T>{});
        if (!handle) {
            throw std::runtime_error("Cannot assign null StrongHandle to Handle.");
        }
        if (handle->id.type != meta::type_id<T>{}) {
            throw std::runtime_error(std::format("Cannot assign StrongHandle of type {} to Handle of type {}",
                                                 handle->id.type.short_name(), meta::type_id<T>::short_name()));
        }
        ref = handle;
        return *this;
    }

    bool operator==(const Handle& other) const { return ref == other.ref; }
    bool is_strong() const { return std::holds_alternative<std::shared_ptr<StrongHandle>>(ref); }
    bool is_weak() const { return std::holds_alternative<AssetId<T>>(ref); }
    Handle<T> weak() const { return id(); }
    AssetId<T> id() const {
        return std::visit(visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->id.typed<T>(); },
                                  [](const AssetId<T>& index) { return index; }},
                          ref);
    }
    operator AssetId<T>() const { return id(); }
};

export struct UntypedHandle {
   private:
    std::variant<std::shared_ptr<StrongHandle>, UntypedAssetId> ref;

    template <typename T>
    UntypedHandle(const std::variant<std::shared_ptr<StrongHandle>, AssetId<T>>& ref) {
        std::visit(visitor{[this](const std::shared_ptr<StrongHandle>& handle) { this->ref = handle; },
                           [this](const AssetId<T>& id) { this->ref = UntypedAssetId(id); }},
                   ref);
    }
    UntypedHandle(const std::variant<std::shared_ptr<StrongHandle>, UntypedAssetId>& ref) : ref(ref) {}

   public:
    UntypedHandle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
    UntypedHandle(const UntypedAssetId& id) : ref(id) {}
    template <typename T>
    UntypedHandle(const Handle<T>& handle) : ref(handle.ref) {}

    UntypedHandle()                                = delete;
    UntypedHandle(const UntypedHandle&)            = default;
    UntypedHandle(UntypedHandle&&)                 = default;
    UntypedHandle& operator=(const UntypedHandle&) = default;
    UntypedHandle& operator=(UntypedHandle&&)      = default;

    UntypedHandle& operator=(const std::shared_ptr<StrongHandle>& handle) {
        assert(handle != nullptr || handle->id.type == type());
        if (!handle) {
            throw std::runtime_error("Cannot assign null StrongHandle to Handle.");
        } else if (handle->id.type != type()) {
            throw std::runtime_error(std::format("Cannot assign StrongHandle of type {} to Handle of type {}",
                                                 handle->id.type.short_name(), type().short_name()));
        }
        ref = handle;
        return *this;
    }
    UntypedHandle& operator=(const UntypedAssetId& id) {
        ref = id;
        return *this;
    }

    bool operator==(const UntypedHandle& other) const { return ref == other.ref; }
    bool is_strong() const { return std::holds_alternative<std::shared_ptr<StrongHandle>>(ref); }
    bool is_weak() const { return std::holds_alternative<UntypedAssetId>(ref); }
    meta::type_index type() const {
        return std::visit(visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->id.type; },
                                  [](const UntypedAssetId& id) { return id.type; }},
                          ref);
    }
    UntypedAssetId id() const {
        return std::visit(visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->id; },
                                  [](const UntypedAssetId& id) { return id; }},
                          ref);
    }
    operator UntypedAssetId() const { return id(); }
    UntypedHandle weak() const { return id(); }

    template <typename T>
    std::optional<Handle<T>> try_typed() const {
        if (type() != meta::type_id<T>{}) {
            return std::nullopt;
        }
        return std::visit(visitor{[](const std::shared_ptr<StrongHandle>& handle) { return Handle<T>(handle); },
                                  [](const UntypedAssetId& id) { return Handle<T>(id.typed<T>()); }},
                          ref);
    }
    template <typename T>
    Handle<T> typed() const {
        return try_typed<T>().value();
    }

    template <typename T>
    friend struct Handle;
};
template <typename T>
Handle<T>::Handle(const UntypedHandle& handle) {
    if (handle.type() != meta::type_id<T>{}) {
        throw std::runtime_error(std::format("{} cannot be constructed from UntypedHandle of type {}",
                                             meta::type_id<T>::short_name(), handle.type().short_name()));
    }
    std::visit(visitor{[this](const std::shared_ptr<StrongHandle>& strong_handle) { ref = strong_handle; },
                       [this](const UntypedAssetId& id) { ref = id.typed<T>(); }},
               handle.ref);
}
template <typename T>
Handle<T>::Handle(UntypedHandle&& handle) {
    if (handle.type() != meta::type_id<T>{}) {
        throw std::runtime_error(std::format("{} cannot be constructed from UntypedHandle of type {}",
                                             meta::type_id<T>::short_name(), handle.type().short_name()));
    }
    std::visit(visitor{[this](std::shared_ptr<StrongHandle>&& strong_handle) { ref = std::move(strong_handle); },
                       [this](UntypedAssetId&& id) { ref = id.typed<T>(); }},
               std::move(handle.ref));
}
template <typename T>
Handle<T>& Handle<T>::operator=(const UntypedHandle& other) {
    if (other.type() != meta::type_id<T>{}) {
        throw std::runtime_error(std::format("{} cannot be constructed from UntypedHandle of type {}",
                                             meta::type_id<T>::short_name(), other.type().short_name()));
    }
    std::visit(visitor{[this](const std::shared_ptr<StrongHandle>& strong_handle) { ref = strong_handle; },
                       [this](const UntypedAssetId& id) { ref = id.typed<T>(); }},
               other.ref);
    return *this;
}
template <typename T>
Handle<T>& Handle<T>::operator=(UntypedHandle&& other) {
    if (other.type() != meta::type_id<T>{}) {
        throw std::runtime_error(std::format("{} cannot be constructed from UntypedHandle of type {}",
                                             meta::type_id<T>::short_name(), other.type().short_name()));
    }
    std::visit(visitor{[this](std::shared_ptr<StrongHandle>&& strong_handle) { ref = std::move(strong_handle); },
                       [this](UntypedAssetId&& id) { ref = id.typed<T>(); }},
               std::move(other.ref));
    return *this;
}

struct HandleProvider {
    AssetIndexAllocator index_allocator;
    Sender<DestructionEvent> event_sender;
    Receiver<DestructionEvent> event_receiver;
    meta::type_index type;

    HandleProvider(const meta::type_index& type);
    HandleProvider(const HandleProvider&)            = delete;
    HandleProvider(HandleProvider&&)                 = delete;
    HandleProvider& operator=(const HandleProvider&) = delete;
    HandleProvider& operator=(HandleProvider&&)      = delete;

    UntypedHandle reserve() const;
    std::shared_ptr<StrongHandle> get_handle(const InternalAssetId& id,
                                             bool loader_managed,
                                             const std::optional<std::filesystem::path>& path) const;
    std::shared_ptr<StrongHandle> reserve(bool loader_managed, const std::optional<std::filesystem::path>& path) const;
};
}  // namespace epix::assets