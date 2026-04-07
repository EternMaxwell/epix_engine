# Bevy Asset Module — C++ Match Status

Reference: `bevy_asset` 0.16 vs `epix_engine/assets` (C++23 MSVC modules)

All items target 1v1 parity. Non-implementable exceptions noted explicitly.

## Legend

| Symbol | Meaning                                         |
| ------ | ----------------------------------------------- |
| ✅      | Functionally equivalent                         |
| ⚠️      | Works but signature/impl diverges (note why)    |
| ❌      | Declaration differs significantly — must fix    |
| 🚫      | Missing entirely — must implement               |
| N/A    | Non-implementable (platform/library constraint) |

## Cross-Cutting Translation Rules

- **Bevy `async fn`** → C++ sync call dispatched via `utils::IOTaskPool::instance().detach_task(...)`
- **Bevy `Box<dyn Reader>` / `dyn Reader`** → C++ `std::istream&`
- **Rust enum (tagged union)** → C++ `std::variant<...>`
- **Bevy `TypeId`** → C++ `epix::meta::type_index`
- **Bevy `Arc<T>`** → C++ `std::shared_ptr<T>`
- **`Box<dyn Error>`** → C++ `std::exception_ptr`
- **Rust `CowArc<str>`** → C++ `std::string` (owned) — no borrow distinction needed

---

## 1. Core Traits & Concepts

| Entity                         | Bevy                                                                   | C++                                                                     | Status | Notes                                                      |
| ------------------------------ | ---------------------------------------------------------------------- | ----------------------------------------------------------------------- | ------ | ---------------------------------------------------------- |
| `Asset` trait                  | `trait Asset: VisitAssetDependencies + TypePath + Send + Sync`         | `concept Asset = VisitAssetDependencies<T>`                             | ✅      | TypePath/Send/Sync have no C++ equivalents                 |
| `VisitAssetDependencies` trait | `visit_dependencies(&self, &mut impl FnMut(UntypedAssetId))`           | C++ concept; `visit_dependencies(std::function<void(UntypedAssetId)>&)` | ✅      | Handle<T>/UntypedHandle/AssetContainer all implement it    |
| `AsAssetId` trait              | component holding `AssetId`                                            | 🚫                                                                       | 🚫      | ECS component integration                                  |
| `AssetLoader` concept          | `async fn load(reader, settings, ctx)`                                 | C++ concept; sync `fn load(istream&, settings, ctx)`                    | ⚠️      | Async→sync; `dyn Reader`→`istream&`                        |
| `AssetSaver` concept           | `type Asset, Settings, OutputLoader, Error`                            | C++ concept; `AssetType` instead of `Asset`                             | ❌      | Associated type name differs                               |
| `AssetTransformer` trait       | explicit Rust trait                                                    | `concept AssetTransformer` in loader.cppm                               | ✅      | Full concept with AssetInput, AssetOutput, Settings, Error |
| `Process` concept              | `type Settings, OutputLoader; async fn process(ctx, settings, writer)` | ✅ explicit concept (sync)                                               | ✅      |                                                            |
| `Settings` marker trait        | `trait Settings: Default + Serialize + Deserialize`                    | `struct Settings {}` base class                                         | ⚠️      | No serialize/deserialize yet                               |
| `DirectAssetAccessExt`         | `World` extension: `add_asset, load_asset, load_asset_with_settings`   | 🚫                                                                       | 🚫      |                                                            |
| `AssetApp` trait               | `init_asset, register_asset_loader, ...` methods on `App`              | Free functions `app_register_asset<T>`, etc.                            | ⚠️      | Free functions vs trait methods                            |

---

## 2. Index Types (`id.rs` → `index.cppm`)

| Entity                                                | Bevy                                     | C++                                                        | Status | Notes                          |
| ----------------------------------------------------- | ---------------------------------------- | ---------------------------------------------------------- | ------ | ------------------------------ |
| `AssetIndex` fields: `generation: u32, index: u32`    | pub fields                               | private with `.index()` / `.generation()` accessors        | ✅      |                                |
| `AssetIndexAllocator` fields: `next_index: AtomicU32` | ✅                                        | `m_next: atomic<u32>`                                      | ✅      |                                |
| `AssetIndexAllocator::reserve()`                      | `-> ErasedAssetIndex`                    | `-> AssetIndex`                                            | ✅      |                                |
| `AssetIndexAllocator::release(index)`                 | ✅                                        | ✅                                                          | ✅      |                                |
| `AssetIndexAllocator::reserved_receiver()`            | absent                                   | C++ extra; `-> Receiver<AssetIndex>` for storage expansion | ✅      |                                |
| `ErasedAssetIndex`                                    | `{ type_id: TypeId, index: AssetIndex }` | `InternalAssetId` + `UntypedAssetId` (index variant)       | ⚠️      | Combined into `UntypedAssetId` |

---

## 3. ID Types (`id.rs` → `asset_id.cppm`)

| Entity                                              | Bevy                                                        | C++                                                   | Status | Notes                               |
| --------------------------------------------------- | ----------------------------------------------------------- | ----------------------------------------------------- | ------ | ----------------------------------- |
| `AssetId<A>`                                        | `enum { Index { index, marker }, Uuid { uuid } }`           | `variant<AssetIndex, uuid>` wrapper                   | ✅      | Rust enum → C++ variant             |
| `AssetId::invalid()`                                | ✅                                                           | ✅                                                     | ✅      |                                     |
| `AssetId::is_uuid()`                                | ✅                                                           | ✅                                                     | ✅      |                                     |
| `AssetId::is_index()`                               | ✅                                                           | ✅                                                     | ✅      |                                     |
| `AssetId::untyped()`                                | `-> UntypedAssetId`                                         | via `UntypedAssetId(AssetId<T>)` ctor                 | ✅      |                                     |
| `AssetId::internal()`                               | `-> ErasedAssetIndex`                                       | `InternalAssetId` (private, non-pub)                  | ⚠️      | Not exposed in C++                  |
| `UntypedAssetId`                                    | `enum { Index { type_id, index }, Uuid { type_id, uuid } }` | `{ type: type_index, id: variant<AssetIndex, uuid> }` | ✅      | Flat struct semantically equivalent |
| `UntypedAssetId::type_id()`                         | `-> TypeId`                                                 | `.type` field                                         | ✅      |                                     |
| `UntypedAssetId::is_uuid / is_index / index / uuid` | ✅                                                           | ✅                                                     | ✅      |                                     |
| `UntypedAssetId::typed<A>`                          | ✅                                                           | ✅                                                     | ✅      |                                     |
| `UntypedAssetId::try_typed<A>`                      | ✅                                                           | ✅                                                     | ✅      |                                     |
| `UntypedAssetId::invalid()`                         | ✅                                                           | ✅                                                     | ✅      |                                     |
| `std::hash` specialization                          | ✅                                                           | ✅                                                     | ✅      |                                     |

---

## 4. Handle Types (`handle.rs` → `handle.cppm`)

