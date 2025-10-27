#pragma once

#include <uuid.h>

#include <variant>

#include "index.hpp"

namespace epix::assets {
template <typename... Ts>
struct visitor : public Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
visitor(Ts...) -> visitor<Ts...>;

struct UntypedAssetId;
inline constexpr uuids::uuid INVALID_UUID = uuids::uuid::from_string("1038587c-0b8d-4f2e-8a3f-1a2b3c4d5e6f").value();

template <typename T>
struct AssetId : public std::variant<AssetIndex, uuids::uuid> {
    static AssetId<T> invalid() { return AssetId<T>(INVALID_UUID); }

    bool operator==(const AssetId<T>& other) const {
        return ((const std::variant<AssetIndex, uuids::uuid>&)*this) ==
               ((const std::variant<AssetIndex, uuids::uuid>&)(other));
    }
    bool operator==(const UntypedAssetId& other) const;

    bool is_uuid() const { return std::holds_alternative<uuids::uuid>(*this); }
    bool is_index() const { return std::holds_alternative<AssetIndex>(*this); }

    std::string to_string() const {
        return std::format(
            "AssetId<{}>({})", meta::type_id<T>::name(),
            std::visit(visitor{[](const AssetIndex& index) {
                                   return std::format("AssetIndex(index={}, generation={})", index.index(),
                                                      index.generation());
                               },
                               [](const uuids::uuid& id) { return std::format("UUID({})", uuids::to_string(id)); }},
                       *this));
    }
    std::string to_string_short() const {
        return std::visit(visitor{[](const AssetIndex& index) {
                                      return std::format("AssetIndex({}, {})", index.index(), index.generation());
                                  },
                                  [](const uuids::uuid& id) { return std::format("UUID({})", uuids::to_string(id)); }},
                          *this);
    }
};

struct UntypedAssetId {
    std::variant<AssetIndex, uuids::uuid> id;
    meta::type_index type;

    template <typename T>
    UntypedAssetId(const AssetId<T>& id) : id(id), type(meta::type_id<T>{}) {}
    template <typename... Args>
    UntypedAssetId(const meta::type_index& type, Args&&... args) : id(std::forward<Args>(args)...), type(type) {}
    template <typename T = void>
    static UntypedAssetId invalid(const meta::type_index& type = meta::type_id<T>{}) {
        return UntypedAssetId(type, AssetId<T>::invalid());
    }

    template <typename T>
    AssetId<T> typed() const {
        return try_typed<T>().value();
    }
    template <typename T>
    std::optional<AssetId<T>> try_typed() const {
        if (type != meta::type_id<T>{}) {
            return std::nullopt;
        }
        return std::make_optional<AssetId<T>>(id);
    }

    bool is_uuid() const { return std::holds_alternative<uuids::uuid>(id); }
    bool is_index() const { return std::holds_alternative<AssetIndex>(id); }
    const AssetIndex& index() const { return std::get<AssetIndex>(id); }
    const uuids::uuid& uuid() const { return std::get<uuids::uuid>(id); }
    bool operator==(const UntypedAssetId& other) const { return id == other.id && type == other.type; }
    std::string to_string() const {
        return std::format(
            "UntypedAssetId<{}>({})", type.name(),
            std::visit(visitor{[](const AssetIndex& index) {
                                   return std::format("AssetIndex(index={}, generation={})", index.index(),
                                                      index.generation());
                               },
                               [](const uuids::uuid& id) { return std::format("UUID({})", uuids::to_string(id)); }},
                       id));
    }
    std::string to_string_short() const {
        return std::visit(visitor{[](const AssetIndex& index) {
                                      return std::format("AssetIndex({}, {})", index.index(), index.generation());
                                  },
                                  [](const uuids::uuid& id) { return std::format("UUID({})", uuids::to_string(id)); }},
                          id);
    }
};

template <typename T>
bool AssetId<T>::operator==(const UntypedAssetId& other) const {
    return other.id == *this && other.type == meta::type_id<T>{};
}

struct InternalAssetId : std::variant<AssetIndex, uuids::uuid> {
    using std::variant<AssetIndex, uuids::uuid>::variant;
    template <typename T>
    InternalAssetId(const AssetId<T>& id) : std::variant<AssetIndex, uuids::uuid>(id) {}
    InternalAssetId(const UntypedAssetId& id) : std::variant<AssetIndex, uuids::uuid>(id.id) {}

    UntypedAssetId untyped(const meta::type_index& type) const { return UntypedAssetId(type, *this); }
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
            epix::assets::visitor{[](const epix::assets::AssetIndex& index) {
                                      return std::hash<uint64_t>()((static_cast<uint64_t>(index.index()) << 32) |
                                                                   static_cast<uint64_t>(index.generation()));
                                  },
                                  [](const uuids::uuid& id) { return std::hash<uuids::uuid>()(id); }},
            id);
    }
};

template <>
struct std::hash<epix::assets::UntypedAssetId> {
    size_t operator()(const epix::assets::UntypedAssetId& id) const {
        size_t type_hash = std::hash<epix::meta::type_index>()(id.type);
        size_t id_hash   = std::visit(
            epix::assets::visitor{[](const epix::assets::AssetIndex& index) {
                                      return std::hash<uint64_t>()((static_cast<uint64_t>(index.index()) << 32) |
                                                                   static_cast<uint64_t>(index.generation()));
                                  },
                                  [](const uuids::uuid& id) { return std::hash<uuids::uuid>()(id); }},
            id.id);
        return type_hash ^ (id_hash + 0x9e3779b9 + (type_hash << 6) + (type_hash >> 2));
    }
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
        return format_to(ctx.out(), "AssetId<{}>({})", epix::meta::type_id<T>::short_name(), [&id]() -> std::string {
            if (std::holds_alternative<epix::assets::AssetIndex>(id)) {
                auto& index = std::get<epix::assets::AssetIndex>(id);
                return std::format("AssetIndex(index={}, generation={})", index.index(), index.generation());
            } else if (std::holds_alternative<uuids::uuid>(id)) {
                auto& uuid = std::get<uuids::uuid>(id);
                return std::format("UUID({})", uuids::to_string(uuid));
            }
            return "Invalid";
        }());
    }
};
template <>
struct formatter<epix::assets::UntypedAssetId> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const epix::assets::UntypedAssetId& id, FormatContext& ctx) const {
        return format_to(ctx.out(), "UntypedAssetId<{}>({})", id.type.short_name(), [&id]() -> std::string {
            if (std::holds_alternative<epix::assets::AssetIndex>(id.id)) {
                auto& index = std::get<epix::assets::AssetIndex>(id.id);
                return std::format("AssetIndex(index={}, generation={})", index.index(), index.generation());
            } else if (std::holds_alternative<uuids::uuid>(id.id)) {
                auto& uuid = std::get<uuids::uuid>(id.id);
                return std::format("UUID({})", uuids::to_string(uuid));
            }
            return "Invalid";
        }());
    }
};
}  // namespace std