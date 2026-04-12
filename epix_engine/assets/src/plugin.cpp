module;

#include <spdlog/spdlog.h>

module epix.assets;

using namespace epix::assets;
using namespace epix::core;

void AssetPlugin::build(App& app) {
    spdlog::debug("[assets] Building AssetPlugin (mode={}).", static_cast<int>(mode));
    auto& world = app.world_mut();

    EmbeddedAssetRegistry embedded;

    // Bevy-style: fetch builders from World, emplace if absent.
    auto& builders = world.resource_or_emplace<AssetSourceBuilders>();
    auto processed = (mode != AssetServerMode::Unprocessed) ? processed_file_path : std::nullopt;
    builders.init_default(file_path, processed);
    embedded.register_source(builders);

    // If the processor is going to be active AND an embedded_processed_path is configured,
    // upgrade the embedded source builder to use file-backed processed IO so the embedded assets
    // participate fully in the processor pipeline.
    // IMPORTANT: only do this when the processor will actually start; otherwise, the
    // ProcessorGatedReader wraps the embedded source and blocks in wait_until_processed forever
    // (tests or apps that never run Startup would deadlock).
    const bool use_asset_processor_effective =
        (mode != AssetServerMode::Unprocessed) && use_asset_processor_override.value_or(true) && processed.has_value();
    if (use_asset_processor_effective && embedded_processed_path.has_value()) {
        auto embedded_proc_path = std::filesystem::absolute(*embedded_processed_path);
        auto builder_opt        = builders.get(AssetSourceId(std::string(EMBEDDED)));
        if (builder_opt) {
            auto& builder = builder_opt->get();
            builder.with_processed_reader([embedded_proc_path]() -> std::unique_ptr<AssetReader> {
                return std::make_unique<FileAssetReader>(embedded_proc_path);
            });
            builder.with_ungated_processed_reader([embedded_proc_path]() -> std::unique_ptr<AssetReader> {
                return std::make_unique<FileAssetReader>(embedded_proc_path);
            });
            builder.with_processed_writer([embedded_proc_path]() -> std::unique_ptr<AssetWriter> {
                return std::make_unique<FileAssetWriter>(embedded_proc_path);
            });
        }
    }

    for (auto& [id, builder] : m_source_builders) {
        builders.insert(std::move(id), std::move(builder));
    }
    m_source_builders.clear();

    const bool watch = watch_for_changes_override.value_or(false);

    switch (mode) {
        case AssetServerMode::Unprocessed: {
            auto sources = std::make_shared<AssetSources>(builders.build_sources(watch, false));
            world.emplace_resource<AssetServer>(std::move(sources), AssetServerMode::Unprocessed, meta_check, watch,
                                                unapproved_path_mode);
            break;
        }
        case AssetServerMode::Processed: {
            const bool use_asset_processor = use_asset_processor_override.value_or(true);
            if (use_asset_processor && processed.has_value()) {
                auto processor = AssetProcessor(builders, watch,
                                                std::make_unique<FileTransactionLogFactory>(processed.value() / "log"));

                world.emplace_resource<AssetServer>(
                    processor.sources(), processor.get_server().get_loaders(), AssetServerMode::Processed,
                    AssetMetaCheck{asset_meta_check::Always{}}, watch, unapproved_path_mode);
                world.emplace_resource<AssetProcessor>(std::move(processor));
                // Wire the processor extension check so the server skips the processed reader for
                // extensions without a registered processor (those are read directly from source).
                {
                    auto proc = world.resource<AssetProcessor>();  // cheap copy (shared_ptr)
                    world.resource<AssetServer>().set_processor_check(
                        [proc](std::string_view ext) -> bool { return proc.has_processor_for_extension(ext); });
                }
                app.add_systems(Startup, into(AssetProcessor::start));
            } else {
                if (!processed.has_value()) {
                    spdlog::warn(
                        "[assets] AssetPlugin is in Processed mode but no processed_file_path was set. Defaulting to "
                        "unprocessed behavior.");
                }
                auto sources = std::make_shared<AssetSources>(builders.build_sources(false, watch));
                world.emplace_resource<AssetServer>(std::move(sources), AssetServerMode::Processed,
                                                    AssetMetaCheck{asset_meta_check::Always{}}, watch,
                                                    unapproved_path_mode);
            }
            break;
        }
    }

    world.emplace_resource<EmbeddedAssetRegistry>(std::move(embedded));

    // Mirror Bevy: init_asset::<LoadedFolder>() and init_asset::<LoadedUntypedAsset>()
    app_register_asset<LoadedFolder>(app);
    app_register_asset<LoadedUntypedAsset>(app);

    app.add_events<UntypedAssetLoadFailedEvent>();

    app.add_systems(Last, into(AssetServer::handle_internal_events));
    app.configure_sets(sets(AssetSystems::HandleEvents, AssetSystems::WriteEvents).chain());
}

void AssetPlugin::finish(App& app) { (void)app; }

AssetPlugin& AssetPlugin::register_asset_source(AssetSourceId id, AssetSourceBuilder source) {
    m_source_builders.emplace_back(std::move(id), std::move(source));
    return *this;
}
