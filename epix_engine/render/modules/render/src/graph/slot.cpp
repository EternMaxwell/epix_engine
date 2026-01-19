module epix.render;

import :graph.slot;

using namespace render;
using namespace graph;

std::string_view graph::type_name(SlotType type) {
    switch (type) {
        case SlotType::Buffer:
            return "Buffer";
        case SlotType::Texture:
            return "Texture";
        case SlotType::Sampler:
            return "Sampler";
        case SlotType::Entity:
            return "Entity";
        default:
            return "Unknown";
    }
}
std::optional<std::uint32_t> SlotInfos::get_slot_index(const SlotLabel& label) const {
    return std::visit(
        assets::visitor{
            [](std::uint32_t l) -> std::optional<std::uint32_t> { return l; },
            [this](const std::string& l) -> std::optional<std::uint32_t> {
                if (auto iter = std::ranges::find_if(slots, [&l](const SlotInfo& info) { return info.name == l; });
                    iter != std::ranges::end(slots)) {
                    return static_cast<std::uint32_t>(iter - std::ranges::begin(slots));
                }
                return std::nullopt;
            },
        },
        label.label);
}