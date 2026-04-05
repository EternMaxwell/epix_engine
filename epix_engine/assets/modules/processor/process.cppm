module;

#include <spdlog/spdlog.h>

export module epix.assets:processor.process;

import std;
import epix.meta;
import epix.utils;

import :meta;
import :io.reader;
import :server.loader;
import :saver;
import :transformer;

namespace epix::assets {

// Forward declarations
export struct AssetProcessor;
export struct ProcessContext;

// ---- Process Concept ----

/** @brief Concept for an asset processor.
 *  Matches bevy_asset's Process trait.
 *  A processor reads input bytes, processes the value in some way,
 *  and writes processed bytes that can be loaded with Process::OutputLoader. */
export template <typename P>
concept Process = requires(P& p, ProcessContext& ctx, const typename P::Settings& settings, std::ostream& writer) {
    typename P::Settings;
    typename P::OutputLoader;
    requires std::derived_from<typename P::Settings, Settings>;
    requires std::is_default_constructible_v<typename P::Settings>;
    requires AssetLoader<typename P::OutputLoader>;
    {
        p.process(ctx, settings, writer)
    } -> std::same_as<std::expected<typename P::OutputLoader::Settings, std::exception_ptr>>;
};

// ---- ProcessError ----

// Aliases to disambiguate from identically-named variant structs below
using ReaderError_ = AssetReaderError;
using WriterError_ = AssetWriterError;

/** @brief An error encountered during asset processing.
 *  Matches bevy_asset's ProcessError.
 *  Each variant is a separate struct in the process_errors namespace. */
export namespace process_errors {
struct MissingAssetLoaderForExtension {
    std::string extension;
};
struct MissingProcessor {
    std::string name;
};
struct AmbiguousProcessor {
    std::string processor_short_name;
    std::vector<std::string_view> ambiguous_processor_names;
};
struct AssetReaderError {
    AssetPath path;
    ReaderError_ err;
};
struct AssetWriterError {
    AssetPath path;
    WriterError_ err;
};
struct MissingProcessedAssetReader {};
struct MissingProcessedAssetWriter {};
struct ReadAssetMetaError {
    AssetPath path;
    ReaderError_ err;
};
struct DeserializeMetaError {
    std::string msg;
};
struct AssetLoadError {
    std::exception_ptr error;
};
struct WrongMetaType {};
struct AssetSaveError {
    std::exception_ptr error;
};
struct AssetTransformError {
    std::exception_ptr error;
};
struct ExtensionRequired {};
}  // namespace process_errors

export using ProcessError = std::variant<process_errors::MissingAssetLoaderForExtension,
                                         process_errors::MissingProcessor,
                                         process_errors::AmbiguousProcessor,
                                         process_errors::AssetReaderError,
                                         process_errors::AssetWriterError,
                                         process_errors::MissingProcessedAssetReader,
                                         process_errors::MissingProcessedAssetWriter,
                                         process_errors::ReadAssetMetaError,
                                         process_errors::DeserializeMetaError,
                                         process_errors::AssetLoadError,
                                         process_errors::WrongMetaType,
                                         process_errors::AssetSaveError,
                                         process_errors::AssetTransformError,
                                         process_errors::ExtensionRequired>;

// ---- ProcessResult ----

/** @brief The (successful) result of processing an asset.
 *  Matches bevy_asset's ProcessResult. */
export enum class ProcessResultKind {
    Processed,
    SkippedNotChanged,
    Ignored,
};

export struct ProcessResult {
    ProcessResultKind kind;
    std::optional<ProcessedInfo> processed_info;

