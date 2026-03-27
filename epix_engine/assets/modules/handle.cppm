module;

#include <cassert>

export module epix.assets:handle;

import std;

import :id;
import :path;

namespace assets {
using core::Receiver;
using core::Sender;
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
    std::optional<AssetPath> path;
    bool loader_managed;

    StrongHandle(const UntypedAssetId& id,
                 const Sender<DestructionEvent>& event_sender,
                 bool loader_managed                  = false,
                 const std::optional<AssetPath>& path = std::nullopt);
    ~StrongHandle();
};
/** @brief Forward declaration. */
export struct UntypedHandle;
/** @brief Typed handle to a loaded asset.
 *  A Handle is either *strong* (shared_ptr to StrongHandle, keeps the asset
 *  alive) or *weak* (holds only an AssetId, does not prevent unloading).
 *  @tparam T The concrete asset type. */
export template <typename T>
struct Handle {
   private:
    std::variant<std::shared_ptr<StrongHandle>, AssetId<T>> ref;

    friend struct UntypedHandle;

   public:
    /** @brief Construct a strong handle from a shared StrongHandle pointer. */
    Handle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
    /** @brief Construct a weak handle from an AssetId. */
    Handle(const AssetId<T>& id) : ref(id) {}
    /** @brief Construct a weak handle from a UUID. */
    Handle(const uuids::uuid& id) : ref(AssetId<T>(id)) {}
    /** @brief Construct from an UntypedHandle (throws on type mismatch). */
    Handle(const UntypedHandle& handle);
    /** @brief Move-construct from an UntypedHandle (throws on type mismatch). */
    Handle(UntypedHandle&& handle);

    Handle()                               = delete;
    Handle(const Handle& other)            = default;
    Handle(Handle&& other)                 = default;
    Handle& operator=(const Handle& other) = default;
    Handle& operator=(Handle&& other)      = default;

    /** @brief Assign from an UntypedHandle (throws on type mismatch). */
    Handle& operator=(const UntypedHandle& other);
    /** @brief Move-assign from an UntypedHandle (throws on type mismatch). */
    Handle& operator=(UntypedHandle&& other);

    /** @brief Assign from a bare AssetId, making this a weak handle. */
    Handle& operator=(const AssetId<T>& id) {
        ref = id;
        return *this;
    }
    /** @brief Assign from a StrongHandle pointer. */
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

    /** @brief Equality comparison between two handles. */
    bool operator==(const Handle& other) const { return ref == other.ref; }
    /** @brief Check if this is a strong (reference-counted) handle. */
    bool is_strong() const { return std::holds_alternative<std::shared_ptr<StrongHandle>>(ref); }
    /** @brief Check if this is a weak (id-only) handle. */
    bool is_weak() const { return std::holds_alternative<AssetId<T>>(ref); }
    /** @brief Return a weak copy that holds only the id. */
    Handle<T> weak() const { return id(); }
    /** @brief Get the underlying asset id regardless of strong/weak status. */
    AssetId<T> id() const {
        return std::visit(
            utils::visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->id.typed<T>(); },
                           [](const AssetId<T>& index) { return index; }},
            ref);
    }
    /** @brief Get the path associated with this asset, if available (only for strong handles). */
    std::optional<AssetPath> path() const {
        return std::visit(utils::visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->path; },
                                         [](const AssetId<T>&) -> std::optional<AssetPath> { return std::nullopt; }},
                          ref);
    }
    /** @brief Convert this typed handle to an UntypedHandle. */
    UntypedHandle untyped() const;
    /** @brief Implicit conversion to the underlying AssetId. */
    operator AssetId<T>() const { return id(); }
};

/** @brief Type-erased handle to an asset of any type.
 *  Works like Handle<T> but stores a type_index at runtime.
 *  Can be converted to Handle<T> via try_typed() / typed(). */
export struct UntypedHandle {
   private:
    std::variant<std::shared_ptr<StrongHandle>, UntypedAssetId> ref;

    template <typename T>
    UntypedHandle(const std::variant<std::shared_ptr<StrongHandle>, AssetId<T>>& ref) {
        std::visit(utils::visitor{[this](const std::shared_ptr<StrongHandle>& handle) { this->ref = handle; },
                                  [this](const AssetId<T>& id) { this->ref = UntypedAssetId(id); }},
                   ref);
    }
    UntypedHandle(const std::variant<std::shared_ptr<StrongHandle>, UntypedAssetId>& ref) : ref(ref) {}

