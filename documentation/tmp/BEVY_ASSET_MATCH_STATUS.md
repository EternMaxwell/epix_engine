# Bevy Asset Module — C++ Match Status (Coroutine Focus)

Reference: `bevy_asset` 0.16 vs `epix_engine/assets` (C++23 modules, clang-23)

**Primary goal**: migrate sync/blocking asset pipeline to C++ coroutines matching bevy's async/await.

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Functionally equivalent |
| ⚠️ | Works but signature/behavior diverges (note why) |
| ❌ | Declaration differs significantly — must fix |
| 🚫 | Missing entirely — must implement |
| N/A | Non-implementable; confirmed with user |

## Cross-Cutting Translation Rules

| Rust / Bevy | C++ / epix_engine |
|-------------|-------------------|
| `async fn f(&self) -> T` | `asio::awaitable<T> f() const` |
| `async fn f(self) -> T` | `asio::awaitable<T> f(this Self self)` (deducing this) |
| `impl ConditionalSendFuture<Output=T>` | `asio::awaitable<T>` |
| `BoxedFuture<'a, T>` | `asio::awaitable<T>` (type-erased via virtual) |
| `IoTaskPool::get().spawn(async { ... })` | `Task<T>` (epix::tasks, spawned on thread pool) |
| `task.detach()` | `task.detach()` |
| `Box<dyn Reader>` (AsyncRead) | Abstract `Reader` class (async read returning awaitable) |
| `Writer` (dyn AsyncWrite) | Abstract `Writer` class (async write returning awaitable) |
| `poll_fn(\|cx\| ...)` + `Waker` | `co_await` on custom awaitable |
| `Arc<T>` | `std::shared_ptr<T>` |
| `TypeId` | `meta::type_index` |
| `fn method(self, ...)` (by-value) | `Ret method(this Self self, ...)` (deducing this) |
| `fn method(&self, ...)` (by-ref) | `Ret method() const` |

**IMPORTANT**: `asio::awaitable<T>` for all bevy `async fn`. `Task<T>` ONLY where bevy uses `bevy_tasks::Task` (spawn sites).

## Coroutine Migration Overview

Current C++ state: **all sync/blocking**. Bevy state: **async throughout**.

