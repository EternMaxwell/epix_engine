module;
#include <epix/assets.hpp>

export module epix.assets;

export namespace uuids {
using ::uuids::to_string;
using ::uuids::uuid;
}  // namespace uuids

export namespace epix::assets {
using epix::assets::app_preregister_loader;
using epix::assets::app_register_asset;
using epix::assets::app_register_asset_processor;
using epix::assets::app_register_loader;
using epix::assets::app_set_default_asset_processor;
using epix::assets::Asset;
using epix::assets::AssetAction;
using epix::assets::AssetActionMinimal;
using epix::assets::AssetActionType;
using epix::assets::AssetContainer;
using epix::assets::AssetError;
using epix::assets::AssetEvent;
using epix::assets::AssetHash;
using epix::assets::AssetId;
using epix::assets::AssetIndex;
using epix::assets::AssetLoader;
using epix::assets::AssetLoadError;
using epix::assets::AssetLoadFailedEvent;
using epix::assets::AssetMeta;
using epix::assets::AssetMetaCheck;
using epix::assets::AssetMetaDyn;
using epix::assets::AssetMetaMinimal;
using epix::assets::AssetNotPresent;
using epix::assets::AssetPath;
using epix::assets::AssetPlugin;
using epix::assets::AssetProcessor;
using epix::assets::AssetProcessorData;
using epix::assets::AssetReader;
using epix::assets::AssetReaderError;
using epix::assets::Assets;
using epix::assets::AssetSaver;
using epix::assets::AssetServer;
using epix::assets::AssetServerMode;
using epix::assets::AssetSource;
using epix::assets::AssetSourceBuilder;
using epix::assets::AssetSourceBuilders;
using epix::assets::AssetSourceEvent;
using epix::assets::AssetSourceId;
using epix::assets::AssetSources;
using epix::assets::AssetSystems;
using epix::assets::AssetTransformer;
using epix::assets::AssetWatcher;
using epix::assets::AssetWriter;
using epix::assets::AssetWriterError;
using epix::assets::DependencyLoadState;
using epix::assets::deserialize_asset_meta;
using epix::assets::deserialize_meta_minimal;
using epix::assets::deserialize_processed_info;
using epix::assets::EMBEDDED;
using epix::assets::EmbeddedAssetRegistry;
using epix::assets::EmptySettings;
using epix::assets::ErasedAssetLoader;
using epix::assets::ErasedAssetSaver;
using epix::assets::ErasedLoadedAsset;
using epix::assets::ErasedProcessor;
using epix::assets::FileAssetReader;
using epix::assets::FileAssetWatcher;
using epix::assets::FileAssetWriter;
using epix::assets::FileTransactionLogFactory;
using epix::assets::GenMismatch;
using epix::assets::GetProcessorError;
using epix::assets::Handle;
using epix::assets::HandleProvider;
using epix::assets::IdentityAssetTransformer;
using epix::assets::IndexOutOfBound;
using epix::assets::is_settings;
using epix::assets::LoadContext;
using epix::assets::LoadedAsset;
using epix::assets::LoadedFolder;
using epix::assets::LoadedUntypedAsset;
using epix::assets::LoadState;
using epix::assets::LoadStateOK;
using epix::assets::LoadTransformAndSave;
using epix::assets::LoadTransformAndSaveSettings;
using epix::assets::LogEntry;
using epix::assets::LogEntryError;
using epix::assets::LogEntryKind;
using epix::assets::MemoryAssetReader;
using epix::assets::MemoryAssetWatcher;
using epix::assets::MemoryAssetWriter;
using epix::assets::META_FORMAT_VERSION;
using epix::assets::MetaTransform;
using epix::assets::MissingAssetSourceError;
using epix::assets::NestedLoader;
using epix::assets::Process;
using epix::assets::ProcessContext;
using epix::assets::ProcessDependencyInfo;
using epix::assets::ProcessedInfo;
using epix::assets::ProcessError;
using epix::assets::ProcessorState;
using epix::assets::ProcessorTransactionLog;
using epix::assets::ProcessorTransactionLogFactory;
using epix::assets::ProcessResult;
using epix::assets::ProcessResultKind;
using epix::assets::ProcessStatus;
using epix::assets::Reader;
using epix::assets::RecursiveDependencyLoadState;
using epix::assets::SavedAsset;
using epix::assets::serialize_asset_meta;
using epix::assets::serialize_meta_minimal;
using epix::assets::Settings;
using epix::assets::SetTransactionLogFactoryError;
using epix::assets::SlotEmpty;
using epix::assets::StrongHandle;
using epix::assets::TransformedAsset;
using epix::assets::TransformedSubAsset;
using epix::assets::UnapprovedPathMode;
using epix::assets::UntypedAssetConversionError;
using epix::assets::UntypedAssetId;
using epix::assets::UntypedAssetLoadFailedEvent;
using epix::assets::UntypedHandle;
using epix::assets::uuid_handle;
using epix::assets::ValidateLogError;
using epix::assets::VecReader;
using epix::assets::VecWriter;
using epix::assets::VisitAssetDependencies;
using epix::assets::WaitForAssetError;
using epix::assets::Writer;
using ::uuids::to_string;
using ::uuids::uuid;
}  // namespace epix::assets

