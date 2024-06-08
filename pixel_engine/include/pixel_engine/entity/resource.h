﻿#pragma once

#include <any>
#include <deque>
#include <entt/entity/registry.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <pfr.hpp>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>

namespace pixel_engine {
    namespace entity {
        template <typename ResT>
        class Resource {
           private:
            std::any* const m_res;

           public:
            Resource(std::any* resource) : m_res(resource) {}
            Resource() : m_res(nullptr) {}

            /*! @brief Check if the resource is const.
             * @return True if the resource is const, false otherwise.
             */
            constexpr bool is_const() { return std::is_const_v<std::remove_reference_t<ResT>>; }

            /*! @brief Check if the resource has a value.
             * @return True if the resource has a value, false otherwise.
             */
            bool has_value() { return m_res != nullptr && m_res->has_value(); }

            /*! @brief Get the value of the resource.
             * @return The value of the resource.
             */
            ResT& value() { return *(std::any_cast<std::shared_ptr<ResT>&>(*m_res)); }
        };

        template <typename T, typename U>
        struct resource_access_contrary {
            const static bool value = true;
        };

        template <typename T, typename U>
        struct resource_access_contrary<Resource<T>, Resource<U>> {
            const static bool value = std::is_same_v<std::remove_const_t<T>, std::remove_const_t<U>> &&
                                        !(std::is_const_v<T> && std::is_const_v<U>);
        };
    }  // namespace entity
}  // namespace pixel_engine