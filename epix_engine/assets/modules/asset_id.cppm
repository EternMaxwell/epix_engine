module;

#include <uuid.h>

export module epix.assets:id;

import std;
import epix.meta;
import epix.utils;

import :index;

export namespace uuids {
/** @brief Three-way comparison for uuids::uuid, providing strong ordering. */
auto operator<=>(const uuids::uuid& lhs, const uuids::uuid& rhs) noexcept {
    if (lhs == rhs) return std::strong_ordering::equal;
    return lhs < rhs ? std::strong_ordering::less : std::strong_ordering::greater;
}
}  // namespace uuids
static_assert(std::three_way_comparable<uuids::uuid>);

namespace assets {
export struct UntypedAssetId;

constexpr uuids::uuid INVALID_UUID = uuids::uuid::from_string("1038587c-0b8d-4f2e-8a3f-1a2b3c4d5e6f").value();

/** @brief Strongly-typed asset identifier, holding either an AssetIndex or a UUID.
 *  @tparam T The asset type this id refers to. */
export template <typename T>
struct AssetId : public std::variant<AssetIndex, uuids::uuid> {
    /** @brief Return a sentinel id that compares unequal to any valid id. */
    static AssetId<T> invalid() { return AssetId<T>(INVALID_UUID); }

    AssetId()                             = delete;
    AssetId(const AssetId<T>&)            = default;
    AssetId(AssetId<T>&&)                 = default;
    AssetId& operator=(const AssetId<T>&) = default;
    AssetId& operator=(AssetId<T>&&)      = default;
    /** @brief Forwarding constructor from variant-compatible arguments.
     * @tparam Args Argument types forwarded to the underlying variant. */
    template <typename... Args>
        requires std::constructible_from<std::variant<AssetIndex, uuids::uuid>, Args...>
    AssetId(Args&&... args) : std::variant<AssetIndex, uuids::uuid>(std::forward<Args>(args)...) {}

    auto operator<=>(const AssetId<T>& other) const = default;
    bool operator==(const AssetId<T>& other) const  = default;
    /** @brief Compare with a type-erased UntypedAssetId. */
    bool operator==(const UntypedAssetId& other) const;

    /** @brief Check whether this id stores a UUID. */
    bool is_uuid() const { return std::holds_alternative<uuids::uuid>(*this); }
    /** @brief Check whether this id stores an AssetIndex. */
    bool is_index() const { return std::holds_alternative<AssetIndex>(*this); }

    /** @brief Return a human-readable string including the type name and underlying id. */
    std::string to_string() const {
        return std::format("AssetId<{}>({})", meta::type_id<T>::name(),
                           std::visit(utils::visitor{[](const AssetIndex& index) {
                                                         return std::format("AssetIndex(index={}, generation={})",
                                                                            index.index(), index.generation());
                                                     },
                                                     [](const uuids::uuid& id) {
                                                         return std::format("UUID({})", uuids::to_string(id));
                                                     }},
                                      *this));
    }
    /** @brief Return a short string representation without the type name. */
    std::string to_string_short() const {
        return std::visit(
            utils::visitor{[](const AssetIndex& index) {
                               return std::format("AssetIndex({}, {})", index.index(), index.generation());
                           },
                           [](const uuids::uuid& id) { return std::format("UUID({})", uuids::to_string(id)); }},
            *this);
    }
};

/** @brief Type-erased asset identifier that stores a type_index alongside the id.
 *  Useful when the asset type is not known at compile time. */
export struct UntypedAssetId {
    /** @brief Runtime type identifier for the asset. */
    meta::type_index type;
    /** @brief The underlying id, either an index or a UUID. */
    std::variant<AssetIndex, uuids::uuid> id;

    /** @brief Construct from a typed AssetId.
     * @tparam T The asset type. */
    template <typename T>
    UntypedAssetId(const AssetId<T>& id) : id(id), type(meta::type_id<T>{}) {}
    /** @brief Construct from a type_index and variant-compatible arguments.
     * @tparam Args Argument types forwarded to the underlying variant. */
    template <typename... Args>
    UntypedAssetId(const meta::type_index& type, Args&&... args) : id(std::forward<Args>(args)...), type(type) {}
    /** @brief Create a sentinel invalid id for the given type.
     *  @tparam T The asset type; defaults to void. */
    template <typename T = void>
    static UntypedAssetId invalid(const meta::type_index& type = meta::type_id<T>{}) {
        return UntypedAssetId(type, AssetId<T>::invalid());
    }

    /** @brief Cast to a typed AssetId. Throws if the type does not match.
     *  @tparam T Expected asset type. */
    template <typename T>
    AssetId<T> typed() const {
        return try_typed<T>().value();
    }
    /** @brief Try to cast to a typed AssetId.
     *  @tparam T Expected asset type.
     *  @return The typed id, or std::nullopt on type mismatch. */
    template <typename T>
    std::optional<AssetId<T>> try_typed() const {
        if (type != meta::type_id<T>{}) {
            return std::nullopt;
        }
        return std::make_optional<AssetId<T>>(id);
    }

    /** @brief Check whether this id stores a UUID. */
    bool is_uuid() const { return std::holds_alternative<uuids::uuid>(id); }
    /** @brief Check whether this id stores an AssetIndex. */
    bool is_index() const { return std::holds_alternative<AssetIndex>(id); }
    /** @brief Get the stored AssetIndex. Throws std::bad_variant_access if it holds a UUID. */
    const AssetIndex& index() const { return std::get<AssetIndex>(id); }
    /** @brief Get the stored UUID. Throws std::bad_variant_access if it holds an AssetIndex. */
    const uuids::uuid& uuid() const { return std::get<uuids::uuid>(id); }
    auto operator<=>(const UntypedAssetId& other) const = default;
    bool operator==(const UntypedAssetId& other) const  = default;
    /** @brief Compare with a typed AssetId. */
    template <typename T>
    bool operator==(const AssetId<T>& other) const {
        return other == *this;
    }
    /** @brief Return a human-readable string including the type name and underlying id. */
    std::string to_string() const {
        return std::format("UntypedAssetId<{}>({})", type.name(),
                           std::visit(utils::visitor{[](const AssetIndex& index) {
                                                         return std::format("AssetIndex(index={}, generation={})",
                                                                            index.index(), index.generation());
                                                     },
                                                     [](const uuids::uuid& id) {
                                                         return std::format("UUID({})", uuids::to_string(id));
                                                     }},
                                      id));
    }
    /** @brief Return a short string representation without the type name. */
    std::string to_string_short() const {
        return std::visit(
            utils::visitor{[](const AssetIndex& index) {
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

export namespace std {
template <typename T>
struct hash<assets::AssetId<T>> {
    std::size_t operator()(const assets::AssetId<T>& id) const {
        return std::visit([]<typename U>(const U& index) { return std::hash<U>()(index); }, id);
    }
};

template <>
struct hash<assets::UntypedAssetId> {
    std::size_t operator()(const assets::UntypedAssetId& id) const {
        std::size_t type_hash = std::hash<meta::type_index>()(id.type);
        std::size_t id_hash   = std::visit([]<typename T>(const T& index) { return std::hash<T>()(index); }, id.id);
        return type_hash ^ (id_hash + 0x9e3779b9 + (type_hash << 6) + (type_hash >> 2));
    }
};
}  // namespace std

/** @brief std::formatter support for AssetId and UntypedAssetId. */
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