export namespace epix::assets::asset_meta_check {
using epix::assets::asset_meta_check::Always;
using epix::assets::asset_meta_check::Never;
using epix::assets::asset_meta_check::Paths;
}  // namespace epix::assets::asset_meta_check

export namespace epix::assets::get_processor_errors {
using epix::assets::get_processor_errors::Ambiguous;
using epix::assets::get_processor_errors::Missing;
}  // namespace epix::assets::get_processor_errors

export namespace epix::assets::internal_asset_event {
using epix::assets::internal_asset_event::Failed;
}  // namespace epix::assets::internal_asset_event

export namespace epix::assets::load_error {
using epix::assets::load_error::AssetLoaderException;
using epix::assets::load_error::AssetMetaReadError;
using epix::assets::load_error::AssetReaderError;
using epix::assets::load_error::CannotLoadIgnoredAsset;
using epix::assets::load_error::CannotLoadProcessedAsset;
using epix::assets::load_error::DeserializeMeta;
using epix::assets::load_error::MissingAssetLoader;
using epix::assets::load_error::MissingAssetSourceError;
using epix::assets::load_error::MissingLabel;
using epix::assets::load_error::MissingProcessedAssetReaderError;
using epix::assets::load_error::RequestHandleMismatch;
}  // namespace epix::assets::load_error

export namespace epix::assets::log_entry_errors {
using epix::assets::log_entry_errors::DuplicateTransaction;
using epix::assets::log_entry_errors::EndedMissingTransaction;
using epix::assets::log_entry_errors::UnfinishedTransaction;
}  // namespace epix::assets::log_entry_errors

export namespace epix::assets::memory {
using epix::assets::memory::Data;
using epix::assets::memory::Directory;
using epix::assets::memory::DirectoryError;
using epix::assets::memory::DirEvent;
using epix::assets::memory::DirEventType;
using epix::assets::memory::ExceptionError;
using epix::assets::memory::IoError;
using epix::assets::memory::NotFoundError;
using epix::assets::memory::Value;
}  // namespace epix::assets::memory

export namespace epix::assets::process_errors {
using epix::assets::process_errors::AmbiguousProcessor;
using epix::assets::process_errors::AssetLoadError;
using epix::assets::process_errors::AssetReaderError;
using epix::assets::process_errors::AssetSaveError;
using epix::assets::process_errors::AssetTransformError;
using epix::assets::process_errors::AssetWriterError;
using epix::assets::process_errors::DeserializeMetaError;
using epix::assets::process_errors::ExtensionRequired;
using epix::assets::process_errors::MissingAssetLoaderForExtension;
using epix::assets::process_errors::MissingProcessedAssetReader;
using epix::assets::process_errors::MissingProcessedAssetWriter;
using epix::assets::process_errors::MissingProcessor;
using epix::assets::process_errors::ReadAssetMetaError;
using epix::assets::process_errors::WrongMetaType;
}  // namespace epix::assets::process_errors

export namespace epix::assets::reader_errors {
using epix::assets::reader_errors::HttpError;
using epix::assets::reader_errors::IoError;
using epix::assets::reader_errors::NotFound;
}  // namespace epix::assets::reader_errors

export namespace epix::assets::set_transaction_log_factory_errors {
using epix::assets::set_transaction_log_factory_errors::AlreadyInUse;
}  // namespace epix::assets::set_transaction_log_factory_errors

export namespace epix::assets::source_events {
using epix::assets::source_events::AddedAsset;
using epix::assets::source_events::AddedDirectory;
using epix::assets::source_events::AddedMeta;
using epix::assets::source_events::ModifiedAsset;
using epix::assets::source_events::ModifiedMeta;
using epix::assets::source_events::RemovedAsset;
using epix::assets::source_events::RemovedDirectory;
using epix::assets::source_events::RemovedMeta;
using epix::assets::source_events::RemovedUnknown;
using epix::assets::source_events::RenamedAsset;
using epix::assets::source_events::RenamedDirectory;
using epix::assets::source_events::RenamedMeta;
}  // namespace epix::assets::source_events

export namespace epix::assets::validate_log_errors {
using epix::assets::validate_log_errors::EntryErrors;
using epix::assets::validate_log_errors::ReadLogError;
using epix::assets::validate_log_errors::UnrecoverableError;
}  // namespace epix::assets::validate_log_errors

export namespace epix::assets::wait_for_asset_error {
using epix::assets::wait_for_asset_error::DependencyFailed;
using epix::assets::wait_for_asset_error::Failed;
using epix::assets::wait_for_asset_error::NotLoaded;
}  // namespace epix::assets::wait_for_asset_error

export namespace epix::assets::writer_errors {
using epix::assets::writer_errors::IoError;
}  // namespace epix::assets::writer_errors