| Entity                                                     | Bevy                                                     | C++                                                                                                                                  | Status | Notes                                                |
| ---------------------------------------------------------- | -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ | ------ | ---------------------------------------------------- |
| `StrongHandle::index`                                      | `AssetIndex` (pub)                                       | inside `id: UntypedAssetId`                                                                                                          | ⚠️      | layout differs                                       |
| `StrongHandle::type_id`                                    | `TypeId` (pub)                                           | inside `id: UntypedAssetId`                                                                                                          | ⚠️      |                                                      |
| `StrongHandle::asset_server_managed`                       | `bool`                                                   | `asset_server_managed: bool`                                                                                                         | ✅      | renamed from loader_managed                          |
| `StrongHandle::path`                                       | `Option<AssetPath<'static>>`                             | `optional<AssetPath>`                                                                                                                | ✅      |                                                      |
| `StrongHandle::meta_transform`                             | `Option<MetaTransform>`                                  | `optional<MetaTransform>`                                                                                                            | ✅      |                                                      |
| `StrongHandle::Drop` → `DropEvent`                         | sends `DropEvent { index, asset_server_managed }`        | sends `DestructionEvent { id, loader_managed }`                                                                                      | ✅      | name differs, semantics same                         |
| `AssetHandleProvider`                                      | `{ allocator, drop_sender, drop_receiver, type_id }`     | C++: `HandleProvider` with inline `AssetIndexAllocator`                                                                              | ⚠️      | name differs; combined                               |
| `HandleProvider::reserve()`                                | `-> UntypedHandle` (strong)                              | ✅                                                                                                                                    | ✅      |                                                      |
| `HandleProvider::get_handle(id, loader_managed, path, mt)` | ✅                                                        | ✅                                                                                                                                    | ✅      |                                                      |
| `HandleProvider::reserve(loader_managed, path, mt)`        | ✅                                                        | ✅                                                                                                                                    | ✅      |                                                      |
| `Handle<A>` variants                                       | `Strong(Arc<StrongHandle>)` \| `Uuid(Uuid, PhantomData)` | `variant<shared_ptr<StrongHandle>, AssetId<T>>`                                                                                      | ✅      |                                                      |
| `Handle::id()`                                             | `-> AssetId<A>`                                          | ✅                                                                                                                                    | ✅      |                                                      |
| `Handle::path()`                                           | `-> Option<&AssetPath>`                                  | `-> optional<AssetPath>`                                                                                                             | ✅      |                                                      |
| `Handle::is_strong / is_weak`                              | ✅                                                        | ✅                                                                                                                                    | ✅      |                                                      |
| `Handle::clone_weak()`                                     | absent in Bevy 0.16                                      | C++: `weak()`                                                                                                                        | ✅      | Bevy 0.16 has no clone_weak; weak() is equivalent    |
| `Handle::weak()`                                           | absent in Bevy 0.16                                      | `-> Handle<T>` weak copy                                                                                                             | ✅      | C++ extra; no Bevy equivalent                        |
| `Handle::untyped()`                                        | `-> UntypedHandle`                                       | ✅                                                                                                                                    | ✅      |                                                      |
| `Handle::make_strong(&Assets<A>)`                          | upgrades weak→strong via assets                          | ✅                                                                                                                                    | ✅      | declaration in handle.cppm, definition in store.cppm |
| `UntypedHandle` variants                                   | `Strong(Arc<StrongHandle>)` \| `Uuid { type_id, uuid }`  | `variant<shared_ptr<StrongHandle>, UntypedAssetId>`                                                                                  | ✅      |                                                      |
| `UntypedHandle::id()`                                      | ✅                                                        | ✅                                                                                                                                    | ✅      |                                                      |
| `UntypedHandle::path()`                                    | ✅                                                        | ✅                                                                                                                                    | ✅      |                                                      |
| `UntypedHandle::type_id()`                                 | `-> TypeId`                                              | `type_id() -> type_index` (+ `type()` alias)                                                                                         | ✅      | renamed; type() kept as alias                        |
| `UntypedHandle::typed<A>`                                  | ✅                                                        | ✅                                                                                                                                    | ✅      |                                                      |
| `UntypedHandle::try_typed<A>`                              | ✅                                                        | ✅                                                                                                                                    | ✅      |                                                      |
| `UntypedHandle::typed_unchecked<A>`                        | unchecked cast                                           | ✅                                                                                                                                    | ✅      |                                                      |
| `UntypedHandle::typed_debug_checked<A>`                    | debug-assert cast                                        | ✅                                                                                                                                    | ✅      |                                                      |
| `UntypedHandle::meta_transform()`                          | `-> Option<&MetaTransform>`                              | ✅                                                                                                                                    | ✅      |                                                      |
| `UntypedHandle::weak()`                                    | id-only copy                                             | ✅                                                                                                                                    | ✅      |                                                      |
| `UntypedAssetConversionError`                              | typed error for type mismatch                            | C++ struct `{ expected, found }` thrown by `typed<T>()`; `try_typed<T>()` returns `expected<Handle<T>, UntypedAssetConversionError>` | ✅      | C++ uses exceptions                                  |
| `uuid_handle!` macro                                       | `uuid_handle!("…") -> Handle<A>` const                   | `uuid_handle<A>(string_view)` function (throws on invalid UUID)                                                                      | ⚠️      | Function not macro; runtime parse vs compile-time    |

---

## 5. Path & Source IDs (`path.cppm`)

| Entity                                                     | Bevy                                           | C++                                         | Status | Notes                            |
| ---------------------------------------------------------- | ---------------------------------------------- | ------------------------------------------- | ------ | -------------------------------- |
| `AssetSourceId`                                            | `enum { Default, Name(CowArc<str>) }`          | `optional<string>` wrapper struct           | ✅      | Rust enum → C++ optional         |
| `AssetSourceId::is_default()`                              | default variant check                          | `.is_default()`                             | ✅      |                                  |
| `AssetSourceId::as_str()`                                  | `-> Option<&str>`                              | `-> optional<string_view>`                  | ✅      |                                  |
| `AssetSourceId` hash + ord                                 | ✅                                              | ✅                                           | ✅      |                                  |
| `AssetPath` fields: `source, path, label`                  | `AssetSourceId, PathBuf, Option<CowArc<str>>`  | `AssetSourceId, fs::path, optional<string>` | ✅      |                                  |
| `AssetPath` parse from `&str`                              | ✅                                              | ctor from `string_view`                     | ✅      |                                  |
| `AssetPath::with_label`                                    | ✅                                              | ✅                                           | ✅      |                                  |
| `AssetPath::with_source`                                   | ✅                                              | ✅                                           | ✅      |                                  |
| `AssetPath::without_label` / `remove_label` / `take_label` | ✅                                              | ✅                                           | ✅      |                                  |
| `AssetPath::parent()`                                      | ✅                                              | ✅                                           | ✅      |                                  |
| `AssetPath::resolve(relative)`                             | ✅                                              | ✅                                           | ✅      |                                  |
| `AssetPath::resolve_embed(embedded_path)`                  | resolves path relative to embedded source root | ✅                                           | ✅      | RFC 1808 semantics               |
| `AssetPath::get_full_extension()`                          | ✅                                              | ✅                                           | ✅      |                                  |
| `AssetPath::get_extension()`                               | ✅                                              | ✅                                           | ✅      |                                  |
| `AssetPath::iter_secondary_extensions()`                   | ✅                                              | ✅                                           | ✅      |                                  |
| `AssetPath::is_unapproved()`                               | checks path escapes asset dir                  | ✅                                           | ✅      | Checks prefix/root/parent escape |
| `AssetPath::try_parse(str)`                                | `-> Option<AssetPath>`                         | ✅                                           | ✅      |                                  |
| `std::hash<AssetPath>`                                     | ✅                                              | ✅                                           | ✅      |                                  |

---

## 6. AssetServerData + AssetServer (`server/mod.cppm` + `src/server.cpp`)

### `AssetServerData` fields

| Field                  | Bevy                           | C++                                       | Status | Notes |
| ---------------------- | ------------------------------ | ----------------------------------------- | ------ | ----- |
| `infos`                | `Arc<RwLock<AssetInfos>>`      | `utils::RwLock<AssetInfos>`               | ✅      |       |
| `loaders`              | `Arc<RwLock<AssetLoaders>>`    | `shared_ptr<utils::RwLock<AssetLoaders>>` | ✅      |       |
| `asset_event_sender`   | `Sender<InternalAssetEvent>`   | `utils::Sender<InternalAssetEvent>`       | ✅      |       |
| `asset_event_receiver` | `Receiver<InternalAssetEvent>` | `utils::Receiver<InternalAssetEvent>`     | ✅      |       |
| `sources`              | `Arc<AssetSources>`            | `shared_ptr<AssetSources>`                | ✅      |       |
| `mode`                 | `AssetServerMode`              | `AssetServerMode`                         | ✅      |       |
| `watching_for_changes` | `bool`                         | `bool`                                    | ✅      |       |
| `meta_check`           | `AssetMetaCheck`               | `AssetMetaCheck`                          | ✅      |       |
| `unapproved_path_mode` | `UnapprovedPathMode`           | `UnapprovedPathMode`                      | ✅      |       |

### `AssetServer` constructors

| Method                                                                       | Bevy | C++ | Status | Notes |
| ---------------------------------------------------------------------------- | ---- | --- | ------ | ----- |
| `new(sources, mode, watching)`                                               | ✅    | ✅   | ✅      |       |
| `new_with_meta_check(sources, mode, meta_check, watching, unapproved)`       | ✅    | ✅   | ✅      |       |
| `new_with_loaders(sources, loaders, mode, meta_check, watching, unapproved)` | ✅    | ✅   | ✅      |       |

### `AssetServer` public methods — loading

| Method                                                                          | Bevy                                     | C++ | Status                                                                                             | Notes                          |
| ------------------------------------------------------------------------------- | ---------------------------------------- | --- | -------------------------------------------------------------------------------------------------- | ------------------------------ |
| `load<A>(path)`                                                                 | `-> Handle<A>`                           | ✅   | ✅                                                                                                  |                                |
| `load_with_meta_transform<A>(path, meta_transform, force, override_unapproved)` | `-> Handle<A>`                           | ✅   | ✅                                                                                                  |
| `load_override<A>(path)`                                                        | `-> Handle<A>`                           | ✅   | ✅                                                                                                  | bypasses unapproved-path check |
| `load_with_settings<A,S>(path, settings_fn)`                                    | `-> Handle<A>`                           | ✅   | ✅                                                                                                  |                                |
| `load_with_settings_override<A,S>(path, settings_fn)`                           | `-> Handle<A>`                           | ✅   | ✅                                                                                                  |                                |
| `load_untyped(path)`                                                            | `-> Handle<LoadedUntypedAsset>`          | ❌   | C++ returns `UntypedHandle` directly; Bevy two-step via `LoadedUntypedAsset`                       |
| `load_unknown_type_with_meta_transform(path, mt)`                               | `-> Handle<LoadedUntypedAsset>`          | 🚫   | used internally by `load_untyped`                                                                  |
| `load_untyped_async(path)`                                                      | `async -> UntypedHandle`                 | 🚫   | used by `load_folder_internal`                                                                     |
| `load_erased_with_meta_transform(path, type_id, mt, ())`                        | `-> UntypedHandle`                       | 🚫   | DynamicTyped deferred load                                                                         |
| `load_folder(path)`                                                             | `-> Handle<LoadedFolder>`                | ✅   | ✅                                                                                                  |                                |
| `load_acquire<A,G>(path, guard)`                                                | `-> Handle<A>`                           | ⚠️   | C++: uses `wait_for_asset` (promise-based blocking); guard param omitted (no Assets RwLock in C++) |
| `load_acquire_with_settings<A,S,G>(path, guard, settings_fn)`                   | `-> Handle<A>`                           | ⚠️   | same caveat                                                                                        |
| `add<A>(asset)`                                                                 | `-> Handle<A>`                           | ⚠️   | implemented via `load_asset_untyped`; Bevy calls `Assets<A>::add` directly                         |
| `add_async<A,E>(future)`                                                        | `async -> Handle<A>`                     | ⚠️   | dispatches via IOTaskPool; not true async future                                                   |
| `reload(path)`                                                                  | `-> Result<(), MissingAssetSourceError>` | ✅   | ✅                                                                                                  |