| Layer | Bevy | C++ current (sync) | C++ target |
|-------|------|--------------------|-----------------------|
| `AssetLoader::load` | `async fn → impl Future` | `expected<Asset> load(istream&, ...)` | `awaitable<expected<Asset>> load(Reader&, ...)` |
| `ErasedAssetLoader::load` | `BoxedFuture<Result<ErasedLoadedAsset>>` | `expected<ErasedLoadedAsset> load(istream&, ...)` | `awaitable<expected<ErasedLoadedAsset>> load(Reader&, ...)` |
| `AssetSaver::save` | `async fn → impl Future` | `expected<Settings> save(ostream&, ...)` | `awaitable<expected<Settings>> save(Writer&, ...)` |
| `ErasedAssetSaver::save` | `BoxedFuture<Result<()>>` | `expected<void> save(ostream&, ...)` | `awaitable<expected<void>> save(Writer&, ...)` |
| `AssetReader::read` | `async fn → impl Future` | `expected<unique_ptr<istream>> read(path)` | `awaitable<expected<unique_ptr<Reader>>> read(path)` |
| `AssetReader::read_meta` | `async fn → impl Future` | `expected<unique_ptr<istream>> read_meta(path)` | `awaitable<expected<unique_ptr<Reader>>> read_meta(path)` |
| `AssetWriter::write` | `async fn → impl Future` | `expected<unique_ptr<ostream>> write(path)` | `awaitable<expected<unique_ptr<Writer>>> write(path)` |
| `AssetServer::load_internal` | `async fn` | sync `void` | `awaitable<expected<optional<UntypedHandle>>>` |
| `AssetServer::spawn_load_task` | `IoTaskPool::spawn(async)` → **Task** | sync `detach_task(lambda)` | **`Task<void>`** spawned on IOTaskPool |
| `AssetServer::get_meta_loader_and_reader` | `async fn` | sync `optional<MetaLoaderReader>` | `awaitable<expected<MetaLoaderReader>>` |
| `AssetServer::load_with_settings_loader_and_reader` | `async fn` | sync `expected<ErasedLoadedAsset>` | `awaitable<expected<ErasedLoadedAsset>>` |
| `AssetServer::wait_for_asset_id` | `async fn` (poll_fn) | blocking `std::promise` | `awaitable<expected<void>>` |
| `AssetServer::load_untyped_async` | `async fn` | absent | `awaitable<expected<UntypedHandle>>` |
| `LoadContext::load_direct` | `async fn` | sync `expected<LoadedAsset<A>>` | `awaitable<expected<LoadedAsset<A>>>` |
| `NestedLoader(Immediate)::load` | `async fn` | absent | `awaitable<expected<LoadedAsset<A>>>` |
| `NestedLoader(Deferred)::load` | **sync** (returns handle) | sync (returns handle) | sync — no change needed |
| `Process::process` | `async fn → impl Future` | sync `expected<ProcessResult>` | `awaitable<expected<ProcessResult>>` |
| `AssetServer::add_async` | `IoTaskPool::spawn(async)` → **Task** | sync `detach_task(lambda)` | **`Task<void>`** spawned on IOTaskPool |
| `AssetServer::reload_internal` | `IoTaskPool::spawn(async).detach()` | sync `detach_task(lambda)` | spawn **`Task<void>`** then `.detach()` |
| `AssetServer::load_folder_internal` | `IoTaskPool::spawn(async).detach()` | sync `detach_task(lambda)` | spawn **`Task<void>`** then `.detach()` |

---

## 1. IO Layer (`io/mod.rs` → `io/reader.cppm`)

Must be migrated first — everything above depends on async IO.

### Reader / Writer abstractions

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `Reader` trait | `AsyncRead + Unpin + Send + Sync` | `std::istream&` (no abstraction) | Abstract `Reader` class | ❌ |
| `Reader::read_to_end` | `fn → StackFuture<io::Result<usize>>` | N/A (istream::read) | `awaitable<expected<size_t>> read_to_end(vector<uint8_t>&)` | 🚫 |
| `Reader::seekable` | `fn → Result<&mut dyn SeekableReader>` | N/A (istream::seekg) | `expected<SeekableReader*> seekable()` | 🚫 |
| `SeekableReader` | `Reader + AsyncSeek` | N/A | Subclass with `awaitable<expected<size_t>> seek(SeekFrom)` | 🚫 |
| `VecReader` | in-memory `Vec<u8>` backed Reader | `VecReader` wraps `istringstream` | `VecReader` implementing async `Reader` | ❌ |
| `Writer` type alias | `dyn AsyncWrite + Unpin + Send + Sync` | `std::ostream&` | Abstract `Writer` class | ❌ |

### AssetReader

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `AssetReader::read` | `fn → impl Future<Result<impl Reader>>` | `expected<unique_ptr<istream>> read(path)` | `awaitable<expected<unique_ptr<Reader>>> read(path)` | ❌ |
| `AssetReader::read_meta` | `fn → impl Future<Result<impl Reader>>` | `expected<unique_ptr<istream>> read_meta(path)` | `awaitable<expected<unique_ptr<Reader>>> read_meta(path)` | ❌ |
| `AssetReader::read_directory` | `fn → impl Future<Result<PathStream>>` | `expected<input_iterable<path>>` | `awaitable<expected<input_iterable<path>>>` | ❌ |
| `AssetReader::is_directory` | `fn → impl Future<Result<bool>>` | `expected<bool>` | `awaitable<expected<bool>>` | ❌ |
| `AssetReader::read_meta_bytes` | `fn → impl Future<Result<Vec<u8>>>` (default) | sync `read_meta_bytes` | `awaitable<expected<vector<uint8_t>>>` (default co_await read_meta) | ❌ |
| `ErasedAssetReader` | type-erased, `BoxedFuture` returns | virtual class, sync | virtual class returning `awaitable<...>` | ❌ |

