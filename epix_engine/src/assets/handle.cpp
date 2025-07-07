#include "epix/assets/handle.h"

using namespace epix::assets;

EPIX_API StrongHandle::StrongHandle(
    const UntypedAssetId& id,
    const Sender<DestructionEvent>& event_sender,
    bool loader_managed,
    const std::optional<std::filesystem::path>& path
)
    : id(id),
      event_sender(event_sender),
      path(path),
      loader_managed(loader_managed) {}

EPIX_API StrongHandle::~StrongHandle() {
    event_sender.send(DestructionEvent{id});
}

EPIX_API UntypedHandle::UntypedHandle()
    : ref(std::shared_ptr<StrongHandle>()) {}
EPIX_API UntypedHandle::UntypedHandle(
    const std::variant<std::shared_ptr<StrongHandle>, UntypedAssetId>& ref
)
    : ref(ref) {}
EPIX_API UntypedHandle::UntypedHandle(
    const std::shared_ptr<StrongHandle>& handle
)
    : ref(handle) {}
EPIX_API UntypedHandle::UntypedHandle(const UntypedAssetId& id) : ref(id) {}
EPIX_API UntypedHandle& UntypedHandle::operator=(
    const std::shared_ptr<StrongHandle>& handle
) {
    ref = handle;
    return *this;
}
EPIX_API UntypedHandle& UntypedHandle::operator=(const UntypedAssetId& id) {
    ref = id;
    return *this;
}

EPIX_API void UntypedHandle::reset() { ref = std::shared_ptr<StrongHandle>(); }

EPIX_API bool UntypedHandle::operator==(const UntypedHandle& other) const {
    return ref == other.ref;
}
EPIX_API bool UntypedHandle::is_strong() const {
    return std::holds_alternative<std::shared_ptr<StrongHandle>>(ref);
}
EPIX_API bool UntypedHandle::is_weak() const {
    return std::holds_alternative<UntypedAssetId>(ref);
}

EPIX_API UntypedHandle UntypedHandle::weak() const {
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
EPIX_API epix::meta::type_index UntypedHandle::type() const {
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
EPIX_API const UntypedAssetId& UntypedHandle::id() const {
    return std::visit(
        epix::util::visitor{
            [](const std::shared_ptr<StrongHandle>& handle
            ) -> const UntypedAssetId& { return handle->id; },
            [](const UntypedAssetId& id) -> const UntypedAssetId& { return id; }
        },
        ref
    );
}
EPIX_API UntypedHandle::operator const UntypedAssetId&() const { return id(); }

EPIX_API HandleProvider::HandleProvider(const epix::meta::type_index& type)
    : type(type) {
    std::tie(event_sender, event_receiver) =
        epix::utils::async::make_channel<DestructionEvent>();
}

EPIX_API UntypedHandle HandleProvider::reserve() {
    auto index = index_allocator.reserve();
    return std::make_shared<StrongHandle>(
        UntypedAssetId(type, index), event_sender, false, std::nullopt
    );
}
EPIX_API std::shared_ptr<StrongHandle> HandleProvider::get_handle(
    const InternalAssetId& id,
    bool loader_managed,
    const std::optional<std::filesystem::path>& path
) {
    return std::make_shared<StrongHandle>(
        id.untyped(type), event_sender, loader_managed, path
    );
}
EPIX_API std::shared_ptr<StrongHandle> HandleProvider::reserve(
    bool loader_managed, const std::optional<std::filesystem::path>& path
) {
    auto index = index_allocator.reserve();
    return get_handle(index, loader_managed, path);
}