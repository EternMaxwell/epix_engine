#include "epix/assets/handle.hpp"

using namespace epix::assets;

HandleProvider::HandleProvider(const epix::meta::type_index& type) : type(type) {
    std::tie(event_sender, event_receiver) = epix::utils::async::make_channel<DestructionEvent>();
}

UntypedHandle HandleProvider::reserve() {
    auto index = index_allocator.reserve();
    return std::make_shared<StrongHandle>(UntypedAssetId(type, index), event_sender, false, std::nullopt);
}
std::shared_ptr<StrongHandle> HandleProvider::get_handle(const InternalAssetId& id,
                                                         bool loader_managed,
                                                         const std::optional<std::filesystem::path>& path) {
    return std::make_shared<StrongHandle>(id.untyped(type), event_sender, loader_managed, path);
}
std::shared_ptr<StrongHandle> HandleProvider::reserve(bool loader_managed,
                                                      const std::optional<std::filesystem::path>& path) {
    auto index = index_allocator.reserve();
    return get_handle(index, loader_managed, path);
}