### AssetWriter

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `AssetWriter::write` | `fn → impl Future<Result<Box<Writer>>>` | `expected<unique_ptr<ostream>>` | `awaitable<expected<unique_ptr<Writer>>>` | ❌ |
| `AssetWriter::write_meta` | `fn → impl Future<Result<Box<Writer>>>` | `expected<unique_ptr<ostream>>` | `awaitable<expected<unique_ptr<Writer>>>` | ❌ |
| `AssetWriter::remove` | `fn → impl Future<Result<()>>` | `expected<void>` | `awaitable<expected<void>>` | ❌ |
| `AssetWriter::remove_meta` | `fn → impl Future<Result<()>>` | `expected<void>` | `awaitable<expected<void>>` | ❌ |
| `AssetWriter::rename` | `fn → impl Future<Result<()>>` | `expected<void>` | `awaitable<expected<void>>` | ❌ |
| `AssetWriter::rename_meta` | `fn → impl Future<Result<()>>` | `expected<void>` | `awaitable<expected<void>>` | ❌ |
| `AssetWriter::write_bytes` | `fn → impl Future<Result<()>>` (default) | sync `write_bytes` | `awaitable<expected<void>>` (default co_await write) | ❌ |
| `AssetWriter::write_meta_bytes` | `fn → impl Future<Result<()>>` (default) | sync `write_meta_bytes` | `awaitable<expected<void>>` (default co_await write_meta) | ❌ |
| `ErasedAssetWriter` | type-erased, `BoxedFuture` returns | virtual class, sync | virtual class returning `awaitable<...>` | ❌ |

### Concrete IO implementations

| Entity | C++ current | C++ target | Status |
|--------|-------------|------------|--------|
| `FileAssetReader` | sync filesystem reads | wrap sync in `awaitable` (post to IO executor) | ❌ |
| `FileAssetWriter` | sync filesystem writes | wrap sync in `awaitable` | ❌ |
| `MemoryAssetReader` | sync memory reads | trivial `awaitable` wrap (co_return immediately) | ❌ |
| `ProcessorGatedReader` | sync delegating reader | `awaitable` delegating to inner reader | ❌ |
| `EmbeddedAssetReader` | sync embedded reads | trivial `awaitable` wrap | ❌ |

---

## 2. Loader Layer (`loader.rs` → `server/loader.cppm`)

### AssetLoader concept

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `AssetLoader::load` | `fn → impl ConditionalSendFuture<Result<Asset>>` | `expected<Asset> load(istream&, settings, context)` | `awaitable<expected<Asset>> load(Reader&, settings, context)` | ❌ |
| `AssetLoader::Asset` | associated type | `T::Asset` | same | ✅ |
| `AssetLoader::Settings` | associated type | `T::Settings` | same | ✅ |
| `AssetLoader::Error` | associated type | `T::Error` | same | ✅ |
| `AssetLoader::extensions` | `fn → &[&str]` | `extensions() → span<string_view>` | same (sync is correct) | ✅ |

### ErasedAssetLoader

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `ErasedAssetLoader::load` | `fn → BoxedFuture<Result<ErasedLoadedAsset>>` | `expected<ErasedLoadedAsset> load(istream&, ...)` | `awaitable<expected<ErasedLoadedAsset>> load(Reader&, ...)` | ❌ |
| `ErasedAssetLoader::type_name` | `fn → &str` | `type_name() → string_view` | same | ✅ |
| `ErasedAssetLoader::type_id` | `fn → TypeId` | `type_id() → type_index` | same | ✅ |
| `ErasedAssetLoader::asset_type_name` | `fn → &str` | `asset_type_name() → string_view` | same | ✅ |
| `ErasedAssetLoader::asset_type_id` | `fn → TypeId` | `asset_type_id() → type_index` | same | ✅ |
| `ErasedAssetLoader::extensions` | `fn → &[&str]` | `extensions() → span<string_view>` | same | ✅ |
| `ErasedAssetLoader::default_meta` | `fn → Box<dyn AssetMetaDyn>` | `default_meta() → unique_ptr<AssetMetaDyn>` | same | ✅ |

