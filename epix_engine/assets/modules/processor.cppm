module;

#include <spdlog/spdlog.h>

export module epix.assets:processor;

import std;
import epix.meta;
import epix.utils;

import :path;
import :meta;
import :io.reader;
import :server;
import :server.loader;
import :saver;
import :transformer;

namespace assets {

/** @brief Errors that can occur during asset processing. */
export namespace process_error {
struct AssetReaderError {
    AssetPath path;
    assets::AssetReaderError err;
};
struct AssetWriterError {
    AssetPath path;
    assets::AssetWriterError err;
};
struct MissingProcessor {
    std::string processor;
};
struct AmbiguousProcessor {
    std::string processor_short_name;
    std::vector<std::string_view> ambiguous_processor_names;
};
struct MissingLoader {
    std::string type_name;
};
struct ReadAssetMetaError {
    AssetPath path;
    assets::AssetReaderError err;
};
struct DeserializeMetaError {
    std::string message;
};
struct AssetLoadError {
    std::exception_ptr error;
    AssetPath path;
};
struct WrongMetaType {};
struct AssetSaveError {
    std::exception_ptr error;
};
struct AssetTransformError {
    std::exception_ptr error;
};
struct ExtensionRequired {};
}  // namespace process_error

/** @brief Sum type for all processing errors. */
export using ProcessError = std::variant<process_error::MissingLoader,
                                         process_error::MissingProcessor,
                                         process_error::AmbiguousProcessor,
                                         process_error::AssetReaderError,
                                         process_error::AssetWriterError,
                                         process_error::ReadAssetMetaError,
                                         process_error::DeserializeMetaError,
                                         process_error::AssetLoadError,
                                         process_error::WrongMetaType,
                                         process_error::AssetSaveError,
                                         process_error::AssetTransformError,
                                         process_error::ExtensionRequired>;

/** @brief Context provided to a Process implementation during asset processing.
 *  Matches bevy_asset's ProcessContext. */
export struct ProcessContext {
   private:
    std::reference_wrapper<const AssetServer> m_server;
    std::reference_wrapper<const AssetPath> m_path;
    std::unique_ptr<std::istream> m_reader;
    std::optional<std::reference_wrapper<ProcessedInfo>> m_new_processed_info;

   public:
    ProcessContext(const AssetServer& server,
                   const AssetPath& path,
                   std::unique_ptr<std::istream> reader,
                   std::optional<std::reference_wrapper<ProcessedInfo>> new_processed_info = std::nullopt)
        : m_server(server), m_path(path), m_reader(std::move(reader)), m_new_processed_info(new_processed_info) {}

    /** @brief Get the path of the asset being processed. */
    const AssetPath& path() const { return m_path.get(); }
    /** @brief Get the source asset reader stream. */
    std::istream& asset_reader() { return *m_reader; }

    /** @brief Load the source asset using the given loader type. */
    template <AssetLoader L>
    std::expected<ErasedLoadedAsset, ProcessError> load_source_asset(const typename L::Settings& settings) {
        auto loader = m_server.get().get_asset_loader_with_type_name(meta::type_id<L>{}.name());
        if (!loader) {
            return std::unexpected(ProcessError{process_error::MissingLoader{std::string(meta::type_id<L>{}.name())}});
        }

        auto context = LoadContext(m_server.get(), m_path.get());
        auto loaded  = loader->load(*m_reader, settings, context);
        if (!loaded) {
            return std::unexpected(ProcessError{process_error::AssetLoadError{loaded.error(), m_path.get()}});
        }

        if (m_new_processed_info) {
            for (const auto& [dependency_path, full_hash] : loaded->loader_dependencies) {
                m_new_processed_info->get().process_dependencies.push_back(
                    ProcessDependencyInfo{full_hash, dependency_path.string()});
            }
        }

        return std::move(*loaded);
    }
};

/** @brief Concept for an asset processor that transforms raw asset data into processed form.
 *  Matches bevy_asset's Process trait.
 *
 *  Implementations must provide:
 *    - Settings  : a struct derived from assets::Settings, default-constructible
 *    - process() : takes a ProcessContext, writes to ostream, returns expected */
export template <typename T>
concept Process =
    requires(const T& t, ProcessContext& ctx, const typename T::Settings& settings, std::ostream& writer) {
        typename T::Settings;
        typename T::OutputLoader;
        requires std::derived_from<typename T::Settings, Settings>;
        requires std::is_default_constructible_v<typename T::Settings>;
        {
            t.process(ctx, settings, writer)
        } -> std::same_as<std::expected<typename T::OutputLoader::Settings, ProcessError>>;
    };

/** @brief Type-erased process interface. */
export struct ErasedProcessor {
    virtual ~ErasedProcessor() = default;
    /** @brief Run the process. */
    virtual std::expected<std::unique_ptr<Settings>, ProcessError> process(ProcessContext& ctx,
                                                                           const Settings& settings,
                                                                           std::ostream& writer) const = 0;
    /** @brief Get the type name. */
    virtual std::string_view type_name() const = 0;
    /** @brief Create default settings. */
    virtual std::unique_ptr<Settings> default_settings() const = 0;
};

/** @brief Blanket implementation for Process concept types. */
template <Process T>
struct ErasedProcessorImpl : T, ErasedProcessor {
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    ErasedProcessorImpl(Args&&... args) : T(std::forward<Args>(args)...) {}

