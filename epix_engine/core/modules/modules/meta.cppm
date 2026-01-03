module;

#if defined __clang__ || defined __GNUC__
#define EPIX_PRETTY_FUNCTION __PRETTY_FUNCTION__
#define EPIX_PRETTY_FUNCTION_PREFIX '='
#define EPIX_PRETTY_FUNCTION_SUFFIX ']'
#elif defined _MSC_VER
#define EPIX_PRETTY_FUNCTION __FUNCSIG__
#define EPIX_PRETTY_FUNCTION_PREFIX '<'
#define EPIX_PRETTY_FUNCTION_SUFFIX '>'
#endif

export module epix.meta;

import std;

namespace meta {
template <typename T>
constexpr const char* pretty_function() {
    return EPIX_PRETTY_FUNCTION;
}
template <typename T>
constexpr std::string_view type_name() {
    std::string_view full_name = pretty_function<T>();
    auto first = full_name.find_first_not_of(' ', full_name.find_first_of(EPIX_PRETTY_FUNCTION_PREFIX) + 1);
    auto last  = full_name.find_last_of(EPIX_PRETTY_FUNCTION_SUFFIX);
    auto value = full_name.substr(first, last - first);
    return value;
}
constexpr std::string shorten(std::string_view str) {
    std::string result = std::string(str);
    while (true) {
        auto last_colon = result.rfind("::");
        if (last_colon == std::string::npos) break;
        constexpr std::array left_chars = std::array{
            '<', '(', '[', ',', ' ',  // characters that can appear before a template argument
        };
        std::vector lefts = left_chars | std::views::transform([&](char c) { return result.rfind(c, last_colon); }) |
                            std::views::filter([&](std::size_t pos) { return pos != std::string::npos; }) |
                            std::ranges::to<std::vector>();
        auto left_elem = std::ranges::max_element(lefts);
        auto left      = (left_elem != lefts.end()) ? *left_elem + 1 : 0;
        result         = result.substr(0, left) + result.substr(last_colon + 2);
    }
    // remove all spaces
    // result.erase(std::remove_if(result.begin(), result.end(), [](char c) { return c == ' '; }), result.end());
    return result;
}
template <typename T>
constexpr std::string_view short_name() {
    static std::string name = shorten(type_name<T>());
    return name;
}
export struct type_info {
    using destruct_fn       = void (*)(void* ptr) noexcept;
    using copy_construct_fn = void (*)(void* dest, const void* src) noexcept;
    using move_construct_fn = void (*)(void* dest, void* src) noexcept;

    std::string_view name;
    std::string_view short_name;
    std::size_t hash;

    std::size_t size;
    std::size_t align;

    // Mandatory operations
    destruct_fn destruct             = nullptr;
    copy_construct_fn copy_construct = nullptr;
    move_construct_fn move_construct = nullptr;

    // Cached traits
    bool copy_constructible : 1;
    bool move_constructible : 1;
    bool trivially_copyable : 1;
    bool trivially_destructible : 1;
    bool noexcept_move_constructible : 1;
    bool noexcept_copy_constructible : 1;

