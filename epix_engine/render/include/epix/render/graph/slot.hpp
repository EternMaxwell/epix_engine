#pragma once

#include <epix/assets.hpp>
#include <epix/core.hpp>

#include "../vulkan.hpp"

namespace epix::render::graph {
enum class SlotType {
    Buffer,   // A buffer
    Texture,  // A texture
    Sampler,  // A sampler
    Entity,   // An entity from ecs world
};
std::string_view type_name(SlotType type);
struct SlotInfo {
    std::string name;
    SlotType type;
};
struct SlotValue {
   private:
    std::variant<Entity, nvrhi::BufferHandle, nvrhi::TextureHandle, nvrhi::SamplerHandle> value;

   public:
    SlotValue(const Entity& entity) : value(entity) {}
    SlotValue(const nvrhi::BufferHandle& buffer) : value(buffer) {}
    SlotValue(const nvrhi::TextureHandle& texture) : value(texture) {}
    SlotValue(const nvrhi::SamplerHandle& sampler) : value(sampler) {}
    SlotType type() const {
        return std::visit(assets::visitor{
                              [](const Entity&) { return SlotType::Entity; },
                              [](const nvrhi::BufferHandle&) { return SlotType::Buffer; },
                              [](const nvrhi::TextureHandle&) { return SlotType::Texture; },
                              [](const nvrhi::SamplerHandle&) { return SlotType::Sampler; },
                          },
                          value);
    }
    bool is_entity() const { return std::holds_alternative<Entity>(value); }
    bool is_buffer() const { return std::holds_alternative<nvrhi::BufferHandle>(value); }
    bool is_texture() const { return std::holds_alternative<nvrhi::TextureHandle>(value); }
    bool is_sampler() const { return std::holds_alternative<nvrhi::SamplerHandle>(value); }
    std::optional<Entity> entity() const {
        return std::get_if<Entity>(&value) ? std::optional<Entity>{*std::get_if<Entity>(&value)} : std::nullopt;
    }
    std::optional<nvrhi::BufferHandle> buffer() const {
        return std::get_if<nvrhi::BufferHandle>(&value)
                   ? std::optional<nvrhi::BufferHandle>{*std::get_if<nvrhi::BufferHandle>(&value)}
                   : std::nullopt;
    }
    std::optional<nvrhi::TextureHandle> texture() const {
        return std::get_if<nvrhi::TextureHandle>(&value)
                   ? std::optional<nvrhi::TextureHandle>{*std::get_if<nvrhi::TextureHandle>(&value)}
                   : std::nullopt;
    }
    std::optional<nvrhi::SamplerHandle> sampler() const {
        return std::get_if<nvrhi::SamplerHandle>(&value)
                   ? std::optional<nvrhi::SamplerHandle>{*std::get_if<nvrhi::SamplerHandle>(&value)}
                   : std::nullopt;
    }
};
struct SlotLabel {
    std::variant<uint32_t, std::string> label;
    SlotLabel(uint32_t l) : label(l) {}
    SlotLabel(const std::string& l) : label(l) {}
    SlotLabel(const char* l) : label(std::string(l)) {}
    SlotLabel(const SlotLabel&)            = default;
    SlotLabel(SlotLabel&&)                 = default;
    SlotLabel& operator=(const SlotLabel&) = default;
    SlotLabel& operator=(SlotLabel&&)      = default;
};
struct SlotInfos {
   private:
    std::vector<SlotInfo> slots;

   public:
    SlotInfos(std::span<const SlotInfo> slot_infos) : slots(slot_infos.begin(), slot_infos.end()) {};
    SlotInfos(const SlotInfos&)            = default;
    SlotInfos(SlotInfos&&)                 = default;
    SlotInfos& operator=(const SlotInfos&) = default;
    SlotInfos& operator=(SlotInfos&&)      = default;

    size_t size() const { return slots.size(); }
    bool empty() const { return slots.empty(); }
    SlotInfo* get_slot(const SlotLabel& label) {
        return get_slot_index(label).transform([this](uint32_t index) { return &slots[index]; }).value_or(nullptr);
    }
    const SlotInfo* get_slot(const SlotLabel& label) const {
        return get_slot_index(label).transform([this](uint32_t index) { return &slots[index]; }).value_or(nullptr);
    }
    std::optional<uint32_t> get_slot_index(const SlotLabel& label) const;
    auto iter() { return std::views::all(slots); }
    auto iter() const { return std::views::all(slots); }
};
}  // namespace epix::render::graph