    const T& as_concrete() const { return static_cast<const T&>(*this); }

    std::expected<std::unique_ptr<Settings>, ProcessError> process(ProcessContext& ctx,
                                                                   const Settings& settings,
                                                                   std::ostream& writer) const override {
        auto* typed = dynamic_cast<const typename T::Settings*>(&settings);
        if (!typed) {
            return std::unexpected(ProcessError{process_error::WrongMetaType{}});
        }
        auto output_settings = as_concrete().process(ctx, *typed, writer);
        if (!output_settings) return std::unexpected(output_settings.error());
        return std::make_unique<typename T::OutputLoader::Settings>(std::move(*output_settings));
    }

    std::string_view type_name() const override { return meta::type_id<T>{}.short_name(); }
    std::unique_ptr<Settings> default_settings() const override { return std::make_unique<typename T::Settings>(); }
};

/** @brief Settings for the LoadTransformAndSave processor.
 *  @tparam LS Loader settings type.
 *  @tparam TS Transformer settings type.
 *  @tparam SS Saver settings type. */
export template <typename LS, typename TS, typename SS>
struct LoadTransformAndSaveSettings : Settings {
    LS loader_settings{};
    TS transformer_settings{};
    SS saver_settings{};
};

/** @brief A generic processor that loads → transforms → saves an asset.
 *  Matches bevy_asset's LoadTransformAndSave.
 *  @tparam L An AssetLoader type.
 *  @tparam T An AssetTransformer type (AssetInput must match L::Asset).
 *  @tparam S An AssetSaver type (Asset must match T::AssetOutput). */
export template <AssetLoader L, AssetTransformer T, AssetSaver S>
    requires std::same_as<typename L::Asset, typename T::AssetInput> &&
             std::same_as<typename T::AssetOutput, typename S::AssetType>
struct LoadTransformAndSave {
    using OutputLoader = typename S::OutputLoader;
    using SettingsType = LoadTransformAndSaveSettings<typename L::Settings, typename T::Settings, typename S::Settings>;
    using Settings     = SettingsType;

    T transformer;
    S saver;

    LoadTransformAndSave(T t, S s) : transformer(std::move(t)), saver(std::move(s)) {}

    std::expected<typename OutputLoader::Settings, ProcessError> process(ProcessContext& ctx,
                                                                         const Settings& settings,
                                                                         std::ostream& writer) const {
        auto loaded = ctx.template load_source_asset<L>(settings.loader_settings);
        if (!loaded) return std::unexpected(loaded.error());

        auto pre_transformed = TransformedAsset<typename L::Asset>::from_loaded(std::move(*loaded));
        if (!pre_transformed) {
            return std::unexpected(ProcessError{process_error::AssetLoadError{
                std::make_exception_ptr(std::runtime_error("Loaded asset type mismatch")), ctx.path()}});
        }

        auto transformed = transformer.transform(std::move(*pre_transformed), settings.transformer_settings);
        if (!transformed) {
            return std::unexpected(ProcessError{process_error::AssetTransformError{transformed.error()}});
        }

        auto saved           = SavedAsset<typename T::AssetOutput>::from_transformed(*transformed);
        auto output_settings = saver.save(writer, saved, settings.saver_settings, ctx.path());
        if (!output_settings) {
            return std::unexpected(ProcessError{
                process_error::AssetSaveError{std::make_exception_ptr(std::runtime_error("Asset saver failed"))}});
        }

        return std::move(*output_settings);
    }
};

/** @brief Central asset processor that manages registered processors and coordinates processing runs.
 *  Matches bevy_asset's AssetProcessor. */
export struct AssetProcessor {
   private:
    std::unordered_map<std::string_view, std::shared_ptr<ErasedProcessor>> m_processors;
    std::unordered_map<std::string, std::string_view> m_default_processors;  // extension → processor type name

   public:
    AssetProcessor()                                 = default;
    AssetProcessor(const AssetProcessor&)            = default;
    AssetProcessor(AssetProcessor&&)                 = default;
    AssetProcessor& operator=(const AssetProcessor&) = default;
    AssetProcessor& operator=(AssetProcessor&&)      = default;

    /** @brief Register a processor by its type. */
    template <Process P>
    void register_processor(P processor) {
        auto name = meta::type_id<P>{}.short_name();
        m_processors.emplace(name, std::make_shared<ErasedProcessorImpl<P>>(std::move(processor)));
    }

    /** @brief Set the default processor for a file extension. */
    template <Process P>
    void set_default_processor(const std::string& extension) {
        auto name = meta::type_id<P>{}.short_name();
        m_default_processors.emplace(extension, name);
    }

    /** @brief Get a processor by type name. */
    std::shared_ptr<ErasedProcessor> get_processor(std::string_view name) const {
        auto it = m_processors.find(name);
        if (it != m_processors.end()) return it->second;
        return nullptr;
    }

    /** @brief Get the default processor for a file extension. */
    std::shared_ptr<ErasedProcessor> get_default_processor(const std::string& extension) const {
        auto it = m_default_processors.find(extension);
        if (it == m_default_processors.end()) return nullptr;
        return get_processor(it->second);
    }
};

}  // namespace assets