    template <typename T>
    static destruct_fn destruct_impl() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            return [](void* p) noexcept { static_cast<T*>(p)->~T(); };
        } else if constexpr (std::is_destructible_v<T>) {
            return [](void* p) noexcept {
                // no-op for trivially destructible types
            };
        } else {
            return [](void* p) noexcept { std::abort(); };
        }
    }
    template <typename T>
    static copy_construct_fn copy_construct_impl() {
        if constexpr (std::copy_constructible<T>) {
            return [](void* dest, const void* src) noexcept { new (dest) T(*static_cast<const T*>(src)); };
        } else {
            return [](void* dest, const void* src) noexcept { std::abort(); };
        }
    }
    template <typename T>
    static move_construct_fn move_construct_impl() {
        if constexpr (std::move_constructible<T>) {
            return [](void* dest, void* src) noexcept { new (dest) T(std::move(*static_cast<T*>(src))); };
        } else {
            return [](void* dest, void* src) noexcept { std::abort(); };
        }
    }

    auto operator<=>(const type_info& other) const { return name <=> other.name; }
    bool operator==(const type_info& other) const { return (name <=> other.name) == std::strong_ordering::equal; }

    template <typename T>
    static const type_info& of() {
        if constexpr (requires { sizeof(T); }) {
            static const type_info ti = of1<T>();
            return ti;
        } else {
            static const type_info ti = of2<T>();
            return ti;
        }
    }

   private:
    type_info() = default;

    template <typename T>
    static type_info of1() {
        type_info ti{};
        ti.name                        = type_name<T>();
        ti.short_name                  = meta::short_name<T>();
        ti.hash                        = std::hash<std::string_view>()(type_name<T>());
        ti.size                        = sizeof(T);
        ti.align                       = alignof(T);
        ti.destruct                    = destruct_impl<T>();
        ti.copy_construct              = copy_construct_impl<T>();
        ti.move_construct              = move_construct_impl<T>();
        ti.copy_constructible          = std::copy_constructible<T>;
        ti.move_constructible          = std::move_constructible<T>;
        ti.trivially_copyable          = std::is_trivially_copyable_v<T>;
        ti.trivially_destructible      = std::is_trivially_destructible_v<T>;
        ti.noexcept_move_constructible = std::is_nothrow_move_constructible_v<T>;
        ti.noexcept_copy_constructible = std::is_nothrow_copy_constructible_v<T>;

        return ti;
    }
    template <typename T>
    static type_info of2() {
        type_info ti{};
        ti.name                        = type_name<T>();
        ti.short_name                  = meta::short_name<T>();
        ti.hash                        = std::hash<std::string_view>()(type_name<T>());
        ti.size                        = 0;
        ti.align                       = 0;
        ti.destruct                    = nullptr;
        ti.copy_construct              = nullptr;
        ti.move_construct              = nullptr;
        ti.copy_constructible          = false;
        ti.move_constructible          = false;
        ti.trivially_copyable          = false;
        ti.trivially_destructible      = false;
        ti.noexcept_move_constructible = false;
        ti.noexcept_copy_constructible = false;

        return ti;
    }
};
export template <typename T>
struct type_id {
   public:
    static std::string_view name() { return type_info::of<T>().name; }
    static std::string_view short_name() { return type_info::of<T>().short_name; }
    static std::size_t hash_code() { return type_info::of<T>().hash; }
    static const type_info& type_info() { return type_info::of<T>(); }
};
export struct type_index {
   public:
    template <typename T>
    type_index(type_id<T> id) : inter(std::addressof(id.type_info())) {}
    type_index() : inter(std::addressof(type_info::of<void>())) {}

    auto operator<=>(const type_index& other) const noexcept {
        if (inter == other.inter) return std::strong_ordering::equal;
        if (!inter && !other.inter) return std::strong_ordering::equal;
        if (!inter) return std::strong_ordering::less;
        if (!other.inter) return std::strong_ordering::greater;
        return *inter <=> *other.inter;
    }
    bool operator==(const type_index& other) const noexcept { return (*this <=> other) == std::strong_ordering::equal; }
    std::string_view name() const noexcept { return inter->name; }
    std::string_view short_name() const noexcept { return inter->short_name; }
    std::size_t hash_code() const noexcept { return inter->hash; }
    const type_info& type_info() const noexcept { return *inter; }
    bool valid() const noexcept { return inter != nullptr; }

   private:
    const meta::type_info* inter;

    template <typename T>
    static const meta::type_info& get_info() {
        return type_info::of<T>();
    }
};
}  // namespace meta

template <>
struct std::hash<meta::type_index> {
    std::size_t operator()(const meta::type_index& ti) const noexcept { return ti.hash_code(); }
};