### LoadedAsset / ErasedLoadedAsset

| Entity | Bevy | C++ current | C++ target | Status | Notes |
|--------|------|-------------|------------|--------|-------|
| `LoadedAsset<A>::take(self)` | by-value `fn take(self) → A` | `take() → A` (member fn) | `A take(this Self self)` (deducing this) | ❌ | deducing this for by-value self |
| `ErasedLoadedAsset::from_loaded<A>(self)` | `fn from_loaded(asset: LoadedAsset<A>)` → self | factory | same | ✅ |
| `ErasedLoadedAsset::downcast<A>(self)` | by-value `fn downcast(self)` | `downcast() → expected<LoadedAsset<A>>` | `expected<LoadedAsset<A>> downcast(this Self self)` | ❌ | deducing this |

### LoadContext

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `LoadContext::loader` | `fn → NestedLoader` (builder) | `loader() → NestedLoader` | same — sync is correct | ✅ |
| `LoadContext::load_direct` | `async fn → Result<LoadedAsset<A>>` | sync `expected<LoadedAsset<A>>` | `awaitable<expected<LoadedAsset<A>>>` | ❌ |
| `LoadContext::load_direct_untyped` | `async fn → Result<ErasedLoadedAsset>` | sync `expected<ErasedLoadedAsset>` | `awaitable<expected<ErasedLoadedAsset>>` | ❌ |
| `LoadContext::load_direct_with_reader` | `async fn` | absent | `awaitable<expected<LoadedAsset<A>>>` | 🚫 |
| `LoadContext::load_direct_internal` | `async fn` (private) | sync internal | `awaitable<...>` (private) | ❌ |
| `LoadContext::finish` | `fn finish(self) → ErasedLoadedAsset` | `finish() → ErasedLoadedAsset` | `finish(this Self self)` (deducing this) | ❌ |

### NestedLoader (`loader_builders.rs`)

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `NestedLoader<StaticTyped, Deferred>::load` | **sync** `fn → Handle<A>` | sync `load → Handle<A>` | same — sync is correct | ✅ |
| `NestedLoader<DynamicTyped, Deferred>::load` | **sync** `fn → UntypedHandle` | absent | sync `load → UntypedHandle` | 🚫 |
| `NestedLoader<UnknownTyped, Deferred>::load` | **sync** `fn → Handle<LoadedUntypedAsset>` | absent | sync `load → Handle<LoadedUntypedAsset>` | 🚫 |
| `NestedLoader::with_settings` | builder method | present | same | ✅ |
| `NestedLoader::immediate()` | mode switch | absent | add `immediate()` builder | 🚫 |
| `NestedLoader::deferred()` | mode switch | absent (only mode) | add `deferred()` builder | 🚫 |
| `NestedLoader::with_dynamic_type` | typing switch | absent | add `with_dynamic_type()` builder | 🚫 |
| `NestedLoader::with_unknown_type` | typing switch | absent | add `with_unknown_type()` builder | 🚫 |
| `NestedLoader<StaticTyped, Immediate>::load` | `async fn → Result<LoadedAsset<A>>` | absent | `awaitable<expected<LoadedAsset<A>>>` | 🚫 |
| `NestedLoader<DynamicTyped, Immediate>::load` | `async fn → Result<ErasedLoadedAsset>` | absent | `awaitable<expected<ErasedLoadedAsset>>` | 🚫 |
| `NestedLoader<UnknownTyped, Immediate>::load` | `async fn → Result<ErasedLoadedAsset>` | absent | `awaitable<expected<ErasedLoadedAsset>>` | 🚫 |
| `NestedLoader::with_reader` | Immediate mode builder | absent | add `with_reader(Reader&)` builder | 🚫 |