### `AssetServer` public methods — state query

| Method                                    | Bevy                                                                        | C++ | Status | Notes |
| ----------------------------------------- | --------------------------------------------------------------------------- | --- | ------ | ----- |
| `get_load_states(id)`                     | `-> Option<(LoadState, DependencyLoadState, RecursiveDependencyLoadState)>` | ✅   | ✅      |       |
| `get_load_state(id)`                      | `-> Option<LoadState>`                                                      | ✅   | ✅      |       |
| `get_dependency_load_state(id)`           | `-> Option<DependencyLoadState>`                                            | ✅   | ✅      |       |
| `get_recursive_dependency_load_state(id)` | `-> Option<RecursiveDependencyLoadState>`                                   | ✅   | ✅      |       |
| `load_state(id)`                          | `-> LoadState`                                                              | ✅   | ✅      |       |
| `dependency_load_state(id)`               | `-> DependencyLoadState`                                                    | ✅   | ✅      |       |
| `recursive_dependency_load_state(id)`     | `-> RecursiveDependencyLoadState`                                           | ✅   | ✅      |       |
| `is_loaded(id)`                           | ✅                                                                           | ✅   | ✅      |       |
| `is_loaded_with_direct_dependencies(id)`  | ✅                                                                           | ✅   | ✅      |       |
| `is_loaded_with_dependencies(id)`         | ✅                                                                           | ✅   | ✅      |       |

### `AssetServer` public methods — handle/path lookup

| Method                                       | Bevy                        | C++ | Status | Notes |
| -------------------------------------------- | --------------------------- | --- | ------ | ----- |
| `get_handle<A>(path)`                        | `-> Option<Handle<A>>`      | ✅   | ✅      |       |
| `get_id_handle<A>(id)`                       | `-> Option<Handle<A>>`      | ✅   | ✅      |       |
| `get_id_handle_untyped(id)`                  | `-> Option<UntypedHandle>`  | ✅   | ✅      |       |
| `is_managed(id)`                             | ✅                           | ✅   | ✅      |       |
| `get_path_id(path)`                          | `-> Option<UntypedAssetId>` | ✅   | ✅      |       |
| `get_path_ids(path)`                         | `-> Vec<UntypedAssetId>`    | ✅   | ✅      |       |
| `get_handle_untyped(path)`                   | `-> Option<UntypedHandle>`  | ✅   | ✅      |       |
| `get_handles_untyped(path)`                  | `-> Vec<UntypedHandle>`     | ✅   | ✅      |       |
| `get_path_and_type_id_handle(path, type_id)` | `-> Option<UntypedHandle>`  | ✅   | ✅      |       |
| `get_path(id)`                               | `-> Option<AssetPath>`      | ✅   | ✅      |       |
| `mode()`                                     | `-> AssetServerMode`        | ✅   | ✅      |       |

### `AssetServer` public methods — loader lookup

| Method                                         | Bevy                                  | C++ | Status                         | Notes |
| ---------------------------------------------- | ------------------------------------- | --- | ------------------------------ | ----- |
| `get_asset_loader_with_extension(ext)`         | ✅                                     | ✅   | ✅                              |       |
| `get_asset_loader_with_type_name(name)`        | ✅                                     | ✅   | ✅                              |       |
| `get_path_asset_loader(path)`                  | `async -> Arc<dyn ErasedAssetLoader>` | ⚠️   | C++ synchronous                |
| `get_asset_loader_with_asset_type_id(type_id)` | `async -> Arc<dyn ErasedAssetLoader>` | ⚠️   | C++ synchronous                |
| `get_asset_loader_with_asset_type<A>()`        | ✅                                     | ✅   | ✅                              |       |
| `register_asset<A>(assets)`                    | ✅                                     | ✅   | renamed from `register_assets` |
| `register_loader<L>(loader)`                   | ✅                                     | ✅   | ✅                              |       |
| `preregister_loader<L>(extensions)`            | ✅                                     | ✅   | ✅                              |       |

### `AssetServer` public methods — async waiting

| Method                                          | Bevy                                         | C++ | Status                                                 | Notes |
| ----------------------------------------------- | -------------------------------------------- | --- | ------------------------------------------------------ | ----- |
| `wait_for_asset<A>(handle)`                     | `async -> Result<(), WaitForAssetError>`     | ⚠️   | blocking via `std::promise` (not async); no sleep loop |
| `wait_for_asset_untyped(handle)`                | `async -> Result<(), WaitForAssetError>`     | ⚠️   | same caveat                                            |
| `wait_for_asset_id(id)`                         | `async -> Result<(), WaitForAssetError>`     | ⚠️   | same caveat                                            |
| `write_default_loader_meta_file_for_path(path)` | `async -> Result<(), WriteDefaultMetaError>` | 🚫   |                                                        |

### `AssetServer` internal methods (pub-crate / private in Bevy)

| Method                                                                      | Bevy                                         | C++ | Status                                                                   | Notes |
| --------------------------------------------------------------------------- | -------------------------------------------- | --- | ------------------------------------------------------------------------ | ----- |
| `get_or_create_path_handle<A>(path, mt)`                                    | `-> Handle<A>`                               | ⚠️   | only on `AssetInfos` in C++; newly added to `AssetServer`                |
| `get_or_create_path_handle_erased(path, type_id, mt)`                       | `-> UntypedHandle`                           | ⚠️   | newly added                                                              |
| `load_erased_with_meta_transform(path, type_id, mt, ())`                    | `-> UntypedHandle`                           | 🚫   | DynamicTyped deferred; different from `get_or_create_path_handle_erased` |
| `get_meta_loader_and_reader(path, asset_type_id)`                           | `async -> (Meta, Loader, Reader)`            | ⚠️   | sync impl; meta deserialization incomplete                               |
| `load_with_settings_loader_and_reader(path, settings, loader, reader, ...)` | `async -> ErasedLoadedAsset`                 | ⚠️   | sync impl done                                                           |
| `load_asset_untyped(path, asset)`                                           | `(path, ErasedLoadedAsset) -> UntypedHandle` | ⚠️   | implemented; Bevy version slightly different signature                   |
| `load_folder_internal(index, path)`                                         | `async`                                      | ⚠️   | uses `load_untyped` not `load_untyped_async`                             |
| `spawn_load_task<G>(handle, path, guard)`                                   | `async`                                      | ⚠️   | no guard param; calls `load_internal`                                    |
| `load_internal(handle, path, force, mt)`                                    | `async -> Result<Option<Handle>>`            | ⚠️   | sync; dispatched via IO task pool                                        |
| `reload_internal(path)`                                                     | `async`                                      | ⚠️   | sync; spawns task calling `load_internal` per handle                     |
| `send_asset_event(event)`                                                   | ✅                                            | ✅   | ✅                                                                        |       |
| `read_infos / write_infos`                                                  | `-> RwLockReadGuard / WriteGuard`            | ✅   | ✅                                                                        |       |
| `read_loaders / write_loaders`                                              | `-> RwLockReadGuard / WriteGuard`            | ✅   | ✅                                                                        |       |
| `get_source(source_id)`                                                     | `-> Option<&AssetSource>`                    | ✅   | ✅                                                                        |       |

### `AssetServer` — free functions / systems

| Symbol                                            | Bevy               | C++ | Status                                       | Notes |
| ------------------------------------------------- | ------------------ | --- | -------------------------------------------- | ----- |
| `handle_internal_asset_events(world)`             | ECS system         | ⚠️   | C++: `handle_internal_events`; wakers differ |
| `publish_asset_server_diagnostics(server, store)` | diagnostics system | 🚫   | 🚫                                            |       |

### `AssetServer` — associated types / enums

| Type                      | Bevy variants                                       | C++ | Status | Notes |
| ------------------------- | --------------------------------------------------- | --- | ------ | ----- |
| `AssetServerMode`         | `Unprocessed, Processed`                            | ✅   | ✅      |       |
| `WaitForAssetError`       | `NotLoaded, Failed(error), DependencyFailed(error)` | ✅   | ✅      |       |
| `WriteDefaultMetaError`   | error type                                          | 🚫   | 🚫      |       |
| `MissingAssetSourceError` | error for reload                                    | ✅   | ✅      |       |

---

## 7. AssetInfos + AssetInfo (`server/info.cppm`)

### Load-state types

| Type                             | Bevy                                                                                    | C++                                                                                   | Status | Notes |
| -------------------------------- | --------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ------ | ----- |
| `LoadState`                      | `NotLoaded \| Loading \| Loaded \| Failed(Arc<AssetLoadError>)`                         | `variant<LoadStateOK, AssetLoadError>` + enum-wrapper                                 | ✅      |       |
| `DependencyLoadState`            | same 4 variants (separate type)                                                         | distinct `struct DependencyLoadState : variant<LoadStateOK, AssetLoadError>`          | ✅      |       |
| `RecursiveDependencyLoadState`   | same 4 variants (separate type)                                                         | distinct `struct RecursiveDependencyLoadState : variant<LoadStateOK, AssetLoadError>` | ✅      |       |
| `InternalAssetEvent`             | `Loaded{id, loaded, handle}, LoadedWithDependencies{id}, Failed{id, path, error}`       | ✅                                                                                     | ✅      |       |
| `HandleLoadingMode`              | `NotLoading, Request, Force`                                                            | ✅                                                                                     | ✅      |       |
| `AssetServerStats`               | `{ started_load_tasks: u64, finished_load_tasks: u64, ... }`                            | `AssetServerStats` struct                                                             | ✅      |       |
| `WaitForAssetError`              | `NotLoaded, Failed(AssetLoadError), DependencyFailed(AssetLoadError)`                   | ✅ `variant<NotLoaded, Failed, DependencyFailed>` in `wait_for_asset_error` namespace  | ✅      |       |
| `GetOrCreateHandleInternalError` | variant: `MissingHandleProviderError{type_id}` / `HandleMissingButTypeIdNotSpecified{}` | variant with matching variant names and TypeId fields                                 | ✅      |       |

