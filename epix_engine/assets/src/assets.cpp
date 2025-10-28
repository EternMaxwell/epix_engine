#include "epix/assets/assets.hpp"

using namespace epix::assets;

void epix::assets::log_asset_error(const AssetError& error,
                                   const std::string_view& header,
                                   const std::string_view& operation) {
    std::visit(visitor{[&header, &operation](const AssetNotPresent& e) {
                           spdlog::error("[{}:{}] Asset not present at {}", header, operation,
                                         std::visit(visitor{[](const AssetIndex& idx) {
                                                                return std::format("index: {}, generation: {}",
                                                                                   idx.index(), idx.generation());
                                                            },
                                                            [](const uuids::uuid& id) {
                                                                return std::format("uuid: {}", uuids::to_string(id));
                                                            }},
                                                    e));
                       },
                       [&header, &operation](const IndexOutOfBound& e) {
                           spdlog::error("[{}:{}] Index out of bound: {}", header, operation, e.index);
                       },
                       [&header, &operation](const SlotEmpty& e) {
                           spdlog::error("[{}:{}] Slot is empty at index {}", header, operation, e.index);
                       },
                       [&header, &operation](const GenMismatch& e) {
                           spdlog::error(
                               "[{}:{}] Generation mismatch at index {} (current: {}, "
                               "expected: {})",
                               header, operation, e.index, e.current_gen, e.expected_gen);
                       }},
               error);
}