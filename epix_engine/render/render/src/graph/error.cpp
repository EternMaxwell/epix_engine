

#include <epix/render.hpp>
#include <epix/render/graph/error.hpp>

namespace epix::render::graph {

namespace {

// Free helper so the std::visit overload sets below contain only non-capturing
// lambdas: the current MSVC Insiders compiler regresses on the mixed
// capture/non-capture lambda visitor (_Variant_all_visit_results_same), and this
// TU had not been recompiled since 2026-07-22 (an older compiler accepted it).
std::string slot_label_name(const SlotLabel& slot) {
    if (std::holds_alternative<std::uint32_t>(slot.label)) {
        return std::to_string(std::get<std::uint32_t>(slot.label));
    }
    return std::get<std::string>(slot.label);
}

}  // namespace

std::string GraphError::to_string() const {
    return std::visit(
        epix::utils::visitor{
            [](const NodeNotPresent& e) -> std::string {
                return "Node not present: " + std::string(e.label.type_index().short_name());
            },
            [](const SubGraphExists& e) -> std::string {
                return "SubGraph already exists: " + std::string(e.id.type_index().short_name());
            },
            [](const EdgeError& e) -> std::string {
                return std::visit(
                    epix::utils::visitor{
                        [](const EdgeNodesNotPresent& e) -> std::string {
                            return std::format(
                                "Edge nodes not present: output(node={}, "
                                "present={}) input(node={}, present={})",
                                e.output_node.type_index().short_name(), !e.missing_output,
                                e.input_node.type_index().short_name(), !e.missing_input);
                        },
                        [](const SlotNotPresent& e) -> std::string {
                            return std::format(
                                "Edge node slot not present: output(node={}, "
                                "slot={}, present={}) input(node={}, "
                                "slot={}, present={})",
                                e.output_node.type_index().short_name(), slot_label_name(e.output_slot),
                                !e.missing_in_output, e.input_node.type_index().short_name(),
                                slot_label_name(e.input_slot), !e.missing_in_input);
                        },
                        [](const EdgeAlreadyExists& e) -> std::string {
                            return std::format("Edge already exists: output(node={}, index={}) "
                                               "input(node={}, index={})",
                                               e.output_node.type_index().short_name(), e.output_index,
                                               e.input_node.type_index().short_name(), e.input_index);
                        },
                        [](const EdgeDoesNotExist& e) -> std::string {
                            return std::format("Edge does not exist: output(node={}, index={}) "
                                               "input(node={}, index={})",
                                               e.output_node.type_index().short_name(), e.output_index,
                                               e.input_node.type_index().short_name(), e.input_index);
                        },
                        [](const InputSlotOccupied& e) -> std::string {
                            return std::format(
                                "Input slot occupied: input(node={}, index={}) "
                                "current output(node={}, index={}) required "
                                "output(node={}, index={})",
                                e.input_node.type_index().short_name(), e.input_index,
                                e.current_output_node.type_index().short_name(), e.current_output_index,
                                e.required_output_node.type_index().short_name(), e.required_output_index);
                        },
                        [](const SlotTypeMismatch& e) -> std::string {
                            return std::format(
                                "Slot type mismatch: output(node={}, index={}, "
                                "type={}) input(node={}, index={}, type={})",
                                e.output_node.type_index().short_name(), e.output_index,
                                type_name(e.output_type), e.input_node.type_index().short_name(),
                                e.input_index, type_name(e.input_type));
                        }},
                    e);
            },
        },
        *this);
}

}  // namespace epix::render::graph