### `AssetInfo` fields

| Field                  | Bevy                            | C++                                                             | Status | Notes                                               |
| ---------------------- | ------------------------------- | --------------------------------------------------------------- | ------ | --------------------------------------------------- |
| `weak_handle`          | `Option<WeakHandle>`            | `weak_ptr<StrongHandle>`                                        | ✅      |                                                     |
| `path`                 | `Option<AssetPath<'static>>`    | `optional<AssetPath>`                                           | ✅      |                                                     |
| `load_state`           | `LoadState`                     | `LoadState`                                                     | ✅      |                                                     |
| `dep_load_state`       | `DependencyLoadState`           | `DependencyLoadState`                                           | ✅      | field named `dep_state`                             |
| `rec_dep_load_state`   | `RecursiveDependencyLoadState`  | `RecursiveDependencyLoadState`                                  | ✅      | field named `rec_dep_state`                         |
| `loading_deps`         | `HashSet<UntypedAssetId>`       | `unordered_set<UntypedAssetId>`                                 | ✅      |                                                     |
| `failed_deps`          | `HashSet<UntypedAssetId>`       | `unordered_set<UntypedAssetId>`                                 | ✅      |                                                     |
| `loading_rec_deps`     | `HashSet<UntypedAssetId>`       | `unordered_set<UntypedAssetId>`                                 | ✅      |                                                     |
| `failed_rec_deps`      | `HashSet<UntypedAssetId>`       | `unordered_set<UntypedAssetId>`                                 | ✅      |                                                     |
| `dependants`           | `HashSet<ErasedAssetIndex>`     | `unordered_set<UntypedAssetId> dependants`                      | ✅      | upstream handle tracking                            |
| `loader_dependencies`  | `HashMap<AssetPath, AssetHash>` | `unordered_map<AssetPath, size_t>`                              | ✅      |                                                     |
| `waiting_tasks`        | `Vec<Waker>` (async wakers)     | `vector<shared_ptr<promise<expected<void,WaitForAssetError>>>>` | ✅      | C++ blocking equivalent; resolved by event handlers |
| `handle_destruct_skip` | `u32`                           | `size_t`                                                        | ✅      |                                                     |

### `AssetInfos` fields

| Field                            | Bevy                                        | C++                                                                    | Status | Notes                                                  |
| -------------------------------- | ------------------------------------------- | ---------------------------------------------------------------------- | ------ | ------------------------------------------------------ |
| `path_to_index`                  | `HashMap<AssetPath, TypeIdMap<AssetIndex>>` | `path_to_ids: HashMap<AssetPath, HashMap<type_index, UntypedAssetId>>` | ⚠️      | different key type — stores `AssetId` not `AssetIndex` |
| `infos`                          | `SlotMap<AssetIndex, AssetInfo>`            | `unordered_map<UntypedAssetId, AssetInfo>`                             | ❌      | SlotMap vs hash map; different lookup by index         |
| `handle_providers`               | `TypeIdMap<AssetHandleProvider>`            | `unordered_map<type_index, shared_ptr<HandleProvider>>`                | ✅      |                                                        |
| `waker_for_next_key`             | `Option<Waker>`                             | absent                                                                 | 🚫      | not needed without async runtime                       |
| `dependency_loaded_event_sender` | `TypeIdMap<fn(World, AssetIndex)>`          | `unordered_map<type_index, fn*>`                                       | ✅      |                                                        |
| `dependency_failed_event_sender` | `TypeIdMap<fn(World, AssetIndex, error)>`   | `unordered_map<type_index, fn*>`                                       | ✅      |                                                        |
| `pending_tasks`                  | `HashMap<ErasedAssetIndex, Task<()>>`       | `HashMap<UntypedAssetId, variant<packaged_task, shared_future>>`       | ❌      | task type differs                                      |
| `infos_generation`               | `u64`                                       | `uint64_t infos_generation = 0` (incremented on handle create/destroy) | ✅      |                                                        |
| `stats`                          | `AssetServerStats`                          | `stats: AssetServerStats`                                              | ✅      |                                                        |
| `watching_for_changes`           | `bool`                                      | `bool`                                                                 | ✅      |                                                        |
| `loader_dependents`              | `HashMap<AssetPath, HashSet<AssetPath>>`    | same                                                                   | ✅      |                                                        |
| `living_labeled_assets`          | `HashMap<AssetPath, HashSet<SmolStr>>`      | `HashMap<AssetPath, HashSet<string>>`                                  | ✅      |                                                        |

### `AssetInfos` methods

| Method                                                            | Bevy                                 | C++ | Status                                                           | Notes |
| ----------------------------------------------------------------- | ------------------------------------ | --- | ---------------------------------------------------------------- | ----- |
| `create_loading_handle_untyped(type_id, type_name)`               | `-> UntypedHandle`                   | ⚠️   | C++: `create_loading_handle_untyped(type_id)` — no type_name arg |
| `get_or_create_path_handle<A>(path, mode, mt)`                    | `-> (Handle<A>, bool)`               | ❌   | C++: `get_or_create_handle<T>` — different args                  |
| `get_or_create_path_handle_erased(path, type_id, name, mode, mt)` | `-> (UntypedHandle, bool)`           | ❌   | C++: `get_or_create_handle_untyped`                              |
| `get_or_create_path_handle_internal(path, type_id, mode, mt)`     | `-> Result<(UntypedHandle, bool)>`   | ✅   | C++: `get_or_create_handle_internal`                             |
| `get_path_handles(path)`                                          | `-> impl Iterator<UntypedHandle>`    | ✅   | C++: `get_handles_by_path`                                       |
| `get_path_indices(path)`                                          | `-> impl Iterator<ErasedAssetIndex>` | ✅   | C++: `get_path_ids`                                              |
| `get_index_handle(index)`                                         | `-> Option<UntypedHandle>`           | ✅   | C++: `get_handle_by_id`                                          |
| `get(index)`                                                      | `-> Option<&AssetInfo>`              | ✅   | C++: `get_info`                                                  |
| `get_mut(index)`                                                  | `-> Option<&mut AssetInfo>`          | ✅   | C++: `get_info_mut`                                              |
| `contains_key(index)`                                             | `-> bool`                            | ✅   | ✅                                                                |       |
| `process_handle_destruction(id)`                                  | `-> bool`                            | ✅   | ✅                                                                |       |
| `process_asset_load(id, loaded, world, sender)`                   | ✅                                    | ✅   | ✅                                                                |       |
| `process_asset_fail(id, error)`                                   | ✅                                    | ✅   | ✅                                                                |       |
| `should_reload(path)`                                             | `-> bool`                            | ✅   | ✅                                                                |       |

---

## 8. AssetLoaders (`server/loaders.cppm`)

| Entity                                                                                       | Bevy                                              | C++                                      | Status                                                             | Notes |
| -------------------------------------------------------------------------------------------- | ------------------------------------------------- | ---------------------------------------- | ------------------------------------------------------------------ | ----- |
| `AssetLoaders` fields: `loaders, type_to_loaders, extension_to_loaders, type_name_to_loader` | ✅                                                 | ✅                                        | ✅                                                                  |       |
| `push<L>(loader)`                                                                            | ✅                                                 | ✅                                        | ✅                                                                  |       |
| `reserve<L>(extensions)`                                                                     | ✅                                                 | ✅                                        | ✅                                                                  |       |
| `find(loader_name, asset_type_id, extension, path)`                                          | unified `-> Option<MaybeAssetLoader>`             | ❌                                        | C++: separate `get_by_name/type/extension/path`; no unified `find` |
| `MaybeAssetLoader`                                                                           | `enum { Ready(Arc), Pending(BroadcastReceiver) }` | `variant<shared_ptr, BroadcastReceiver>` | ✅                                                                  |       |
| `MaybeAssetLoader::get()`                                                                    | `async -> Arc<dyn ErasedAssetLoader>`             | ⚠️                                        | C++ synchronous (`BroadcastReceiver::receive()`)                   |
| `PendingAssetLoader`                                                                         | `{ sender, loader }`                              | absent (inlined)                         | ⚠️                                                                  |       |

---

## 9. Loader / LoadContext / NestedLoader (`server/loader.cppm`)

### `AssetLoader` concept / trait

| Entity                                     | Bevy                                                                      | C++                                       | Status | Notes                          |
| ------------------------------------------ | ------------------------------------------------------------------------- | ----------------------------------------- | ------ | ------------------------------ |
| `AssetLoader::Asset` associated type       | ✅                                                                         | ✅                                         | ✅      |                                |
| `AssetLoader::Settings` associated type    | ✅                                                                         | ✅                                         | ✅      |                                |
| `AssetLoader::Error` associated type       | ✅                                                                         | ✅                                         | ✅      |                                |
| `AssetLoader::load(reader, settings, ctx)` | `async (Box<dyn Reader>, &Settings, LoadContext) -> Result<Asset, Error>` | sync `(istream&, Settings, LoadContext&)` | ⚠️      | async→sync; dyn Reader→istream |
| `AssetLoader::extensions()`                | `-> &[&str]`                                                              | ✅                                         | ✅      |                                |