---

## 3. Saver & Transformer Layer (`saver.rs`, `transformer.rs`)

### AssetSaver concept

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `AssetSaver::save` | `fn → impl ConditionalSendFuture<Result<Settings>>` | `expected<Settings> save(ostream&, ...)` | `awaitable<expected<Settings>> save(Writer&, ...)` | ❌ |
| `AssetSaver::Asset` | associated type | `T::Asset` | same | ✅ |
| `AssetSaver::Settings` | associated type | `T::Settings` | same | ✅ |
| `AssetSaver::OutputLoader` | associated type | `T::OutputLoader` | same | ✅ |
| `AssetSaver::Error` | associated type | `T::Error` | same | ✅ |

### ErasedAssetSaver

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `ErasedAssetSaver::save` | `fn → BoxedFuture<Result<()>>` | `expected<void> save(ostream&, ...)` | `awaitable<expected<void>> save(Writer&, ...)` | ❌ |
| `ErasedAssetSaver::type_name` | `fn → &str` | `type_name() → string_view` | same | ✅ |
| `ErasedAssetSaver::asset_type_name/id` | present | present | same | ✅ |

### SavedAsset

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `SavedAsset<A>::from_loaded(self)` | by-value self | factory (takes ref) | `from_loaded(this Self self, ...)` (deducing this) | ⚠️ |
| `SavedAsset<A>::get<B>` | `fn(&self) → Option<&B>` | `get() → optional<const B&>` | same | ✅ |

### AssetTransformer concept

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `AssetTransformer::transform` | `fn → impl ConditionalSendFuture<Result<TransformedAsset>>` | `expected<TransformedAsset> transform(asset, settings)` | `awaitable<expected<TransformedAsset>> transform(asset, settings)` | ❌ |
| `AssetTransformer::AssetInput` | associated type | `T::AssetInput` | same | ✅ |
| `AssetTransformer::AssetOutput` | associated type | `T::AssetOutput` | same | ✅ |
| `AssetTransformer::Settings` | associated type | `T::Settings` | same | ✅ |
| `AssetTransformer::Error` | associated type | `T::Error` | same | ✅ |

---

## 4. AssetServer Layer (`server/mod.rs` → `server/mod.cppm`)

### Spawn sites (bevy uses Task here → C++ uses Task)

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `spawn_load_task` | `IoTaskPool::spawn(async { load_internal().await })` → stores **Task** in pending_tasks | sync `detach_task(lambda)` | spawn **`Task<void>`** storing in pending_tasks | ❌ |
| `add_async` | `IoTaskPool::spawn(async { future.await ... })` → stores **Task** | sync `detach_task(lambda)` | spawn **`Task<void>`** storing in pending_tasks | ❌ |
| `reload_internal` | `IoTaskPool::spawn(async).detach()` | sync `detach_task(lambda)` | spawn **`Task<void>`** then `.detach()` | ❌ |
| `load_folder_internal` | `IoTaskPool::spawn(async).detach()` | sync `detach_task(lambda)` | spawn **`Task<void>`** then `.detach()` | ❌ |
| `load_unknown_type_with_meta_transform` | `IoTaskPool::spawn(async)` → stores **Task** | absent | spawn **`Task<void>`** | 🚫 |

