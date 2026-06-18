module;
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

export module webgpu;
#define EPIX_CXX_MODULE
#define WEBGPU_EXPORT_BEGIN export {
#define WEBGPU_EXPORT_END }
extern "C++" {
#include <webgpu/webgpu.hpp>
}
