#pragma once

#include <epix/utils/core.h>
#include <uuid.h>

#include <filesystem>
#include <optional>
#include <typeindex>

#include "asset_id.h"
#include "index.h"

namespace epix::assets {
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

    EPIX_API StrongHandle(
        const UntypedAssetId& id,
        const Sender<DestructionEvent>& event_sender,
        bool loader_managed                              = false,
        const std::optional<std::filesystem::path>& path = std::nullopt
    );
    EPIX_API ~StrongHandle();
};
struct UntypedHandle;
template <typename T>
struct Handle {
   private:
    std::variant<std::shared_ptr<StrongHandle>, AssetId<T>> ref;

   public:
    Handle() : ref(std::shared_ptr<StrongHandle>()) {}
    Handle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
    Handle(const AssetId<T>& id) : ref(id) {}
    Handle(const uuids::uuid& id) : ref(AssetId<T>(id)) {}
    Handle(const UntypedHandle& handle);
    Handle(UntypedHandle&& handle);

    Handle(const Handle& other)            = default;
    Handle(Handle&& other)                 = default;
    Handle& operator=(const Handle& other) = default;
    Handle& operator=(Handle&& other)      = default;
    Handle& operator=(const UntypedHandle& other);
    Handle& operator=(UntypedHandle&& other);

    bool operator==(const Handle& other) const { return ref == other.ref; }
    bool is_strong() const {
        return std::holds_alternative<std::shared_ptr<StrongHandle>>(ref);
    }
    bool is_weak() const { return std::holds_alternative<AssetId<T>>(ref); }
    Handle<T> weak() const { return id(); }
    AssetId<T> id() const {
        return std::visit(
            epix::util::visitor{
                [](const std::shared_ptr<StrongHandle>& handle) {
                    return handle->id.typed<T>();
                },
                [](const AssetId<T>& index) { return index; }
            },
            ref
        );
    }
    operator AssetId<T>() const { return id(); }
};

struct UntypedHandle {
   private:
    std::variant<std::shared_ptr<StrongHandle>, UntypedAssetId> ref;

    template <typename T>
    UntypedHandle(
        const std::variant<std::shared_ptr<StrongHandle>, AssetId<T>>& ref
    )
        : ref(std::visit(
              epix::util::visitor{
                  [](const std::shared_ptr<StrongHandle>& handle) {
                      return handle;
                  },
                  [](const AssetId<T>& index) { return UntypedAssetId(index); }
              },
              ref
          )) {}
    EPIX_API UntypedHandle(
        const std::variant<std::shared_ptr<StrongHandle>, UntypedAssetId>& ref
    );

   public:
    EPIX_API UntypedHandle();
    EPIX_API UntypedHandle(const std::shared_ptr<StrongHandle>& handle);
    EPIX_API UntypedHandle(const UntypedAssetId& id);
    template <typename T>
    UntypedHandle(const Handle<T>& handle) : ref(handle.ref) {}

    UntypedHandle(const UntypedHandle&)            = default;
    UntypedHandle(UntypedHandle&&)                 = default;
    UntypedHandle& operator=(const UntypedHandle&) = default;
    UntypedHandle& operator=(UntypedHandle&&)      = default;

    EPIX_API bool operator==(const UntypedHandle& other) const;
    EPIX_API bool is_strong() const;
    EPIX_API bool is_weak() const;
    EPIX_API std::type_index type() const;
    EPIX_API const UntypedAssetId& id() const;
    EPIX_API operator const UntypedAssetId&() const;
    EPIX_API UntypedHandle weak() const;

    template <typename T>
    std::optional<Handle<T>> try_typed() const {
        if (type() != typeid(T)) {
            return std::nullopt;
        }
        return std::visit(
            epix::util::visitor{
                [](const std::shared_ptr<StrongHandle>& handle) {
                    return Handle<T>(handle);
                },
                [](const UntypedAssetId& id) {
                    return Handle<T>(id.typed<T>());
                }
            },
            ref
        );
    }
    template <typename T>
    Handle<T> typed() const {
        return try_typed<T>().value();
    }

    template <typename T>
    friend struct Handle;
};
template <typename T>
Handle<T>::Handle(const UntypedHandle& handle)
    : ref(std::visit(
          epix::util::visitor{
              [](const std::shared_ptr<StrongHandle>& strong_handle) {
                  return strong_handle;
              },
              [](const UntypedAssetId& id) { return id.typed<T>(); }
          },
          handle.ref
      )) {}
template <typename T>
Handle<T>::Handle(UntypedHandle&& handle)
    : ref(std::visit(
          epix::util::visitor{
              [](std::shared_ptr<StrongHandle>&& strong_handle) {
                  return std::move(strong_handle);
              },
              [](UntypedAssetId&& id) { return std::move(id).typed<T>(); }
          },
          std::move(handle.ref)
      )) {}
template <typename T>
Handle<T>& Handle<T>::operator=(const UntypedHandle& other) {
    ref = std::visit(
        epix::util::visitor{
            [](const std::shared_ptr<StrongHandle>& strong_handle) {
                return strong_handle;
            },
            [](const UntypedAssetId& id) { return id.typed<T>(); }
        },
        other.ref
    );
    return *this;
}
template <typename T>
Handle<T>& Handle<T>::operator=(UntypedHandle&& other) {
    ref = std::visit(
        epix::util::visitor{
            [](std::shared_ptr<StrongHandle>&& strong_handle) {
                return std::move(strong_handle);
            },
            [](UntypedAssetId&& id) { return std::move(id).typed<T>(); }
        },
        std::move(other.ref)
    );
    return *this;
}

struct HandleProvider {
    AssetIndexAllocator index_allocator;
    Sender<DestructionEvent> event_sender;
    Receiver<DestructionEvent> event_receiver;
    std::type_index type;

    EPIX_API HandleProvider(const std::type_index& type);
    HandleProvider(const HandleProvider&)            = delete;
    HandleProvider(HandleProvider&&)                 = delete;
    HandleProvider& operator=(const HandleProvider&) = delete;
    HandleProvider& operator=(HandleProvider&&)      = delete;

    EPIX_API UntypedHandle reserve();
    EPIX_API std::shared_ptr<StrongHandle> get_handle(
        const InternalAssetId& id,
        bool loader_managed,
        const std::optional<std::filesystem::path>& path
    );
    EPIX_API std::shared_ptr<StrongHandle> reserve(
        bool loader_managed, const std::optional<std::filesystem::path>& path
    );
};
}  // namespace epix::assets