### Async methods (bevy uses async fn → C++ uses awaitable)

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `load_internal` | `async fn → Result<Option<UntypedHandle>>` | sync `void` | `awaitable<expected<optional<UntypedHandle>>>` | ❌ |
| `get_meta_loader_and_reader` | `async fn → Result<(meta, loader, reader)>` | sync `optional<MetaLoaderReader>` | `awaitable<expected<MetaLoaderReader>>` | ❌ |
| `load_with_settings_loader_and_reader` | `async fn → Result<ErasedLoadedAsset>` | sync `expected<ErasedLoadedAsset>` | `awaitable<expected<ErasedLoadedAsset>>` | ❌ |
| `wait_for_asset_id` | `async fn` (poll_fn + Waker) | blocking `std::promise::get_future().get()` | `awaitable<expected<void>>` (co_await custom awaitable) | ❌ |
| `wait_for_asset` | `async fn` (delegates) | blocking (delegates) | `awaitable<expected<void>>` | ❌ |
| `wait_for_asset_untyped` | `async fn` (delegates) | blocking (delegates) | `awaitable<expected<void>>` | ❌ |
| `load_untyped_async` | `async fn → Result<UntypedHandle>` | absent | `awaitable<expected<UntypedHandle>>` | 🚫 |
| `write_default_loader_meta_file_for_path` | `async fn` | absent | `awaitable<expected<void>>` | 🚫 |
| `get_asset_loader_with_extension` | `async fn` (waits for pending) | sync (blocks on broadcast) | `awaitable<shared_ptr<ErasedAssetLoader>>` | ❌ |
| `get_asset_loader_with_type_name` | `async fn` | sync | `awaitable<shared_ptr<ErasedAssetLoader>>` | ❌ |
| `get_path_asset_loader` | `async fn` | sync | `awaitable<shared_ptr<ErasedAssetLoader>>` | ❌ |
| `get_asset_loader_with_asset_type_id` | `async fn` | sync | `awaitable<shared_ptr<ErasedAssetLoader>>` | ❌ |
| `get_asset_loader_with_asset_type` | `async fn` | sync | `awaitable<shared_ptr<ErasedAssetLoader>>` | ❌ |
| `load_direct_untyped` | `async fn` (Bevy name: `load_direct`) | sync `expected<ErasedLoadedAsset>` | `awaitable<expected<ErasedLoadedAsset>>` | ❌ |
| `load_direct_with_reader_untyped` | `async fn` | sync `expected<ErasedLoadedAsset>` | `awaitable<expected<ErasedLoadedAsset>>` | ❌ |

### Sync methods (no change needed for coroutine migration)

| Entity | Status | Notes |
|--------|--------|-------|
| `load<A>(path)` | ✅ | sync — kicks off spawn_load_task internally |
| `load_with_settings<A>` | ✅ | sync wrapper |
| `get_or_create_path_handle` | ✅ | sync |
| `get_or_create_path_handle_erased` | ✅ | sync |
| `load_asset_untyped` | ✅ | sync — inserts + sends Loaded event |
| `get_load_states` / `load_state` / etc. | ✅ | sync queries |
| `get_handle` / `get_id_handle` / etc. | ✅ | sync queries |
| `get_path` / `get_path_id` / etc. | ✅ | sync queries |
| `is_managed` / `mode` / `watching_for_changes` | ✅ | sync queries |
| `process_handle_destruction` | ✅ | sync |
| `handle_internal_events` | ✅ | sync system |

---

## 5. Processor Layer (`processor/mod.rs`, `processor/process.rs`)

### Process concept

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `Process::process` | `fn → impl ConditionalSendFuture<Result<ProcessResult>>` | `expected<ProcessResult> process(...)` | `awaitable<expected<ProcessResult>> process(...)` | ❌ |

### AssetProcessor spawn sites (bevy uses Task)

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `AssetProcessor::start` | `IoTaskPool::spawn(async).detach()` (x2) | sync start | spawn **`Task<void>`** then `.detach()` | ❌ |
| `spawn_source_change_event_listeners` | `IoTaskPool::spawn(async).detach()` per source | sync | spawn **`Task<void>`** then `.detach()` | ❌ |
| `execute_processing_tasks` | `IoTaskPool::spawn(async).detach()` per asset | sync | spawn **`Task<void>`** then `.detach()` | ❌ |