### `ErasedAssetLoader` abstract class / trait object

| Method                    | Bevy                                                                                       | C++                                            | Status                                                                   | Notes                         |
| ------------------------- | ------------------------------------------------------------------------------------------ | ---------------------------------------------- | ------------------------------------------------------------------------ | ----------------------------- |
| `load(reader, meta, ctx)` | `async (Box<dyn Reader>, Box<dyn AssetMetaDyn>, LoadContext) -> Result<ErasedLoadedAsset>` | sync `(istream&, AssetMetaDyn&, LoadContext&)` | ⚠️                                                                        | async→sync                    |
| `extensions()`            | `-> &[&str]`                                                                               | ✅                                              | ✅                                                                        |                               |
| `asset_type_name()`       | `-> &'static str`                                                                          | ✅                                              | `asset_type().short_name()`                                              |
| `asset_type_id()`         | `-> TypeId`                                                                                | ✅                                              | `asset_type() -> type_index`; `type_index` is C++ equivalent of `TypeId` |
| `default_meta()`          | `-> Box<dyn AssetMetaDyn>`                                                                 | ✅                                              | ✅                                                                        | recently added                |
| `deserialize_meta(bytes)` | `-> Result<Box<dyn AssetMetaDyn>>`                                                         | 🚫                                              | 🚫                                                                        | needs meta file format parser |
| `type_path()` (full name) | `-> &'static str`                                                                          | ✅                                              | `loader_type().name()`                                                   |

### `LoadContext` struct

| Field/Method                               | Bevy                                 | C++                                                                                                           | Status | Notes                                                              |
| ------------------------------------------ | ------------------------------------ | ------------------------------------------------------------------------------------------------------------- | ------ | ------------------------------------------------------------------ |
| `asset_path`                               | `AssetPath<'static>`                 | `AssetPath`                                                                                                   | ✅      |                                                                    |
| `asset_server`                             | `&AssetServer`                       | `const AssetServer&`                                                                                          | ✅      |                                                                    |
| `populate_hashes`                          | `bool`                               | absent                                                                                                        | 🚫      |                                                                    |
| `asset_bytes_for_dependencies`             | `HashMap<AssetPath, Vec<u8>>`        | absent                                                                                                        | 🚫      |                                                                    |
| `labeled_assets`                           | `HashMap<CowArc<str>, LabeledAsset>` | `unordered_map<string, LabeledAsset>`                                                                         | ✅      |                                                                    |
| `dependencies`                             | `HashSet<UntypedAssetId>`            | `unordered_set<UntypedAssetId>`                                                                               | ✅      |                                                                    |
| `loader_dependencies`                      | `HashMap<AssetPath, AssetHash>`      | `unordered_map<AssetPath, size_t>`                                                                            | ✅      |                                                                    |
| `should_load_dependencies`                 | `bool`                               | `bool m_should_load_dependencies = true` with `should_load_dependencies()` / `set_should_load_dependencies()` | ✅      | controls whether `NestedLoader` actually loads or just gets handle |
| `finish(asset)`                            | `-> LoadedAsset<A>` (consuming)      | `finish<A>(value) -> LoadedAsset<A>`                                                                          | ✅      |                                                                    |
| `begin_labeled_asset()`                    | `-> LoadContext` (nested)            | ✅                                                                                                             | ✅      |                                                                    |
| `finish_labeled_asset(label, context)`     | stores labeled sub-asset             | ✅                                                                                                             | ✅      |                                                                    |
| `add_labeled_asset<A>(label, asset)`       | `-> Handle<A>`                       | ✅                                                                                                             | ✅      |                                                                    |
| `get_label_handle<A>(label)`               | `-> Handle<A>`                       | ✅                                                                                                             | ✅      |                                                                    |
| `has_labeled_asset(label)`                 | `-> bool`                            | ✅                                                                                                             | ✅      |                                                                    |
| `set_default_asset<A>(asset)`              | ✅                                    | ✅                                                                                                             | ✅      |                                                                    |
| `load<A>(path)`                            | `-> Handle<A>` (via `NestedLoader`)  | `load<A>(path)`                                                                                               | ✅      | simplified API                                                     |
| `load_direct<A>(path)`                     | `async -> Result<LoadedAsset<A>>`    | `load_direct<A>(path)` sync                                                                                   | ⚠️      |                                                                    |
| `load_direct_with_reader<A>(path, reader)` | `async -> Result<LoadedAsset<A>>`    | `load_direct_with_reader<A>(path, reader)` sync                                                               | ⚠️      | Sync; reader caller-provided                                       |

### `LabeledAsset` struct

| Field    | Bevy                | C++                 | Status | Notes |
| -------- | ------------------- | ------------------- | ------ | ----- |
| `asset`  | `ErasedLoadedAsset` | `ErasedLoadedAsset` | ✅      |       |
| `handle` | `UntypedHandle`     | `UntypedHandle`     | ✅      |       |

### `ErasedLoadedAsset` struct

| Field/Method                     | Bevy                               | C++                                                            | Status | Notes                               |
| -------------------------------- | ---------------------------------- | -------------------------------------------------------------- | ------ | ----------------------------------- |
| `value: Box<dyn AssetContainer>` | type-erased asset value            | `shared_ptr<void>` with type info                              | ⚠️      | C++ stores via void ptr             |
| `dependencies`                   | `HashSet<UntypedAssetId>`          | `unordered_set<UntypedAssetId>`                                | ✅      |                                     |
| `loader_dependencies`            | `HashMap<AssetPath, AssetHash>`    | same                                                           | ✅      |                                     |
| `labeled_assets`                 | `HashMap<...>`                     | `unordered_map<string, LabeledAsset>`                          | ✅      |                                     |
| `downcast<A>()`                  | `-> Option<LoadedAsset<A>>`        | ✅                                                              | ✅      |                                     |
| `get<A>()`                       | `-> Option<&A>`                    | ✅                                                              | ✅      |                                     |
| `asset_type_id()`                | `-> TypeId`                        | via `type_id` field                                            | ✅      |                                     |
| `AssetContainer` trait           | `downcast_ref, type_id, type_path` | `export struct AssetContainer` virtual base (type(), downcast) | ⚠️      | C++ virtual class; type_path absent |

### `NestedLoader` builder

| Entity                                     | Bevy                                                                                          | C++                                                 | Status | Notes                          |
| ------------------------------------------ | --------------------------------------------------------------------------------------------- | --------------------------------------------------- | ------ | ------------------------------ |
| `NestedLoader<T, M>`                       | generic over typing (`StaticTyped/DynamicTyped/UnknownTyped`) and mode (`Deferred/Immediate`) | simple struct with `load<A>()`                      | ❌      | C++ lacks full builder pattern |
| `NestedLoader::with_settings<S>(fn)`       | sets meta transform                                                                           | ✅                                                   | ✅      |                                |
| `NestedLoader::with_static_type()`         | builder state transition                                                                      | absent                                              | 🚫      |                                |
| `NestedLoader::with_dynamic_type(type_id)` | builder state transition                                                                      | absent                                              | 🚫      |                                |
| `NestedLoader::with_unknown_type()`        | builder state transition                                                                      | absent                                              | 🚫      |                                |
| `NestedLoader::deferred()`                 | returns handle, load happens later                                                            | default behavior in C++                             | ✅      |                                |
| `NestedLoader::immediate()`                | loads right now, returns asset value                                                          | absent                                              | 🚫      |                                |
| `NestedLoader::with_reader(reader)`        | provides reader for immediate load                                                            | absent                                              | 🚫      |                                |
| `StaticTyped::load<A>(path)`               | `-> Handle<A>`                                                                                | C++: `load<A>(path) -> Handle<A>`                   | ✅      |                                |
| `DynamicTyped::load(path)`                 | `-> UntypedHandle`                                                                            | `NestedLoader::load_untyped(path) -> UntypedHandle` | ✅      | Method on NestedLoader         |
| `UnknownTyped::load(path)`                 | `-> Handle<LoadedUntypedAsset>`                                                               | absent                                              | 🚫      |                                |
| Immediate `StaticTyped::load<A>(path)`     | `async -> Result<LoadedAsset<A>>`                                                             | absent                                              | 🚫      |                                |

---

## 10. Meta Types (`meta.cppm`)

| Entity                                                                           | Bevy                                                         | C++                                                           | Status | Notes                           |
| -------------------------------------------------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------- | ------ | ------------------------------- |
| `AssetMetaCheck`                                                                 | `Always \| Paths(HashSet<AssetPath>) \| Never`               | `variant<Always,Never,Paths{set<AssetPath>}>`                 | ✅      | Full variant with Paths set     |
| `UnapprovedPathMode`                                                             | `Allow \| Deny \| Forbid`                                    | ✅                                                             | ✅      |                                 |
| `AssetActionType`                                                                | `Load \| Process`                                            | ✅                                                             | ✅      |                                 |
| `AssetMeta<LoaderSettings, ProcessorSettings>`                                   | generic struct with `processed_info, loader, processor, ...` | ✅                                                             | ✅      |                                 |
| `AssetMetaDyn` trait methods: `loader_settings, processed_info, set_loader, ...` | ✅ abstract                                                   | ✅                                                             | ✅      |                                 |
| `MetaTransform`                                                                  | `fn(&mut dyn AssetMetaDyn)` type alias                       | `std::function<void(AssetMetaDyn&)>`                          | ✅      |                                 |
| `loader_settings_meta_transform<S>(fn)`                                          | creates MetaTransform that modifies settings                 | ✅                                                             | ✅      |                                 |
| `meta_transform_settings(meta, fn)`                                              | applies settings transform                                   | ✅                                                             | ✅      |                                 |
| `AssetHash`                                                                      | `[u8; 32]`                                                   | `array<uint8_t, 32>`                                          | ✅      |                                 |
| `ProcessedInfo` fields: `hash, full_hash, process_dependencies`                  | ✅                                                            | ✅                                                             | ✅      |                                 |
| `AssetMetaMinimal` / `AssetActionMinimal`                                        | minimal structs for parsing loader name only                 | `AssetMetaMinimal`, `AssetActionMinimal` structs in meta.cppm | ✅      | needed for `.meta` file reading |
| `deserialize_meta(bytes)`                                                        | parses meta from bytes                                       | 🚫                                                             | 🚫      | needs serde equivalent          |

