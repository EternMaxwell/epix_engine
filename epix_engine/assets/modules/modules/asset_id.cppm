module;

#include <uuid.h>

export module epix.assets:id;

import std;
import epix.meta;
import :index;

namespace uuids {
auto operator<=>(const uuids::uuid& lhs, const uuids::uuid& rhs) noexcept {
    if (lhs == rhs) return std::strong_ordering::equal;
    return lhs < rhs ? std::strong_ordering::less : std::strong_ordering::greater;
}
}  // namespace uuids
static_assert(std::three_way_comparable<uuids::uuid>);

namespace assets {
export template <typename... Ts>
struct visitor : public Ts... {
    using Ts::operator()...;
};
export template <typename... Ts>
visitor(Ts...) -> visitor<Ts...>;

export struct UntypedAssetId;

constexpr uuids::uuid INVALID_UUID = uuids::uuid::from_string("1038587c-0b8d-4f2e-8a3f-1a2b3c4d5e6f").value();

export template <typename T>
struct AssetId : public std::variant<AssetIndex, uuids::uuid> {
    static AssetId<T> invalid() { return AssetId<T>(INVALID_UUID); }

    AssetId()                             = delete;
    AssetId(const AssetId<T>&)            = default;
    AssetId(AssetId<T>&&)                 = default;
    AssetId& operator=(const AssetId<T>&) = default;
    AssetId& operator=(AssetId<T>&&)      = default;
    template <typename... Args>
        requires std::constructible_from<std::variant<AssetIndex, uuids::uuid>, Args...>
    AssetId(Args&&... args) : std::variant<AssetIndex, uuids::uuid>(std::forward<Args>(args)...) {}

    auto operator<=>(const AssetId<T>& other) const = default;
    bool operator==(const AssetId<T>& other) const  = default;
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

export struct UntypedAssetId {
    meta::type_index type;
    std::variant<AssetIndex, uuids::uuid> id;

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
    auto operator<=>(const UntypedAssetId& other) const = default;
    bool operator==(const UntypedAssetId& other) const  = default;
    template <typename T>
    bool operator==(const AssetId<T>& other) const {
        return other == *this;
    }
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

export template <typename T>
bool AssetId<T>::operator==(const UntypedAssetId& other) const {
    return other.id == *this && other.type == meta::type_id<T>{};
}

struct InternalAssetId : std::variant<AssetIndex, uuids::uuid> {
    using std::variant<AssetIndex, uuids::uuid>::variant;
    template <typename T>
    InternalAssetId(const AssetId<T>& id) : std::variant<AssetIndex, uuids::uuid>(id) {}
    InternalAssetId(const UntypedAssetId& id) : std::variant<AssetIndex, uuids::uuid>(id.id) {}

    auto operator<=>(const InternalAssetId& other) const = default;
    bool operator==(const InternalAssetId& other) const  = default;

    UntypedAssetId untyped(const meta::type_index& type) const { return UntypedAssetId(type, *this); }
    template <typename T>
    AssetId<T> typed() const {
        return AssetId<T>(*this);
    }
};
}  // namespace assets

export template <typename T>
struct std::hash<assets::AssetId<T>> {
    std::size_t operator()(const assets::AssetId<T>& id) const {
        return std::visit(
            assets::visitor{[](const assets::AssetIndex& index) {
                                return std::hash<uint64_t>()((static_cast<uint64_t>(index.index()) << 32) |
                                                             static_cast<uint64_t>(index.generation()));
                            },
                            [](const uuids::uuid& id) { return std::hash<uuids::uuid>()(id); }},
            id);
    }
};

export template <>
struct std::hash<assets::UntypedAssetId> {
    std::size_t operator()(const assets::UntypedAssetId& id) const {
        std::size_t type_hash = std::hash<meta::type_index>()(id.type);
        std::size_t id_hash =
            std::visit(assets::visitor{[](const assets::AssetIndex& index) {
                                           return std::hash<uint64_t>()((static_cast<uint64_t>(index.index()) << 32) |
                                                                        static_cast<uint64_t>(index.generation()));
                                       },
                                       [](const uuids::uuid& id) { return std::hash<uuids::uuid>()(id); }},
                       id.id);
        return type_hash ^ (id_hash + 0x9e3779b9 + (type_hash << 6) + (type_hash >> 2));
    }
};

// formatter support for AssetId and UntypedAssetId
export namespace std {
template <typename T>
struct formatter<assets::AssetId<T>> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const assets::AssetId<T>& id, FormatContext& ctx) const {
        return format_to(ctx.out(), "AssetId<{}>({})", meta::type_id<T>::short_name(), [&id]() -> std::string {
            if (std::holds_alternative<assets::AssetIndex>(id)) {
                auto& index = std::get<assets::AssetIndex>(id);
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
struct formatter<assets::UntypedAssetId> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const assets::UntypedAssetId& id, FormatContext& ctx) const {
        return format_to(ctx.out(), "UntypedAssetId<{}>({})", id.type.short_name(), [&id]() -> std::string {
            if (std::holds_alternative<assets::AssetIndex>(id.id)) {
                auto& index = std::get<assets::AssetIndex>(id.id);
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