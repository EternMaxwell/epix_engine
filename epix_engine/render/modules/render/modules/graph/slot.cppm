export module epix.render:graph.slot;

import epix.assets;
import epix.core;
import webgpu;
import std;

using namespace core;

namespace render::graph {
export enum class SlotType {
    Buffer,   // A buffer
    Texture,  // A texture
    Sampler,  // A sampler
    Entity,   // An entity from ecs world
};
export std::string_view type_name(SlotType type);
export struct SlotInfo {
    std::string name;
    SlotType type;
};
export struct SlotValue {
   private:
    std::variant<Entity, wgpu::Buffer, wgpu::TextureView, wgpu::Sampler> value;

   public:
    SlotValue(const Entity& entity) : value(entity) {}
    SlotValue(const wgpu::Buffer& buffer) : value(buffer) {}
    SlotValue(const wgpu::TextureView& texture) : value(texture) {}
    SlotValue(const wgpu::Sampler& sampler) : value(sampler) {}
    SlotType type() const {
        return std::visit(assets::visitor{
                              [](const Entity&) { return SlotType::Entity; },
                              [](const wgpu::Buffer&) { return SlotType::Buffer; },
                              [](const wgpu::TextureView&) { return SlotType::Texture; },
                              [](const wgpu::Sampler&) { return SlotType::Sampler; },
                          },
                          value);
    }
    bool is_entity() const { return std::holds_alternative<Entity>(value); }
    bool is_buffer() const { return std::holds_alternative<wgpu::Buffer>(value); }
    bool is_texture() const { return std::holds_alternative<wgpu::TextureView>(value); }
    bool is_sampler() const { return std::holds_alternative<wgpu::Sampler>(value); }
    std::optional<Entity> entity() const {
        return std::get_if<Entity>(&value) ? std::optional<Entity>{*std::get_if<Entity>(&value)} : std::nullopt;
    }
    std::optional<wgpu::Buffer> buffer() const {
        return std::get_if<wgpu::Buffer>(&value) ? std::optional<wgpu::Buffer>{*std::get_if<wgpu::Buffer>(&value)}
                                                 : std::nullopt;
    }
    std::optional<wgpu::TextureView> texture() const {
        return std::get_if<wgpu::TextureView>(&value)
                   ? std::optional<wgpu::TextureView>{*std::get_if<wgpu::TextureView>(&value)}
                   : std::nullopt;
    }
    std::optional<wgpu::Sampler> sampler() const {
        return std::get_if<wgpu::Sampler>(&value) ? std::optional<wgpu::Sampler>{*std::get_if<wgpu::Sampler>(&value)}
                                                  : std::nullopt;
    }
};
export struct SlotLabel {
    std::variant<std::uint32_t, std::string> label;
    SlotLabel(std::uint32_t l) : label(l) {}
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
    std::optional<std::reference_wrapper<SlotInfo>> get_slot(const SlotLabel& label) {
        return get_slot_index(label).transform([this](std::uint32_t index) { return std::ref(slots[index]); });
    }
    std::optional<std::reference_wrapper<const SlotInfo>> get_slot(const SlotLabel& label) const {
        return get_slot_index(label).transform([this](std::uint32_t index) { return std::cref(slots[index]); });
    }
    std::optional<std::uint32_t> get_slot_index(const SlotLabel& label) const;
    auto iter() { return std::views::all(slots); }
    auto iter() const { return std::views::all(slots); }
};
}  // namespace render::graph