### AssetProcessor async methods (awaitable)

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `process_asset` | `async fn` | sync | `awaitable<expected<ProcessResult>>` | ❌ |
| `process_asset_internal` | `async fn` | sync | `awaitable<expected<ProcessResult>>` | ❌ |
| `initialize` | `async fn` | sync | `awaitable<void>` | ❌ |
| `queue_initial_processing_tasks` | `async fn` | sync | `awaitable<void>` | ❌ |
| `execute_processing_tasks` (body) | `async fn` | sync | `awaitable<void>` | ❌ |
| `handle_asset_source_event` | `async fn` | sync | `awaitable<void>` | ❌ |
| `wait_until_processed` | `async fn` | blocking | `awaitable<void>` | ❌ |
| `wait_until_initialized` | `async fn` | blocking | `awaitable<void>` | ❌ |
| `wait_until_finished` | `async fn` | blocking | `awaitable<void>` | ❌ |

---

## 6. Deducing This Migration

Methods where bevy takes `self` by value (consuming the object). C++ target uses deducing this.

| Entity | Bevy | C++ current | C++ target | Status |
|--------|------|-------------|------------|--------|
| `LoadedAsset<A>::take` | `fn take(self) → A` | `A take()` (moves out) | `A take(this Self self)` | ❌ |
| `ErasedLoadedAsset::downcast<A>` | `fn downcast(self) → Result<LoadedAsset<A>>` | `downcast()` | `expected<LoadedAsset<A>> downcast(this Self self)` | ❌ |
| `LoadContext::finish` | `fn finish(self) → ErasedLoadedAsset` | `finish()` | `ErasedLoadedAsset finish(this Self self)` | ❌ |
| `SavedAsset<A>::from_loaded` | `fn from_loaded(asset: LoadedAsset<A>) → Self` | factory takes ref | factory `from_loaded(this Self self, LoadedAsset<A>)` or take by value | ⚠️ |
| `TransformedAsset<A>::take` | `fn take(self) → A` | `A take()` | `A take(this Self self)` | ❌ |

---

## 7. Build / Dependency Changes Required

| Item | Current | Target | Status |
|------|---------|--------|--------|
| asio link | commented out in CMakeLists.txt | `target_link_libraries(epix_assets PUBLIC asio)` | ❌ |
| `istream&` → `Reader&` | throughout loader/saver/server | all async interfaces use `Reader&` / `Writer&` | ❌ |
| `IOTaskPool::detach_task(lambda)` | sync fire-and-forget | `IOTaskPool::spawn(awaitable) → Task<T>` | ❌ |
| `std::promise` blocking waits | `wait_for_asset_id` | `awaitable` with custom awaitable (waker-like) | ❌ |

---

## Migration Order (recommended)

1. **Enable asio** — uncomment `target_link_libraries(epix_assets PUBLIC asio)`, verify build
2. **Reader/Writer abstractions** — define abstract `Reader`/`Writer` with `awaitable` methods
3. **AssetReader/AssetWriter** — convert virtual methods to return `awaitable<...>`
4. **Concrete readers/writers** — FileAssetReader, MemoryAssetReader, etc.
5. **AssetLoader concept** — `load()` returns `awaitable`, takes `Reader&` instead of `istream&`
6. **ErasedAssetLoader** — virtual `load()` returns `awaitable`
7. **AssetSaver/Transformer** — same pattern
8. **LoadContext** — `load_direct*` methods become `awaitable`
9. **NestedLoader** — add Immediate mode with `awaitable` load
10. **AssetServer internals** — `load_internal`, `get_meta_loader_and_reader`, etc. → `awaitable`
11. **AssetServer spawn sites** — `spawn_load_task` etc. use `Task<void>` from IOTaskPool
12. **wait_for_asset** — replace `std::promise` with `awaitable` + custom awaitable
13. **Processor** — convert `Process::process` and processor internals
14. **Deducing this** — apply `(this Self self)` to consuming methods
15. **Tests** — update all tests for async interfaces