---

## 11. `Assets<T>` / `AssetStorage<T>` (`store.cppm`)

### Internal storage

| Entity                                       | Bevy                             | C++                                | Status | Notes |
| -------------------------------------------- | -------------------------------- | ---------------------------------- | ------ | ----- |
| `AssetStorage<T>` (dense generational slots) | absent; Bevy uses `DenseSlotMap` | C++: custom `AssetStorage<T>`      | ✅      |       |
| `Assets<T>` fields: `dense_storage`          | ✅                                | ✅                                  | ✅      |       |
| `Assets<T>` fields: `handle_provider`        | `AssetHandleProvider`            | `HandleProvider`                   | ✅      |       |
| `Assets<T>` fields: `mapped_assets`          | `HashMap<UntypedAssetId, T>`     | `unordered_map<UntypedAssetId, T>` | ✅      |       |
| `Assets<T>` fields: `queued_events`          | `Vec<AssetEvent<T>>`             | `m_cached_events`                  | ✅      |       |
| `Assets<T>` fields: `ref_change_sender`      | `Sender<RefChange>`              | held inside `HandleProvider`       | ✅      |       |

### Mutation methods

| Method                       | Bevy                | C++                                  | Status | Notes     |
| ---------------------------- | ------------------- | ------------------------------------ | ------ | --------- |
| `add(asset)`                 | `-> Handle<T>`      | ✅                                    | ✅      |           |
| `emplace(args...)`           | ❌ absent            | C++: `emplace(args...) -> Handle<T>` | ✅      | C++ extra |
| `insert(id, asset)`          | ✅                   | ✅                                    | ✅      |           |
| `reserve_handle()`           | `-> Handle<T>`      | ✅                                    | ✅      |           |
| `remove(id)`                 | `-> Option<T>`      | `remove(id) -> optional<T>`          | ✅      |           |
| `remove_untracked(id)`       | `-> Option<T>`      | ✅                                    | ✅      |           |
| `get_or_insert_with(id, fn)` | `-> &mut T`         | ✅                                    | ✅      |           |
| `get_mut_untracked(id)`      | `-> Option<&mut T>` | ✅                                    | ✅      |           |

### Query methods

| Method                  | Bevy                                 | C++                                       | Status | Notes     |
| ----------------------- | ------------------------------------ | ----------------------------------------- | ------ | --------- |
| `get(id)`               | `-> Option<&T>`                      | ✅                                         | ✅      |           |
| `get_mut(id)`           | `-> Option<&mut T>`                  | ✅                                         | ✅      |           |
| `contains(id)`          | `-> bool`                            | ✅                                         | ✅      |           |
| `is_empty()`            | `-> bool`                            | ✅                                         | ✅      |           |
| `len()`                 | `-> usize`                           | ✅                                         | ✅      |           |
| `ids()`                 | iterator over `AssetId<T>`           | ✅                                         | ✅      |           |
| `iter()`                | iterator over `(AssetId<T>, &T)`     | ✅                                         | ✅      |           |
| `iter_mut()`            | iterator over `(AssetId<T>, &mut T)` | ✅                                         | ✅      |           |
| `get_strong_handle(id)` | absent                               | C++: `get_strong_handle(id) -> Handle<T>` | ✅      | C++ extra |

### System methods

| Method                          | Bevy                                                                  | C++                                | Status | Notes                             |
| ------------------------------- | --------------------------------------------------------------------- | ---------------------------------- | ------ | --------------------------------- |
| `track_assets(world)`           | system: drains ref-change channel, inserts/removes from dense storage | C++: `handle_events(res_mut, res)` | ⚠️      | Same purpose, different API shape |
| `asset_events(res_mut, writer)` | system: flushes `queued_events` → `EventWriter<AssetEvent<T>>`        | ✅                                  | ✅      |                                   |
| `handle_events_manual(events)`  | absent                                                                | C++ extra for testing              | ✅      |                                   |

---

## 12. Events (`store.cppm`)

| Entity                                                  | Bevy                      | C++                                      | Status | Notes |
| ------------------------------------------------------- | ------------------------- | ---------------------------------------- | ------ | ----- |
| `AssetEvent<T>::Added { id }`                           | ✅                         | ✅                                        | ✅      |       |
| `AssetEvent<T>::Modified { id }`                        | ✅                         | ✅                                        | ✅      |       |
| `AssetEvent<T>::Removed { id }`                         | ✅                         | ✅                                        | ✅      |       |
| `AssetEvent<T>::Unused { id }`                          | ✅                         | ✅                                        | ✅      |       |
| `AssetEvent<T>::LoadedWithDependencies { id }`          | ✅                         | ✅                                        | ✅      |       |
| `AssetEvent::asset_id()`                                | helper method             | ✅                                        | ✅      |       |
| `AssetEvent::is_loaded_with_dependencies(id)`           | `-> bool`                 | ✅                                        | ✅      |       |
| `AssetLoadFailedEvent<T>` fields: `id, path, error`     | ✅                         | ✅                                        | ✅      |       |
| `UntypedAssetLoadFailedEvent` fields: `id, path, error` | ✅                         | ✅                                        | ✅      |       |
| `LoadedFolder { handles: Vec<UntypedHandle> }`          | ✅                         | ✅                                        | ✅      |       |
| `LoadedUntypedAsset { handle: UntypedHandle }`          | `#[dependency]` attribute | C++: field present, no dependency marker | ⚠️      |       |

---

## 13. Saver (`saver.cppm`)

| Entity                                                                                        | Bevy                        | C++                                                            | Status | Notes |
| --------------------------------------------------------------------------------------------- | --------------------------- | -------------------------------------------------------------- | ------ | ----- |
| `AssetSaver` trait: `Asset, Settings, OutputLoader, Error`                                    | ✅                           | C++: uses `AssetType` param instead of `Asset` associated type | ⚠️      |       |
| `AssetSaver::save(writer, asset, settings) -> Result<OutputLoader::Settings>`                 | ✅                           | ✅                                                              | ✅      |       |
| `ErasedAssetSaver` abstract: `save(writer, asset, settings) -> Result<Box<dyn AssetMetaDyn>>` | ✅                           | ✅                                                              | ✅      |       |
| `ErasedAssetSaverImpl<T>` blanket implementation                                              | ✅                           | ✅                                                              | ✅      |       |
| `SavedAsset<A>` struct: `erased: &ErasedLoadedAsset`                                          | ✅                           | ✅                                                              | ✅      |       |
| `SavedAsset::get()`                                                                           | `-> &A`                     | ✅                                                              | ✅      |       |
| `SavedAsset::get_labeled<B>(label)`                                                           | `-> Option<&B>`             | ✅                                                              | ✅      |       |
| `SavedAsset::get_handle<B>(label)`                                                            | `-> Option<UntypedHandle>`  | ✅                                                              | ✅      |       |
| `SavedAsset::labeled_assets()`                                                                | iterator                    | ✅                                                              | ✅      |       |
| `SavedAsset::labels()`                                                                        | iterator over label strings | ✅                                                              | ✅      |       |

---

## 14. Transformer (`transformer.cppm`)

| Entity                                                                             | Bevy                                   | C++                           | Status | Notes                            |
| ---------------------------------------------------------------------------------- | -------------------------------------- | ----------------------------- | ------ | -------------------------------- |
| `AssetTransformer` trait: `AssetInput, AssetOutput, Settings, Error`               | ✅                                      | C++: duck typing concept only | ❌      | C++ missing explicit concept     |
| `AssetTransformer::transform(asset, settings) -> Result<TransformedAsset<Output>>` | ✅                                      | ✅                             | ✅      |                                  |
| `TransformedAsset<A>` struct                                                       | ✅                                      | ✅                             | ✅      |                                  |
| `TransformedAsset::get()`                                                          | `-> &A`                                | ✅                             | ✅      |                                  |
| `TransformedAsset::get_mut()`                                                      | `-> &mut A`                            | ✅                             | ✅      |                                  |
| `TransformedAsset::replace_asset<B>(asset)`                                        | replaces main asset, keeping labels    | ✅                             | ✅      |                                  |
| `TransformedAsset::take_labeled_assets<B>()`                                       | extract labeled assets from sub-assets | ✅                             | ✅      |                                  |
| `TransformedAsset::get_labeled<B>(label)`                                          | ✅                                      | ✅                             | ✅      |                                  |
| `TransformedAsset::insert_labeled(label, asset)`                                   | ✅                                      | ✅                             | ✅      |                                  |
| `TransformedAsset::labels()`                                                       | ✅                                      | ✅                             | ✅      |                                  |
| `TransformedSubAsset<A>`                                                           | absent in Bevy                         | C++ extra                     | ✅      | maps to Bevy's in-place mutation |
| `IdentityAssetTransformer<A>`                                                      | ✅                                      | ✅                             | ✅      |                                  |

