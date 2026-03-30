module;

#include <spdlog/spdlog.h>

module epix.assets;

import :handle;

using namespace epix::assets;
using namespace epix::core;

StrongHandle::StrongHandle(const UntypedAssetId& id,
                           const Sender<DestructionEvent>& event_sender,
                           bool loader_managed,
                           const std::optional<AssetPath>& path,
                           std::optional<MetaTransform> meta_transform)
    : id(id),
      event_sender(event_sender),
      path(path),
      loader_managed(loader_managed),
      meta_transform(std::move(meta_transform)) {}

StrongHandle::~StrongHandle() {
    spdlog::trace("[assets] StrongHandle destroyed: {}.", id);
    event_sender.send(DestructionEvent{id, loader_managed});
}

HandleProvider::HandleProvider(const meta::type_index& type) : type(type) {
    std::tie(event_sender, event_receiver) = make_channel<DestructionEvent>();
}

UntypedHandle HandleProvider::reserve() const {
    auto index = index_allocator.reserve();
    spdlog::trace("[assets] HandleProvider::reserve: index={}/gen={}.", index.index(), index.generation());
    return std::make_shared<StrongHandle>(UntypedAssetId(type, index), event_sender, false, std::nullopt);
}
std::shared_ptr<StrongHandle> HandleProvider::get_handle(const InternalAssetId& id,
                                                         bool loader_managed,
                                                         const std::optional<AssetPath>& path,
                                                         std::optional<MetaTransform> meta_transform) const {
    return std::make_shared<StrongHandle>(id.untyped(type), event_sender, loader_managed, path,
                                          std::move(meta_transform));
}
std::shared_ptr<StrongHandle> HandleProvider::reserve(bool loader_managed,
                                                      const std::optional<AssetPath>& path,
                                                      std::optional<MetaTransform> meta_transform) const {
    auto index = index_allocator.reserve();
    return get_handle(index, loader_managed, path, std::move(meta_transform));
}
