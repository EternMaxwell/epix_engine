#pragma once

#include <string>
#include <type_traits>

#include "epix/common.h"
#include "epix/meta/typeid.h"

namespace epix::app {
struct Label {
   protected:
    meta::type_index m_type;
    size_t m_index;

    template <typename U>
    Label(U u) noexcept : m_type(meta::type_id<U>{}), m_index(0) {
        if constexpr (std::is_enum_v<U>) {
            m_index = static_cast<size_t>(u);
        }
    }
    EPIX_API Label(meta::type_index t, size_t i) noexcept;
    EPIX_API Label() noexcept;

   public:
    Label(const Label&)            = default;
    Label(Label&&)                 = default;
    Label& operator=(const Label&) = default;
    Label& operator=(Label&&)      = default;
    EPIX_API bool operator==(const Label& other) const noexcept;
    EPIX_API bool operator!=(const Label& other) const noexcept;
    EPIX_API void set_type(meta::type_index t) noexcept;
    EPIX_API void set_index(size_t i) noexcept;
    EPIX_API meta::type_index get_type() const noexcept;
    EPIX_API size_t get_index() const noexcept;
    EPIX_API size_t hash_code() const noexcept;
    EPIX_API std::string name() const noexcept;
};
}  // namespace epix::app