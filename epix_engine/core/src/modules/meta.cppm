module;

#include <array>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

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

namespace meta {
template <typename T>
constexpr std::string_view type_name() {
    static std::string pretty_function{EPIX_PRETTY_FUNCTION};
    static auto first =
        pretty_function.find_first_not_of(' ', pretty_function.find_first_of(EPIX_PRETTY_FUNCTION_PREFIX) + 1);
    static auto last  = pretty_function.find_last_of(EPIX_PRETTY_FUNCTION_SUFFIX);
    static auto value = pretty_function.substr(first, last - first);
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
                            std::views::filter([&](size_t pos) { return pos != std::string::npos; }) |
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
    size_t hash;

    size_t size;
    size_t align;

    // Mandatory operations
    void (*destruct)(void* ptr) noexcept                         = nullptr;
    void (*copy_construct)(void* dest, const void* src) noexcept = nullptr;
    void (*move_construct)(void* dest, void* src) noexcept       = nullptr;

    // Cached traits
    bool copy_constructible;
    bool move_constructible;
    bool trivially_copyable;
    bool trivially_destructible;
    bool noexcept_move_constructible;
    bool noexcept_copy_constructible;

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
            return *of1<T>();
        } else {
            return *of2<T>();
        }
    }

   private:
    template <typename T>
    static const type_info* of1() {
        static type_info ti = type_info{
            .name                        = type_name<T>(),
            .short_name                  = meta::short_name<T>(),
            .hash                        = std::hash<std::string_view>()(type_name<T>()),
            .size                        = sizeof(T),
            .align                       = alignof(T),
            .destruct                    = destruct_impl<T>(),
            .copy_construct              = copy_construct_impl<T>(),
            .move_construct              = move_construct_impl<T>(),
            .copy_constructible          = std::copy_constructible<T>,
            .move_constructible          = std::move_constructible<T>,
            .trivially_copyable          = std::is_trivially_copyable_v<T>,
            .trivially_destructible      = std::is_trivially_destructible_v<T>,
            .noexcept_move_constructible = std::is_nothrow_move_constructible_v<T>,
            .noexcept_copy_constructible = std::is_nothrow_copy_constructible_v<T>,
        };
        return &ti;
    }
    template <typename T>
    static const type_info* of2() {
        static type_info ti = type_info{
            .name                        = type_name<T>(),
            .short_name                  = meta::short_name<T>(),
            .hash                        = std::hash<std::string_view>()(type_name<T>()),
            .size                        = 0,
            .align                       = 0,
            .destruct                    = nullptr,
            .copy_construct              = nullptr,
            .move_construct              = nullptr,
            .copy_constructible          = false,
            .move_constructible          = false,
            .trivially_copyable          = false,
            .trivially_destructible      = false,
            .noexcept_move_constructible = false,
            .noexcept_copy_constructible = false,
        };
        return &ti;
    }
};
export template <typename T>
struct type_id {
   public:
    static std::string_view name() { return type_info::of<T>().name; }
    static std::string_view short_name() { return type_info::of<T>().short_name; }
    static size_t hash_code() { return type_info::of<T>().hash; }
};
export struct type_index {
   public:
    template <typename T>
    type_index(type_id<T>) : inter(std::addressof(get_info<T>())) {}
    type_index() : inter(nullptr) {}

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
    size_t hash_code() const noexcept { return inter->hash; }
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

export namespace epix::meta {
using namespace meta;
}  // namespace epix::meta