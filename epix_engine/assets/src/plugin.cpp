module;

#include <spdlog/spdlog.h>

module epix.assets;

using namespace assets;
using namespace core;

void assets::log_asset_error(const AssetError& error,
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

void AssetPlugin::build(App& app) {
    app.world_mut().emplace_resource<AssetServer>();
    app.add_systems(Last, into(AssetServer::handle_events));
    app.configure_sets(sets(AssetSystems::HandleEvents, AssetSystems::WriteEvents).chain());
}
void AssetPlugin::finish(App& app) {
    for (auto&& insert : m_assets_inserts) {
        insert(app);
    }
}