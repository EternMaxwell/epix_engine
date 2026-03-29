export module epix.render:graph.slot;

import epix.assets;
import epix.core;
import epix.utils;
import webgpu;
import std;

using namespace epix::core;

namespace epix::render::graph {
/** @brief Type of data that can flow through a render graph slot. */
export enum class SlotType {
    Buffer,  /**< @brief A GPU buffer. */
    Texture, /**< @brief A texture view. */
    Sampler, /**< @brief A texture sampler. */
    Entity,  /**< @brief An ECS entity reference. */
};
/** @brief Get a human-readable name for a SlotType.
 * @param type The slot type.
 * @return String view of the type name. */
export std::string_view type_name(SlotType type);
/** @brief Describes a named slot with its data type. */
export struct SlotInfo {
    /** @brief Name of the slot. */
    std::string name;
    /** @brief Data type of the slot. */
    SlotType type;
};
/** @brief Type-erased value held in a render graph slot.
 *
 * Can contain an Entity, wgpu::Buffer, wgpu::TextureView, or
 * wgpu::Sampler. Clones GPU handles on copy. */
export struct SlotValue {
   private:
    using variant_t = std::variant<Entity, wgpu::Buffer, wgpu::TextureView, wgpu::Sampler>;
    variant_t value;

   public:
    SlotValue(const SlotValue& other) {
        value = std::visit(utils::visitor{
                               [](const Entity& e) -> variant_t { return Entity(e); },
                               [](const wgpu::Buffer& b) -> variant_t { return b.clone(); },
                               [](const wgpu::TextureView& t) -> variant_t { return t.clone(); },
                               [](const wgpu::Sampler& s) -> variant_t { return s.clone(); },
                           },
                           other.value);
    }
    SlotValue& operator=(const SlotValue& other) {
        value = std::visit(utils::visitor{
                               [](const Entity& e) -> variant_t { return Entity(e); },
                               [](const wgpu::Buffer& b) -> variant_t { return b.clone(); },
                               [](const wgpu::TextureView& t) -> variant_t { return t.clone(); },
                               [](const wgpu::Sampler& s) -> variant_t { return s.clone(); },
                           },
                           other.value);
        return *this;
    }
    SlotValue(SlotValue&&)            = default;
    SlotValue& operator=(SlotValue&&) = default;
    SlotValue(const Entity& entity) : value(entity) {}
    SlotValue(const wgpu::Buffer& buffer) : value(buffer.clone()) {}
    SlotValue(const wgpu::TextureView& texture) : value(texture.clone()) {}
    SlotValue(const wgpu::Sampler& sampler) : value(sampler.clone()) {}
    /** @brief Get the type of the stored slot value.
     *  @return The SlotType enum value. */
    SlotType type() const {
        return std::visit(utils::visitor{
                              [](const Entity&) { return SlotType::Entity; },
                              [](const wgpu::Buffer&) { return SlotType::Buffer; },
                              [](const wgpu::TextureView&) { return SlotType::Texture; },
                              [](const wgpu::Sampler&) { return SlotType::Sampler; },
                          },
                          value);
    }
    /** @brief Check if the slot holds an Entity. */
    bool is_entity() const { return std::holds_alternative<Entity>(value); }
    /** @brief Check if the slot holds a wgpu::Buffer. */
    bool is_buffer() const { return std::holds_alternative<wgpu::Buffer>(value); }
    /** @brief Check if the slot holds a wgpu::TextureView. */
    bool is_texture() const { return std::holds_alternative<wgpu::TextureView>(value); }
    /** @brief Check if the slot holds a wgpu::Sampler. */
    bool is_sampler() const { return std::holds_alternative<wgpu::Sampler>(value); }
    /** @brief Get the stored Entity, if present.
     *  @return The Entity, or std::nullopt. */
    std::optional<Entity> entity() const {
        return std::get_if<Entity>(&value) ? std::optional<Entity>{*std::get_if<Entity>(&value)} : std::nullopt;
    }
    /** @brief Get a clone of the stored wgpu::Buffer, if present.
     *  @return The cloned buffer, or std::nullopt. */
    std::optional<wgpu::Buffer> buffer() const {
        return std::get_if<wgpu::Buffer>(&value)
                   ? std::optional<wgpu::Buffer>{std::get_if<wgpu::Buffer>(&value)->clone()}
                   : std::nullopt;
    }
    /** @brief Get a clone of the stored wgpu::TextureView, if present.
     *  @return The cloned texture view, or std::nullopt. */
    std::optional<wgpu::TextureView> texture() const {
        return std::get_if<wgpu::TextureView>(&value)
                   ? std::optional<wgpu::TextureView>{std::get_if<wgpu::TextureView>(&value)->clone()}
                   : std::nullopt;
    }
    /** @brief Get a clone of the stored wgpu::Sampler, if present.
     *  @return The cloned sampler, or std::nullopt. */
    std::optional<wgpu::Sampler> sampler() const {
        return std::get_if<wgpu::Sampler>(&value)
                   ? std::optional<wgpu::Sampler>{std::get_if<wgpu::Sampler>(&value)->clone()}
                   : std::nullopt;
    }
};
/** @brief Label identifying a slot by index or name. */
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