---

## 15. IO Types

### Reader / Writer interfaces (`io/reader.cppm`)

| Entity                                                                                                       | Bevy                                    | C++                                    | Status | Notes                                 |
| ------------------------------------------------------------------------------------------------------------ | --------------------------------------- | -------------------------------------- | ------ | ------------------------------------- |
| `Reader` trait                                                                                               | `AsyncRead + Unpin` (async byte stream) | `std::istream&`                        | ⚠️      | translation rule; no async            |
| `AssetReader` trait: `read(path)`                                                                            | `async -> Result<Box<dyn Reader>>`      | sync `(path) -> unique_ptr<istream>`   | ⚠️      |                                       |
| `AssetReader` trait: `read_meta(path)`                                                                       | `async -> Result<Box<dyn Reader>>`      | ✅                                      | ⚠️      | async→sync                            |
| `AssetReader` trait: `read_directory(path)`                                                                  | `async -> Result<Box<dyn PathStream>>`  | ✅                                      | ⚠️      |                                       |
| `AssetReader` trait: `is_directory(path)`                                                                    | `async -> Result<bool>`                 | ✅                                      | ⚠️      |                                       |
| `ErasedAssetReader`                                                                                          | type-erased `AssetReader`               | ✅                                      | ✅      |                                       |
| `AssetWriter` trait methods: `write, write_meta, remove, remove_meta, rename, rename_meta, remove_directory` | ✅                                       | ✅                                      | ✅      |                                       |
| `ErasedAssetWriter`                                                                                          | type-erased `AssetWriter`               | ✅                                      | ✅      |                                       |
| `AssetReaderError` variants: `NotFound(path), Io(error), HttpError(u16, body)`                               | ✅                                       | `NotFound, Io` (no Http)               | ⚠️      | Http → N/A                            |
| `AssetWriterError::Io(error)`                                                                                | ✅                                       | ✅                                      | ✅      |                                       |
| `AssetSourceEvent` variants: `Added, Modified, Removed, Renamed { from, to }`                                | ✅                                       | ✅                                      | ✅      |                                       |
| `AssetWatcher` abstract: `on_event(fn)`                                                                      | ✅                                       | ✅                                      | ✅      |                                       |
| `VecReader`                                                                                                  | in-memory `Reader` backed by `Vec<u8>`  | C++: `VecReader` wraps `istringstream` | ✅      | implements `operator std::istream&()` |

### Gated readers (`io/processor_gated.cppm`)

| Entity                  | Bevy                                        | C++                | Status | Notes |
| ----------------------- | ------------------------------------------- | ------------------ | ------ | ----- |
| `ProcessorGatedReader`  | blocks `read_meta` until processor finishes | ✅                  | ✅      |       |
| `GatedReader` (generic) | wraps inner reader, gates `read`            | C++: `GatedReader` | ✅      |       |

### Memory asset reader (`io/memory.cppm` / `memory_asset.cppm`)

| Entity                                  | Bevy                        | C++ | Status | Notes               |
| --------------------------------------- | --------------------------- | --- | ------ | ------------------- |
| `MemoryAssetReader`                     | in-memory map of path→bytes | ✅   | ✅      |                     |
| `memory::Directory`                     | virtual directory entry     | ✅   | ✅      | C++ internal helper |
| `memory::Value`                         | file or directory node      | ✅   | ✅      | C++ internal helper |
| `MemoryAssetReader::insert(path, data)` | ✅                           | ✅   | ✅      |                     |
| `MemoryAssetReader::remove(path)`       | ✅                           | ✅   | ✅      |                     |

### Embedded asset registry (`io/embedded.cppm`)

| Entity                                       | Bevy | C++ | Status | Notes |
| -------------------------------------------- | ---- | --- | ------ | ----- |
| `EmbeddedAssetRegistry`                      | ✅    | ✅   | ✅      |       |
| `insert_asset(path, full_path, data)`        | ✅    | ✅   | ✅      |       |
| `insert_asset_static(path, full_path, data)` | ✅    | ✅   | ✅      |       |
| `insert_meta(path, full_path, data)`         | ✅    | ✅   | ✅      |       |
| `remove_asset(path)`                         | ✅    | ✅   | ✅      |       |
| `register_source(sources)`                   | ✅    | ✅   | ✅      |       |
| `EMBEDDED` source id constant                | ✅    | ✅   | ✅      |       |

### File asset reader/writer/watcher (`io/file_asset.cppm`, `io/file_watcher.cppm`)

| Entity             | Bevy                       | C++ | Status | Notes |
| ------------------ | -------------------------- | --- | ------ | ----- |
| `FileAssetReader`  | reads from filesystem      | ✅   | ✅      |       |
| `FileAssetWriter`  | writes to filesystem       | ✅   | ✅      |       |
| `FileAssetWatcher` | watches filesystem changes | ✅   | ✅      |       |

### Asset sources (`io/source.cppm`)

| Entity                                                                                                                                               | Bevy                          | C++                                               | Status | Notes                   |
| ---------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------- | ------------------------------------------------- | ------ | ----------------------- |
| `AssetSource` fields: `id, reader, writer, processed_reader, ungated_processed_reader, processed_writer, event_receiver, watcher, processed_watcher` | ✅                             | ✅                                                 | ✅      |                         |
| `AssetSource::gate_on_processor(barrier)`                                                                                                            | ✅                             | ✅                                                 | ✅      |                         |
| `AssetSource::get_default_reader(path)`                                                                                                              | static factory                | ✅                                                 | ✅      |                         |
| `AssetSource::get_default_writer(path)`                                                                                                              | static factory                | ✅                                                 | ✅      |                         |
| `AssetSource::get_default_watcher(path, debounce)`                                                                                                   | static factory                | ✅                                                 | ✅      |                         |
| `AssetSource::get_default_processed_reader(path)`                                                                                                    | static factory                | ✅                                                 | ✅      |                         |
| `AssetSource::get_default_processed_writer(path)`                                                                                                    | static factory                | ✅                                                 | ✅      |                         |
| `AssetSourceBuilder` fields: `reader, writer, processed_reader, processed_writer, watcher, processed_watcher`                                        | ✅                             | ✅                                                 | ✅      |                         |
| `AssetSourceBuilder::build(id)`                                                                                                                      | `-> AssetSource`              | ✅                                                 | ✅      |                         |
| `AssetSourceBuilder::with_reader(fn)` etc.                                                                                                           | builder methods               | ✅                                                 | ✅      |                         |
| `AssetSourceBuilder::platform_default(path, processed_path)`                                                                                         | picks OS-appropriate defaults | ✅                                                 | ✅      |                         |
| `AssetSourceBuilders` struct: `insert, get, init_default, build_sources`                                                                             | ✅                             | C++: `init_default` vs Bevy `init_default_source` | ⚠️      | minor naming difference |
| `AssetSources` struct: `get, iter, iter_mut, iter_processed, gate_on_processor`                                                                      | ✅                             | ✅                                                 | ✅      |                         |

---

## 16. Processor Types (`processor/process.cppm` + `processor/mod.cppm`)

### `Process` concept / trait

| Entity                                    | Bevy                                           | C++  | Status | Notes      |
| ----------------------------------------- | ---------------------------------------------- | ---- | ------ | ---------- |
| `Process::Settings`                       | ✅                                              | ✅    | ✅      |            |
| `Process::OutputLoader`                   | ✅                                              | ✅    | ✅      |            |
| `Process::process(ctx, settings, writer)` | `async -> Result<ProcessResult, ProcessError>` | sync | ⚠️      | async→sync |

### Error / result types

| Entity                                                                           | Bevy                                                                                                                                                                                                                                                           | C++ | Status | Notes |
| -------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --- | ------ | ----- |
| `ProcessError` variants                                                          | `MissingAssetLoader, MissingProcessor, AmbiguousProcessor, AssetReaderError, AssetWriterError, MissingProcessedReader/Writer, ReadAssetMetaError, DeserializeMetaError, AssetLoadError, WrongMetaType, AssetSaveError, AssetTransformError, ExtensionRequired` | ✅   | ✅      |       |
| `ProcessResult` variants: `Processed(ProcessedInfo), SkippedNotChanged, Ignored` | ✅                                                                                                                                                                                                                                                              | ✅   | ✅      |       |
| `ProcessStatus` variants: `Processed, Failed, NonExistent`                       | ✅                                                                                                                                                                                                                                                              | ✅   | ✅      |       |
| `GetProcessorError` variants: `Missing, Ambiguous`                               | ✅                                                                                                                                                                                                                                                              | ✅   | ✅      |       |

### Erased processor

| Entity                                                                                             | Bevy | C++ | Status | Notes |
| -------------------------------------------------------------------------------------------------- | ---- | --- | ------ | ----- |
| `ErasedProcessor` abstract: `process, deserialize_meta, type_path, default_meta, default_settings` | ✅    | ✅   | ✅      |       |
| `ErasedProcessorImpl<P>` blanket impl                                                              | ✅    | ✅   | ✅      |       |

### `LoadTransformAndSave<L, T, S>`

| Entity                                                 | Bevy | C++ | Status | Notes |
| ------------------------------------------------------ | ---- | --- | ------ | ----- |
| `LoadTransformAndSaveSettings<LS, TS, SS>`             | ✅    | ✅   | ✅      |       |
| `LoadTransformAndSave::process(ctx, settings, writer)` | ✅    | ✅   | ✅      |       |

