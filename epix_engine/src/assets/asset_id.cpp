#include "epix/assets/asset_id.h"

using namespace epix::assets;

EPIX_API size_t std::hash<epix::assets::UntypedAssetId>::operator()(
    const epix::assets::UntypedAssetId& id
) const {
    auto seed = std::visit(
        epix::util::visitor{
            [](const epix::assets::AssetIndex& index) {
                return std::hash<uint64_t>()(
                    *reinterpret_cast<const uint64_t*>(&index)
                );
            },
            [](const uuids::uuid& uuid) {
                return std::hash<uuids::uuid>()(uuid);
            }
        },
        id.id
    );
    // combine with type index
    return id.type.hash_code() ^
           (seed + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

EPIX_API const AssetIndex& UntypedAssetId::index() const { return std::get<AssetIndex>(id); }
EPIX_API const uuids::uuid& UntypedAssetId::uuid() const {
    return std::get<uuids::uuid>(id);
}
EPIX_API bool UntypedAssetId::operator==(const UntypedAssetId& other) const {
    return id == other.id && type == other.type;
}
EPIX_API std::string UntypedAssetId::to_string() const {
    return std::format(
        "UntypedAssetId<{}>({})", type.name(),
        std::visit(
            epix::util::visitor{
                [](const AssetIndex& index) {
                    return std::format(
                        "AssetIndex(index={}, generation={})", index.index,
                        index.generation
                    );
                },
                [](const uuids::uuid& id) {
                    return std::format("UUID({})", uuids::to_string(id));
                }
            },
            id
        )
    );
}