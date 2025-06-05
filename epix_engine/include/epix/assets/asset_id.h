#pragma once

#include <epix/utils/core.h>
#include <uuid.h>

#include <typeindex>

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

    std::string to_string() const {
        return std::format(
            "AssetId<{}>({})", typeid(T).name(),
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
                *this
            );
        )
    }
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

    EPIX_API const AssetIndex& index() const;
    EPIX_API const uuids::uuid& uuid() const;
    EPIX_API bool operator==(const UntypedAssetId& other) const;
    EPIX_API std::string to_string() const;
};

template <typename T>
bool AssetId<T>::operator==(const UntypedAssetId& other) const {
    return other.id == *this && other.type == typeid(T);
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

template <>
struct std::hash<epix::assets::UntypedAssetId> {
    EPIX_API size_t operator()(const epix::assets::UntypedAssetId& id) const;
};

// formatter support for AssetId and UntypedAssetId
namespace std {
template <typename T>
struct formatter<epix::assets::AssetId<T>> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const epix::assets::AssetId<T>& id, FormatContext& ctx) const {
        return format_to(
            ctx.out(), "AssetId<{}>({})", typeid(T).name(),
            [&id]() -> std::string {
                if (std::holds_alternative<epix::assets::AssetIndex>(id)) {
                    auto& index = std::get<epix::assets::AssetIndex>(id);
                    return std::format(
                        "AssetIndex(index={}, generation={})", index.index,
                        index.generation
                    );
                } else if (std::holds_alternative<uuids::uuid>(id)) {
                    auto& uuid = std::get<uuids::uuid>(id);
                    return std::format("UUID({})", uuids::to_string(uuid));
                }
                return "Invalid";
            }()
        );
    }
};
template <>
struct formatter<epix::assets::UntypedAssetId> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const epix::assets::UntypedAssetId& id, FormatContext& ctx)
        const {
        return format_to(
            ctx.out(), "UntypedAssetId<{}>({})", id.type.name(),
            [&id]() -> std::string {
                if (std::holds_alternative<epix::assets::AssetIndex>(id.id)) {
                    auto& index = std::get<epix::assets::AssetIndex>(id.id);
                    return std::format(
                        "AssetIndex(index={}, generation={})", index.index,
                        index.generation
                    );
                } else if (std::holds_alternative<uuids::uuid>(id.id)) {
                    auto& uuid = std::get<uuids::uuid>(id.id);
                    return std::format("UUID({})", uuids::to_string(uuid));
                }
                return "Invalid";
            }()
        );
    }
};
}  // namespace std