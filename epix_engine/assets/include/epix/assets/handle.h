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

    StrongHandle(
        const UntypedAssetId& id,
        const Sender<DestructionEvent>& event_sender,
        bool loader_managed                              = false,
        const std::optional<std::filesystem::path>& path = std::nullopt
    )
        : id(id),
          event_sender(event_sender),
          path(path),
          loader_managed(loader_managed) {}

    ~StrongHandle() {
        if (event_sender) {
            event_sender.send(id);
        }
    }
};
template <typename T>
struct Handle {
   private:
    std::variant<std::shared_ptr<StrongHandle>, AssetId<T>> ref;

   public:
    Handle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
    Handle(const AssetId<T>& id) : ref(id) {}
    Handle(const uuids::uuid& id) : ref(AssetId<T>(id)) {}

    Handle(const Handle& other)            = default;
    Handle(Handle&& other)                 = default;
    Handle& operator=(const Handle& other) = default;
    Handle& operator=(Handle&& other)      = default;

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
    UntypedHandle(
        const std::variant<std::shared_ptr<StrongHandle>, UntypedAssetId>& ref
    )
        : ref(ref) {}

   public:
    UntypedHandle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
    UntypedHandle(const UntypedAssetId& id) : ref(id) {}
    template <typename T>
    UntypedHandle(const Handle<T>& handle) : ref(handle.ref) {}

    UntypedHandle(const UntypedHandle&)            = default;
    UntypedHandle(UntypedHandle&&)                 = default;
    UntypedHandle& operator=(const UntypedHandle&) = default;
    UntypedHandle& operator=(UntypedHandle&&)      = default;

    bool is_strong() const {
        return std::holds_alternative<std::shared_ptr<StrongHandle>>(ref);
    }
    bool is_weak() const { return std::holds_alternative<UntypedAssetId>(ref); }
    std::type_index type() const {
        return std::visit(
            epix::util::visitor{
                [](const std::shared_ptr<StrongHandle>& handle) {
                    return handle->id.type;
                },
                [](const UntypedAssetId& id) { return id.type; }
            },
            ref
        );
    }

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

    const UntypedAssetId& id() const {
        return std::visit(
            epix::util::visitor{
                [](const std::shared_ptr<StrongHandle>& handle
                ) -> const UntypedAssetId& { return handle->id; },
                [](const UntypedAssetId& id) -> const UntypedAssetId& {
                    return id;
                }
            },
            ref
        );
    }

    operator const UntypedAssetId&() const { return id(); }

    UntypedHandle weak() const {
        return std::visit(
            epix::util::visitor{
                [](const std::shared_ptr<StrongHandle>& handle) {
                    return handle->id;
                },
                [](const UntypedAssetId& id) { return id; }
            },
            ref
        );
    }
};

struct HandleProvider {
    AssetIndexAllocator index_allocator;
    Sender<DestructionEvent> event_sender;
    Receiver<DestructionEvent> event_receiver;
    std::type_index type;

    HandleProvider(const std::type_index& type)
        : type(type),
          event_receiver(
              std::get<1>(epix::utils::async::make_channel<DestructionEvent>())
          ) {
        event_sender = event_receiver.create_sender();
    }
    HandleProvider(const HandleProvider&)            = delete;
    HandleProvider(HandleProvider&&)                 = delete;
    HandleProvider& operator=(const HandleProvider&) = delete;
    HandleProvider& operator=(HandleProvider&&)      = delete;

    UntypedHandle reserve() {
        auto index = index_allocator.reserve();
        return std::make_shared<StrongHandle>(
            UntypedAssetId(type, index), event_sender, false, std::nullopt
        );
    }
    std::shared_ptr<StrongHandle> get_handle(
        const InternalAssetId& id,
        bool loader_managed,
        const std::optional<std::filesystem::path>& path
    ) {
        return std::make_shared<StrongHandle>(
            id.untyped(type), event_sender, loader_managed, path
        );
    }
    std::shared_ptr<StrongHandle> reserve(
        bool loader_managed, const std::optional<std::filesystem::path>& path
    ) {
        auto index = index_allocator.reserve();
        return get_handle(index, loader_managed, path);
    }
};
}  // namespace epix::assets