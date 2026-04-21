module;
#ifndef EPIX_IMPORT_STD
#include <format>
#include <string>
#include <variant>
#endif
#include <uuid.h>
module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.meta;
import epix.utils;

namespace uuids {

std::strong_ordering operator<=>(const uuids::uuid& lhs, const uuids::uuid& rhs) noexcept {
    if (lhs == rhs) return std::strong_ordering::equal;
    return lhs < rhs ? std::strong_ordering::less : std::strong_ordering::greater;
}

}  // namespace uuids

namespace epix::assets {

std::string UntypedAssetId::to_string() const {
    return std::format(
        "UntypedAssetId<{}>({})", type.name(),
        std::visit(utils::visitor{[](const AssetIndex& index) {
                                      return std::format("AssetIndex(index={}, generation={})", index.index(),
                                                         index.generation());
                                  },
                                  [](const uuids::uuid& id) { return std::format("UUID({})", uuids::to_string(id)); }},
                   id));
}

std::string UntypedAssetId::to_string_short() const {
    return std::visit(
        utils::visitor{[](const AssetIndex& index) {
                           return std::format("AssetIndex({}, {})", index.index(), index.generation());
                       },
                       [](const uuids::uuid& id) { return std::format("UUID({})", uuids::to_string(id)); }},
        id);
}

}  // namespace epix::assets
