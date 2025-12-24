#include "epix/assets/handle.hpp"

using namespace epix::assets;

StrongHandle::StrongHandle(const UntypedAssetId& id,
                           const Sender<DestructionEvent>& event_sender,
                           bool loader_managed,
                           const std::optional<std::filesystem::path>& path)
    : id(id), event_sender(event_sender), path(path), loader_managed(loader_managed) {}

StrongHandle::~StrongHandle() { event_sender.send(DestructionEvent{id}); }

HandleProvider::HandleProvider(const epix::meta::type_index& type) : type(type) {
    std::tie(event_sender, event_receiver) = epix::utils::async::make_channel<DestructionEvent>();
}

UntypedHandle HandleProvider::reserve() const {
    auto index = index_allocator.reserve();
    return std::make_shared<StrongHandle>(UntypedAssetId(type, index), event_sender, false, std::nullopt);
}
std::shared_ptr<StrongHandle> HandleProvider::get_handle(const InternalAssetId& id,
                                                         bool loader_managed,
                                                         const std::optional<std::filesystem::path>& path) const {
    return std::make_shared<StrongHandle>(id.untyped(type), event_sender, loader_managed, path);
}
std::shared_ptr<StrongHandle> HandleProvider::reserve(bool loader_managed,
                                                      const std::optional<std::filesystem::path>& path) const {
    auto index = index_allocator.reserve();
    return get_handle(index, loader_managed, path);
}