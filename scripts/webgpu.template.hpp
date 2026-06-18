#pragma once

#ifndef EPIX_CXX_MODULE
{{webgpu_includes}}

#include <atomic>
#include <cstddef>
#include <iostream>
#include <vector>
#include <functional>
#include <cassert>
#include <concepts>
#include <cmath>
#include <memory>
#include <new>
#include <initializer_list>
#include <string>
#include <string_view>
#include <span>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#endif

#ifndef WEBGPU_EXPORT_BEGIN
#define WEBGPU_EXPORT_BEGIN
#define WEBGPU_EXPORT_END
#endif

{{begin_inject}}
typename StringView:
    StringView(const std::string_view& sv) : owned_(sv) {}
    StringView(const char* str) : owned_(str ? str : "") {}
    StringView(const StringView&) = default;
    StringView(StringView&&) = default;
    StringView& operator=(const StringView&) = default;
    StringView& operator=(StringView&&) = default;
    operator std::string_view() const { return std::string_view(owned_); }
typename Instance:
    WEBGPU_CPP_NAMESPACE::Adapter requestAdapter(const RequestAdapterOptions& options) const;
typename Adapter:
    WEBGPU_CPP_NAMESPACE::Device requestDevice(const DeviceDescriptor& descriptor) const;
typename Color:
    Color(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
typename Extent3D:
    Extent3D(uint32_t width, uint32_t height, uint32_t depthOrArrayLayers = 1) : width(width), height(height), depthOrArrayLayers(depthOrArrayLayers) {}
typename Origin3D:
    Origin3D(uint32_t x, uint32_t y, uint32_t z) : x(x), y(y), z(z) {}
{{end_inject}}

WEBGPU_EXPORT_BEGIN

// Type aliases
{{type_aliases}}

// Enums
{{enums}}

// Struct declarations
{{structs_decl}}
// Handle declarations
{{handles_decl}}
// Callback declarations
{{callbacks_decl}}

// Handles
{{handles}}
// Callbacks
{{callbacks}}
// Structs
{{structs}}

// Non member functions
{{functions_decl}}

WEBGPU_EXPORT_END

// Struct template implementations
{{structs_template_impl}}
// Handle template implementations
{{handles_template_impl}}
// Callback template implementations
{{callbacks_template_impl}}
