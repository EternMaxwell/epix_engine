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