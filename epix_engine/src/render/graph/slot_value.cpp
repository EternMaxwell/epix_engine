#include "epix/render/graph.h"

using namespace epix::render::graph;
using namespace epix::render;

EPIX_API SlotValue::SlotValue(const epix::app::Entity& entity)
    : value(entity) {}
EPIX_API SlotValue::SlotValue(const nvrhi::BufferHandle& buffer)
    : value(buffer) {}
EPIX_API SlotValue::SlotValue(const nvrhi::TextureHandle& texture_view)
    : value(texture_view) {}
EPIX_API SlotValue::SlotValue(const nvrhi::SamplerHandle& sampler)
    : value(sampler) {}
EPIX_API SlotType SlotValue::type() const {
    return std::visit(epix::util::visitor{
                          [](const epix::app::Entity&) -> SlotType {
                              return SlotType::Entity;
                          },
                          [](const nvrhi::BufferHandle&) -> SlotType {
                              return SlotType::Buffer;
                          },
                          [](const nvrhi::TextureHandle&) -> SlotType {
                              return SlotType::Texture;
                          },
                          [](const nvrhi::SamplerHandle&) -> SlotType {
                              return SlotType::Sampler;
                          },
                      },
                      value);
}
EPIX_API bool SlotValue::is_entity() const {
    return std::holds_alternative<epix::app::Entity>(value);
}
EPIX_API bool SlotValue::is_buffer() const {
    return std::holds_alternative<nvrhi::BufferHandle>(value);
}
EPIX_API bool SlotValue::is_texture() const {
    return std::holds_alternative<nvrhi::TextureHandle>(value);
}
EPIX_API bool SlotValue::is_sampler() const {
    return std::holds_alternative<nvrhi::SamplerHandle>(value);
}
EPIX_API std::optional<epix::app::Entity> SlotValue::entity() const {
    if (is_entity()) {
        return std::get<epix::app::Entity>(value);
    }
    return std::nullopt;
}
EPIX_API std::optional<nvrhi::BufferHandle> SlotValue::buffer() const {
    if (is_buffer()) {
        return std::get<nvrhi::BufferHandle>(value);
    }
    return std::nullopt;
}
EPIX_API std::optional<nvrhi::TextureHandle> SlotValue::texture() const {
    if (is_texture()) {
        return std::get<nvrhi::TextureHandle>(value);
    }
    return std::nullopt;
}
EPIX_API std::optional<nvrhi::SamplerHandle> SlotValue::sampler() const {
    if (is_sampler()) {
        return std::get<nvrhi::SamplerHandle>(value);
    }
    return std::nullopt;
}

//==================== SlotLabel ==================//

EPIX_API SlotLabel::SlotLabel(uint32_t l) : label(l) {}
EPIX_API SlotLabel::SlotLabel(const std::string& l) : label(l) {}
EPIX_API SlotLabel::SlotLabel(const char* l) : label(std::string(l)) {}