    static ProcessResult make_processed(ProcessedInfo info) { return {ProcessResultKind::Processed, std::move(info)}; }
    static ProcessResult skipped_not_changed() { return {ProcessResultKind::SkippedNotChanged, std::nullopt}; }
    static ProcessResult ignored() { return {ProcessResultKind::Ignored, std::nullopt}; }
};

/** @brief The final status of processing an asset.
 *  Matches bevy_asset's ProcessStatus. */
export enum class ProcessStatus {
    Processed,
    Failed,
    NonExistent,
};

// ---- ErasedProcessor ----

/** @brief Type-erased processor interface, analogous to bevy_asset's ErasedProcessor. */
export struct ErasedProcessor {
    virtual ~ErasedProcessor() = default;
    /** @brief Process the asset described by context, writing results to writer. */
    virtual std::expected<std::unique_ptr<AssetMetaDyn>, ProcessError> process(ProcessContext& context,
                                                                               const Settings& settings,
                                                                               std::ostream& writer) const = 0;
    /** @brief Deserialize metadata bytes into type-erased AssetMetaDyn. */
    virtual std::expected<std::unique_ptr<AssetMetaDyn>, std::string> deserialize_meta(
        std::span<const std::byte> meta_bytes) const = 0;
    /** @brief Get the type path (full name) of this processor. */
    virtual std::string_view type_path() const = 0;
    /** @brief Get the short type path of this processor. */
    virtual std::string_view short_type_path() const = 0;
    /** @brief Get the default type-erased AssetMetaDyn for this processor. */
    virtual std::unique_ptr<AssetMetaDyn> default_meta() const = 0;
    /** @brief Get default settings for this processor. */
    virtual std::unique_ptr<Settings> default_settings() const = 0;
};

/** @brief Blanket ErasedProcessor implementation for any type satisfying the Process concept. */
template <Process P>
struct ErasedProcessorImpl : P, ErasedProcessor {
    template <typename... Args>
        requires std::constructible_from<P, Args...>
    ErasedProcessorImpl(Args&&... args) : P(std::forward<Args>(args)...) {}

    const P& as_concrete() const { return static_cast<const P&>(*this); }
    P& as_concrete_mut() { return static_cast<P&>(*this); }

    std::expected<std::unique_ptr<AssetMetaDyn>, ProcessError> process(ProcessContext& context,
                                                                       const Settings& settings,
                                                                       std::ostream& writer) const override {
        auto* typed_settings = dynamic_cast<const typename P::Settings*>(&settings);
        if (!typed_settings) {
            return std::unexpected(ProcessError{process_errors::WrongMetaType{}});
        }
        auto result = const_cast<P&>(as_concrete()).process(context, *typed_settings, writer);
        if (!result) {
            return std::unexpected(ProcessError{process_errors::AssetSaveError{result.error()}});
        }
        // Build an AssetMeta with a Load action using the output loader settings
        auto meta    = std::make_unique<AssetMeta<typename P::OutputLoader::Settings, typename P::Settings>>();
        meta->action = AssetActionType::Load;
        meta->loader = std::string(epix::meta::type_id<typename P::OutputLoader>{}.short_name());
        meta->loader_settings_value = std::move(*result);
        return meta;
    }

    std::expected<std::unique_ptr<AssetMetaDyn>, std::string> deserialize_meta(
        std::span<const std::byte> /*meta_bytes*/) const override {
        // TODO: implement actual deserialization when meta serialization is implemented
        return std::make_unique<AssetMeta<typename P::OutputLoader::Settings, typename P::Settings>>();
    }

    std::string_view type_path() const override { return epix::meta::type_id<P>{}.name(); }
    std::string_view short_type_path() const override { return epix::meta::type_id<P>{}.short_name(); }

    std::unique_ptr<AssetMetaDyn> default_meta() const override {
        auto meta       = std::make_unique<AssetMeta<typename P::OutputLoader::Settings, typename P::Settings>>();
        meta->action    = AssetActionType::Process;
        meta->processor = std::string(epix::meta::type_id<P>{}.short_name());
        return meta;
    }

    std::unique_ptr<Settings> default_settings() const override { return std::make_unique<typename P::Settings>(); }
};

// ---- LoadTransformAndSave ----

/** @brief Settings for the LoadTransformAndSave processor.
 *  Matches bevy_asset's LoadTransformAndSaveSettings. */
export template <typename LoaderSettings, typename TransformerSettings, typename SaverSettings>
struct LoadTransformAndSaveSettings : Settings {
    LoaderSettings loader_settings{};
    TransformerSettings transformer_settings{};
    SaverSettings saver_settings{};
};

/** @brief A flexible Process implementation that loads, transforms, and saves assets.
 *  Matches bevy_asset's LoadTransformAndSave<L, T, S>. */
export template <AssetLoader L, AssetTransformer T, AssetSaver S>
    requires std::same_as<typename L::Asset, typename T::AssetInput> &&
             std::same_as<typename T::AssetOutput, typename S::Asset>
struct LoadTransformAndSave {
    using Settings     = LoadTransformAndSaveSettings<typename L::Settings, typename T::Settings, typename S::Settings>;
    using OutputLoader = typename S::OutputLoader;