   public:
    /** @brief Construct a strong untyped handle from a StrongHandle pointer. */
    UntypedHandle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
    /** @brief Construct a weak untyped handle from an UntypedAssetId. */
    UntypedHandle(const UntypedAssetId& id) : ref(id) {}
    /** @brief Construct from a typed Handle, erasing the type. */
    template <typename T>
    UntypedHandle(const Handle<T>& handle) : UntypedHandle(handle.ref) {}

    UntypedHandle()                                = delete;
    UntypedHandle(const UntypedHandle&)            = default;
    UntypedHandle(UntypedHandle&&)                 = default;
    UntypedHandle& operator=(const UntypedHandle&) = default;
    UntypedHandle& operator=(UntypedHandle&&)      = default;

    /** @brief Assign from a StrongHandle pointer. */
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
    /** @brief Assign from an UntypedAssetId, making this a weak handle. */
    UntypedHandle& operator=(const UntypedAssetId& id) {
        ref = id;
        return *this;
    }

    /** @brief Equality comparison. */
    bool operator==(const UntypedHandle& other) const { return ref == other.ref; }
    /** @brief Check if this is a strong (reference-counted) handle. */
    bool is_strong() const { return std::holds_alternative<std::shared_ptr<StrongHandle>>(ref); }
    /** @brief Check if this is a weak (id-only) handle. */
    bool is_weak() const { return std::holds_alternative<UntypedAssetId>(ref); }
    /** @brief Get the runtime type_index of the asset this handle refers to. */
    meta::type_index type() const {
        return std::visit(utils::visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->id.type; },
                                         [](const UntypedAssetId& id) { return id.type; }},
                          ref);
    }
    /** @brief Get the type-erased id. */
    UntypedAssetId id() const {
        return std::visit(utils::visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->id; },
                                         [](const UntypedAssetId& id) { return id; }},
                          ref);
    }
    /** @brief Get the path associated with this asset, if available (only for strong handles). */
    std::optional<AssetPath> path() const {
        return std::visit(
            utils::visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->path; },
                           [](const UntypedAssetId&) -> std::optional<AssetPath> { return std::nullopt; }},
            ref);
    }
    /** @brief Implicit conversion to UntypedAssetId. */
    operator UntypedAssetId() const { return id(); }
    /** @brief Return a weak copy holding only the id. */
    UntypedHandle weak() const { return id(); }

    /** @brief Try to downcast to a typed Handle.
     *  @tparam T Expected asset type.
     *  @return The typed handle, or std::nullopt on type mismatch. */
    template <typename T>
    std::optional<Handle<T>> try_typed() const {
        if (type() != meta::type_id<T>{}) {
            return std::nullopt;
        }
        return std::visit(utils::visitor{[](const std::shared_ptr<StrongHandle>& handle) { return Handle<T>(handle); },
                                         [](const UntypedAssetId& id) { return Handle<T>(id.typed<T>()); }},
                          ref);
    }
    /** @brief Downcast to a typed Handle. Throws on type mismatch.
     *  @tparam T Expected asset type. */
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
    std::visit(utils::visitor{[this](const std::shared_ptr<StrongHandle>& strong_handle) { ref = strong_handle; },
                              [this](const UntypedAssetId& id) { ref = id.typed<T>(); }},
               handle.ref);
}
template <typename T>
UntypedHandle Handle<T>::untyped() const {
    return UntypedHandle(*this);
}
template <typename T>
Handle<T>::Handle(UntypedHandle&& handle) {
    if (handle.type() != meta::type_id<T>{}) {
        throw std::runtime_error(std::format("{} cannot be constructed from UntypedHandle of type {}",
                                             meta::type_id<T>::short_name(), handle.type().short_name()));
    }
    std::visit(utils::visitor{[this](std::shared_ptr<StrongHandle>&& strong_handle) { ref = std::move(strong_handle); },
                              [this](UntypedAssetId&& id) { ref = id.typed<T>(); }},
               std::move(handle.ref));
}
template <typename T>
Handle<T>& Handle<T>::operator=(const UntypedHandle& other) {
    if (other.type() != meta::type_id<T>{}) {
        throw std::runtime_error(std::format("{} cannot be constructed from UntypedHandle of type {}",
                                             meta::type_id<T>::short_name(), other.type().short_name()));
    }
    std::visit(utils::visitor{[this](const std::shared_ptr<StrongHandle>& strong_handle) { ref = strong_handle; },
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
    std::visit(utils::visitor{[this](std::shared_ptr<StrongHandle>&& strong_handle) { ref = std::move(strong_handle); },
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
                                             const std::optional<AssetPath>& path) const;
    std::shared_ptr<StrongHandle> reserve(bool loader_managed, const std::optional<AssetPath>& path) const;
};
}  // namespace assets