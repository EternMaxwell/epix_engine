#include "epix/render/graph.h"

using namespace epix::render::graph;
using namespace epix::render;

EPIX_API std::string_view epix::render::graph::type_name(SlotType type) {
    switch (type) {
        case SlotType::Buffer:
            return "Buffer";
        case SlotType::Texture:
            return "Texture";
        case SlotType::Sampler:
            return "Sampler";
        case SlotType::Entity:
            return "Entity";
    }
    return "Unknown";
}

//==================== SlotInfos ==================//

EPIX_API SlotInfos::SlotInfos(epix::util::ArrayProxy<SlotInfo> slots)
    : slots(slots.begin(), slots.end()) {}

EPIX_API size_t SlotInfos::size() const { return slots.size(); }
EPIX_API bool SlotInfos::empty() const { return slots.empty(); }
EPIX_API SlotInfo* SlotInfos::get_slot(const SlotLabel& label) {
    auto index = get_slot_index(label);
    if (index) {
        return &slots[*index];
    }
    return nullptr;
}
EPIX_API const SlotInfo* SlotInfos::get_slot(const SlotLabel& label) const {
    auto index = get_slot_index(label);
    if (index) {
        return &slots[*index];
    }
    return nullptr;
}
EPIX_API std::optional<uint32_t> SlotInfos::get_slot_index(
    const SlotLabel& label) const {
    return std::visit(
        epix::util::visitor{
            [](uint32_t l) -> std::optional<uint32_t> { return l; },
            [this](const std::string& l) -> std::optional<uint32_t> {
                if (auto iter = std::find_if(
                        slots.begin(), slots.end(),
                        [&l](const SlotInfo& info) { return info.name == l; });
                    iter != slots.end()) {
                    return static_cast<uint32_t>(iter - slots.begin());
                }
                return std::nullopt;
            },
        },
        label.label);
}
EPIX_API SlotInfos::iterable SlotInfos::iter() {
    return std::ranges::views::all(slots);
}
EPIX_API SlotInfos::const_iterable SlotInfos::iter() const {
    return std::ranges::views::all(slots);
}