    T transformer;
    S saver;

    LoadTransformAndSave(T transformer, S saver) : transformer(std::move(transformer)), saver(std::move(saver)) {}

    /** @brief Construct from just a saver, using identity transformer. */
    explicit LoadTransformAndSave(S saver)
        requires std::same_as<T, IdentityAssetTransformer<typename L::Asset>>
        : transformer(), saver(std::move(saver)) {}

    std::expected<typename OutputLoader::Settings, std::exception_ptr> process(ProcessContext& context,
                                                                               const Settings& settings,
                                                                               std::ostream& writer);
};

// ---- GetProcessorError ----

/** @brief An error when retrieving an asset processor.
 *  Matches bevy_asset's GetProcessorError. */
export namespace get_processor_errors {
struct Missing {
    std::string name;
};
struct Ambiguous {
    std::string processor_short_name;
    std::vector<std::string_view> ambiguous_processor_names;
};
}  // namespace get_processor_errors

export using GetProcessorError = std::variant<get_processor_errors::Missing, get_processor_errors::Ambiguous>;

// ---- ProcessContext ----

/** @brief Provides scoped data access to the AssetProcessor during processing.
 *  Matches bevy_asset's ProcessContext.
 *  Only exposes data represented in the asset hash. */
export struct ProcessContext {
   private:
    const AssetProcessor* m_processor;
    const AssetPath* m_path;
    std::istream* m_reader;
    ProcessedInfo* m_new_processed_info;

    friend struct AssetProcessor;

   public:
    ProcessContext(const AssetProcessor& processor,
                   const AssetPath& path,
                   std::istream& reader,
                   ProcessedInfo& new_processed_info)
        : m_processor(&processor), m_path(&path), m_reader(&reader), m_new_processed_info(&new_processed_info) {}

    /** @brief Get the path of the asset being processed. */
    const AssetPath& path() const { return *m_path; }
    /** @brief Get the reader for the asset being processed. */
    std::istream& asset_reader() { return *m_reader; }
    /** @brief Get a reference to the processor. */
    const AssetProcessor& processor() const { return *m_processor; }
    /** @brief Get mutable ref to the new processed info (for adding process dependencies). */
    ProcessedInfo& new_processed_info() { return *m_new_processed_info; }
};

// ---- LoadTransformAndSave::process implementation ----

template <AssetLoader L, AssetTransformer T, AssetSaver S>
    requires std::same_as<typename L::Asset, typename T::AssetInput> &&
             std::same_as<typename T::AssetOutput, typename S::Asset>
std::expected<typename LoadTransformAndSave<L, T, S>::OutputLoader::Settings, std::exception_ptr>
LoadTransformAndSave<L, T, S>::process(ProcessContext& context, const Settings& settings, std::ostream& writer) {
    try {
        // Load the source asset
        auto load_context    = LoadContext(context.processor().get_server(), context.path());
        auto loader_instance = L();
        auto load_result     = loader_instance.load(context.asset_reader(), settings.loader_settings, load_context);
        if (!load_result) {
            return std::unexpected(asset_loader_error_to_exception(load_result.error()));
        }
        auto pre_transformed = TransformedAsset<typename L::Asset>(std::move(*load_result));

        // Transform
        auto post_transformed = transformer.transform(std::move(pre_transformed), settings.transformer_settings);
        if (!post_transformed) {
            return std::unexpected(std::make_exception_ptr(std::runtime_error("Asset transformation failed")));
        }

        // Save
        auto saved       = SavedAsset<typename T::AssetOutput>::from_transformed(*post_transformed);
        auto save_result = saver.save(writer, saved, settings.saver_settings, context.path());
        if (!save_result) {
            return std::unexpected(std::make_exception_ptr(std::runtime_error("Asset save failed")));
        }
        return std::move(*save_result);
    } catch (...) {
        return std::unexpected(std::current_exception());
    }
}

}  // namespace epix::assets
