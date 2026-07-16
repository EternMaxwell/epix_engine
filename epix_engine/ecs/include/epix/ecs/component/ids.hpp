#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#endif

#include <epix/ecs/core/type_id.hpp>

namespace epix::ecs {

EPIX_EXPORT struct ComponentIds {
   public:
    TypeId peek() const { return m_id->load(std::memory_order::relaxed); }
    TypeId next() const { return m_id->fetch_add(1, std::memory_order::relaxed); }

   private:
    std::unique_ptr<std::atomic<std::size_t>> m_id = std::make_unique<std::atomic<std::size_t>>(0);
};
}  // namespace epix::ecs
