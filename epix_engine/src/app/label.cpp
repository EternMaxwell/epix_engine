#include <format>

#include "epix/app/label.h"

using namespace epix::app;

EPIX_API Label::Label(std::type_index t, size_t i) noexcept
    : m_type(t), m_index(i) {}
EPIX_API Label::Label() noexcept : m_type(typeid(void)), m_index(0) {}
EPIX_API bool Label::operator==(const Label& other) const noexcept {
    return m_type == other.m_type && m_index == other.m_index;
}
EPIX_API bool Label::operator!=(const Label& other) const noexcept {
    return m_type != other.m_type || m_index != other.m_index;
}
EPIX_API void Label::set_type(std::type_index t) noexcept { m_type = t; }
EPIX_API void Label::set_index(size_t i) noexcept { m_index = i; }
EPIX_API std::type_index Label::get_type() const noexcept { return m_type; }
EPIX_API size_t Label::get_index() const noexcept { return m_index; }
EPIX_API size_t Label::hash_code() const noexcept {
    size_t seed = m_type.hash_code();
    seed ^=
        std::hash<size_t>()(m_index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}
EPIX_API std::string Label::name() const noexcept {
    return std::format("{}#{:#x}", m_type.name(), m_index);
}