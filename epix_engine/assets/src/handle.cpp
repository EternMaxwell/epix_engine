module;

#include <spdlog/spdlog.h>

module epix.assets;

import epix.meta;

namespace meta = epix::meta;
using namespace epix::assets;
using namespace epix::core;

StrongHandle::StrongHandle(const UntypedAssetId& id,
                           const Sender<DestructionEvent>& event_sender,
                           bool asset_server_managed,
                           const std::optional<AssetPath>& path,
                           std::optional<MetaTransform> meta_transform)
    : id(id),
      event_sender(event_sender),
      path(path),
      asset_server_managed(asset_server_managed),
      meta_transform(std::move(meta_transform)) {}

StrongHandle::~StrongHandle() {
    spdlog::trace("[assets] StrongHandle destroyed: {}.", id);
    event_sender.send(DestructionEvent{id, asset_server_managed});
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
                                                         bool asset_server_managed,
                                                         const std::optional<AssetPath>& path,
                                                         std::optional<MetaTransform> meta_transform) const {
    return std::make_shared<StrongHandle>(id.untyped(type), event_sender, asset_server_managed, path,
                                          std::move(meta_transform));
}
std::shared_ptr<StrongHandle> HandleProvider::reserve(bool asset_server_managed,
                                                      const std::optional<AssetPath>& path,
                                                      std::optional<MetaTransform> meta_transform) const {
    auto index = index_allocator.reserve();
    return get_handle(index, asset_server_managed, path, std::move(meta_transform));
}

UntypedHandle& UntypedHandle::operator=(const std::shared_ptr<StrongHandle>& handle) {
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

meta::type_index UntypedHandle::type_id() const {
    return std::visit(utils::visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->id.type; },
                                     [](const UntypedAssetId& id) { return id.type; }},
                      ref);
}

UntypedAssetId UntypedHandle::id() const {
    return std::visit(utils::visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->id; },
                                     [](const UntypedAssetId& id) { return id; }},
                      ref);
}

std::optional<AssetPath> UntypedHandle::path() const {
    return std::visit(utils::visitor{[](const std::shared_ptr<StrongHandle>& handle) { return handle->path; },
                                     [](const UntypedAssetId&) -> std::optional<AssetPath> { return std::nullopt; }},
                      ref);
}

const MetaTransform* UntypedHandle::meta_transform() const {
    return std::visit(utils::visitor{[](const std::shared_ptr<StrongHandle>& handle) -> const MetaTransform* {
                                         return handle->meta_transform ? &*handle->meta_transform : nullptr;
                                     },
                                     [](const UntypedAssetId&) -> const MetaTransform* { return nullptr; }},
                      ref);
}
