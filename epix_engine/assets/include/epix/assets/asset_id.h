#pragma once

#include <uuid.h>

#include "index.h"

namespace epix::assets {
struct UntypedAssetId;

template <typename T>
struct AssetId : public std::variant<AssetIndex, uuids::uuid> {
    static const uuids::uuid INVALID_UUID;
    static AssetId<T> invalid() { return AssetId<T>(INVALID_UUID); }

    bool operator==(const AssetId<T>& other) const {
        if (std::holds_alternative<AssetIndex>(other)) {
            return std::holds_alternative<AssetIndex>(*this) &&
                   std::get<AssetIndex>(*this) == std::get<AssetIndex>(other);
        } else if (std::holds_alternative<uuids::uuid>(other)) {
            return std::holds_alternative<uuids::uuid>(*this) &&
                   std::get<uuids::uuid>(*this) == std::get<uuids::uuid>(other);
        }
    }
    bool operator==(const UntypedAssetId& other) const;
};

template <typename T>
inline const uuids::uuid AssetId<T>::INVALID_UUID =
    uuids::uuid::from_string("1038587c-0b8d-4f2e-8a3f-1a2b3c4d5e6f").value();

struct UntypedAssetId {
    std::variant<AssetIndex, uuids::uuid> id;
    std::type_index type;

    template <typename T>
    UntypedAssetId(const AssetId<T>& id) : id(id), type(typeid(T)) {}
    template <typename... Args>
    UntypedAssetId(const std::type_index& type, Args&&... args)
        : id(std::forward<Args>(args)...), type(type) {}

    const AssetIndex& index() const { return std::get<AssetIndex>(id); }
    const uuids::uuid& uuid() const { return std::get<uuids::uuid>(id); }

    template <typename T>
    AssetId<T> typed() const {
        return try_typed<T>().value();
    }
    template <typename T>
    std::optional<AssetId<T>> try_typed() const {
        if (type != typeid(T)) {
            return std::nullopt;
        }
        return std::make_optional<AssetId<T>>(id);
    }

    bool operator==(const UntypedAssetId& other) const {
        return id == other.id && type == other.type;
    }
};

template <typename T>
bool AssetId<T>::operator==(const UntypedAssetId& other) const {
    return other == *this;
}

struct InternalAssetId : std::variant<AssetIndex, uuids::uuid> {
    using std::variant<AssetIndex, uuids::uuid>::variant;
    template <typename T>
    InternalAssetId(const AssetId<T>& id)
        : std::variant<AssetIndex, uuids::uuid>(id) {}
    InternalAssetId(const UntypedAssetId& id)
        : std::variant<AssetIndex, uuids::uuid>(id.id) {}

    UntypedAssetId untyped(const std::type_index& type) const {
        return UntypedAssetId(type, *this);
    }
    template <typename T>
    AssetId<T> typed() const {
        return AssetId<T>(*this);
    }
};
}  // namespace epix::assets

template <typename T>
struct std::hash<epix::assets::AssetId<T>> {
    size_t operator()(const epix::assets::AssetId<T>& id) const {
        return std::visit(
            epix::util::visitor{
                [](const epix::assets::AssetIndex& index) {
                    return std::hash<uint64_t>()(
                        *reinterpret_cast<const uint64_t*>(&index)
                    );
                },
                [](const uuids::uuid& id) {
                    return std::hash<uuids::uuid>()(id);
                }
            },
            id
        );
    }
};