### `ProcessContext`

| Entity                                                   | Bevy           | C++ | Status | Notes |
| -------------------------------------------------------- | -------------- | --- | ------ | ----- |
| `ProcessContext` fields: `path, asset_reader, processor` | ✅              | ✅   | ✅      |       |
| `ProcessContext::new_processed_info`                     | optional field | ✅   | ✅      |       |

### Processor state management

| Entity                                                                                                                                     | Bevy | C++ | Status | Notes |
| ------------------------------------------------------------------------------------------------------------------------------------------ | ---- | --- | ------ | ----- |
| `ProcessorState` variants: `Initializing, Processing, Finished`                                                                            | ✅    | ✅   | ✅      |       |
| `ProcessorAssetInfo` fields: `processed_info, dependents, status, file_transaction_lock, status_sender/receiver`                           | ✅    | ✅   | ✅      |       |
| `ProcessorAssetInfos` methods: `get_or_insert, get, add_dependent, remove, finish_processing, clear_dependencies`                          | ✅    | ✅   | ✅      |       |
| `ProcessingState` methods: `set_state, get_state, wait_until_processed, get_transaction_lock, wait_until_initialized, wait_until_finished` | ✅    | ✅   | ✅      |       |

### `AssetProcessorData`

| Field                | Bevy                                              | C++ | Status | Notes |
| -------------------- | ------------------------------------------------- | --- | ------ | ----- |
| `processing_state`   | `RwLock<ProcessorState>`                          | ✅   | ✅      |       |
| `log_factory`        | `Box<dyn ProcessorTransactionLogFactory>`         | ✅   | ✅      |       |
| `log`                | `Mutex<Option<Box<dyn ProcessorTransactionLog>>>` | ✅   | ✅      |       |
| `processors`         | `HashMap<&str, Arc<dyn ErasedProcessor>>`         | ✅   | ✅      |       |
| `source_builders`    | `AssetSourceBuilders`                             | ✅   | ✅      |       |
| `default_processors` | `HashMap<&str, &str>` (ext→processor type)        | ✅   | ✅      |       |

### `AssetProcessor` methods

| Method                                                   | Bevy                                                           | C++ | Status | Notes                   |
| -------------------------------------------------------- | -------------------------------------------------------------- | --- | ------ | ----------------------- |
| `register_processor<P>(processor)`                       | ✅                                                              | ✅   | ✅      |                         |
| `set_default_processor<P>(extension)`                    | ✅                                                              | ✅   | ✅      |                         |
| `get_default_processor(extension)`                       | ✅                                                              | ✅   | ✅      |                         |
| `get_processor(type_path)`                               | ✅                                                              | ✅   | ✅      |                         |
| `start(system)`                                          | `async` — listens for source changes, processes initial assets | ✅   | ✅      |                         |
| `initialize()`                                           | validates logs, recovers, queues initial tasks                 | ✅   | ✅      |                         |
| `process_asset(path)`                                    | processes single asset                                         | ✅   | ✅      |                         |
| `process_asset_internal(source, reader, path, log)`      | internal core                                                  | ✅   | ✅      |                         |
| `handle_added_folder(path)`                              | ✅                                                              | ✅   | ✅      |                         |
| `handle_removed_meta/asset/folder`                       | ✅                                                              | ✅   | ✅      |                         |
| `handle_renamed_asset(old, new)`                         | ✅                                                              | ✅   | ✅      |                         |
| `queue_processing_tasks_for_folder(path)`                | ✅                                                              | ✅   | ✅      |                         |
| `queue_initial_processing_tasks(source, reader, path)`   | ✅                                                              | ✅   | ✅      |                         |
| `spawn_source_change_event_listeners(sources)`           | ✅                                                              | ✅   | ✅      |                         |
| `execute_processing_tasks(tasks)`                        | ✅                                                              | ✅   | ✅      |                         |
| `validate_transaction_log_and_recover()`                 | ✅                                                              | ✅   | ✅      |                         |
| `remove_processed_asset_and_meta(writer, path)`          | ✅                                                              | ✅   | ✅      |                         |
| `clean_empty_processed_ancestor_folders(writer, path)`   | ✅                                                              | ✅   | ⚠️      | C++ may differ slightly |
| `write_default_meta_file_for_path(source, writer, path)` | ✅                                                              | ✅   | ✅      |                         |
| `log_begin/end_processing(log, path)`                    | ✅                                                              | ✅   | ✅      |                         |
| `log_unrecoverable(log)`                                 | ✅                                                              | ✅   | ✅      |                         |

---

## 17. Processor Log (`processor/log.cppm`)

| Entity                                                                                                         | Bevy                           | C++ | Status | Notes |
| -------------------------------------------------------------------------------------------------------------- | ------------------------------ | --- | ------ | ----- |
| `LogEntryKind` variants: `BeginProcessing, EndProcessing, UnrecoverableError`                                  | ✅                              | ✅   | ✅      |       |
| `LogEntry` struct + static constructors: `begin_processing(path), end_processing(path), unrecoverable_error()` | ✅                              | ✅   | ✅      |       |
| `ProcessorTransactionLogFactory` abstract: `read() -> Result<(log, entries)>, create_new_log() -> Box<log>`    | ✅                              | ✅   | ✅      |       |
| `ProcessorTransactionLog` abstract: `begin_processing(path), end_processing(path), unrecoverable()`            | ✅                              | ✅   | ✅      |       |
| `LogEntryError` variants: `DuplicateTransaction, EndedMissingTransaction, UnfinishedTransaction(path)`         | ✅                              | ✅   | ✅      |       |
| `ValidateLogError` variants: `UnrecoverableError, ReadLogError(err), EntryErrors(vec)`                         | ✅                              | ✅   | ✅      |       |
| `validate_transaction_log(factory)` free function                                                              | ✅                              | ✅   | ✅      |       |
| `FileTransactionLogFactory`                                                                                    | reads/creates log file on disk | ✅   | ✅      |       |
| `FileProcessorTransactionLog`                                                                                  | writes log entries to file     | ✅   | ✅      |       |

---

## 18. Plugin & App Integration (`assets.cppm`)

### `AssetPlugin` fields

| Field                          | Bevy                                  | C++                | Status | Notes                           |
| ------------------------------ | ------------------------------------- | ------------------ | ------ | ------------------------------- |
| `file_path`                    | `String`                              | `filesystem::path` | ✅      |                                 |
| `processed_file_path`          | `String`                              | `filesystem::path` | ✅      |                                 |
| `watch_for_changes_override`   | `Option<bool>`                        | `optional<bool>`   | ✅      |                                 |
| `use_asset_processor_override` | absent; Bevy uses `mode`              | C++: separate bool | ⚠️      |                                 |
| `mode`                         | `AssetMode::Unprocessed \| Processed` | ✅                  | ✅      |                                 |
| `meta_check`                   | `AssetMetaCheck`                      | ✅                  | ⚠️      | `Paths` variant missing HashSet |
| `unapproved_path_mode`         | `UnapprovedPathMode`                  | ✅                  | ✅      |                                 |

### `AssetPlugin` methods

| Method                              | Bevy                                 | C++ | Status | Notes |
| ----------------------------------- | ------------------------------------ | --- | ------ | ----- |
| `build(app)`                        | registers all systems, resources     | ✅   | ✅      |       |
| `finish(app)`                       | finalizes (creates AssetServer etc.) | ✅   | ✅      |       |
| `register_asset_source(id, source)` | adds source to plugin before build   | ✅   | ✅      |       |

### Enum types

| Entity                       | Bevy | C++ | Status | Notes |
| ---------------------------- | ---- | --- | ------ | ----- |
| `AssetMode::Unprocessed`     | ✅    | ✅   | ✅      |       |
| `AssetMode::Processed`       | ✅    | ✅   | ✅      |       |
| `AssetSystems::HandleEvents` | ✅    | ✅   | ✅      |       |
| `AssetSystems::WriteEvents`  | ✅    | ✅   | ✅      |       |

### App helper free functions (C++) / `AssetApp` trait methods (Bevy)

| Entity                                               | Bevy (`AssetApp` trait)               | C++                                          | Status | Notes                      |
| ---------------------------------------------------- | ------------------------------------- | -------------------------------------------- | ------ | -------------------------- |
| `init_asset<A>()`                                    | ✅                                     | `app_register_asset<T>(app)`                 | ✅      | free function in C++       |
| `init_asset_loader<L>()`                             | ✅                                     | `app_register_loader<T>(app, loader)`        | ✅      |                            |
| `register_asset_loader<L>(loader)`                   | ✅                                     | `app_preregister_loader<T>(app, extensions)` | ⚠️      | slightly different purpose |
| `configure_asset_meta_check(check)`                  | ✅                                     | absent as separate method                    | ❌      | set via `AssetPlugin` only |
| `register_asset_reflect<A>()`                        | reflection registration               | 🚫                                            | 🚫      | no reflection system       |
| `register_asset_source(id, source)`                  | ✅                                     | ✅                                            | ✅      |                            |
| `app_register_asset_processor<P>(app, processor)`    | Bevy: `register_asset_processor<P>()` | ✅                                            | ✅      |                            |
| `app_set_default_asset_processor<P>(app, extension)` | Bevy: `set_default_processor<P>(ext)` | ✅                                            | ✅      |                            |
| `log_unrecoverable(log)`                             | ✅                                     | ✅                                            | ✅      |                            |
