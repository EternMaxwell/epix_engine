# TODO — Assets

Tracks partially implemented or stub features in the `epix_assets` module.
See also: [project-wide todo](../todo.md)

---

## [x] Meta serialization & deserialization {#meta-serialization}

**Status:** Implemented — binary `.meta` files are fully round-tripped via **zpp::bits**.

`AssetMeta<LS,PS>` has `serialize_bytes()` (virtual, inherited from `AssetMetaDyn`) and a
free-function `deserialize_asset_meta<LS,PS>()`. The load pipeline reads `.meta` bytes,
deserializes via `ErasedAssetLoader::deserialize_meta()`, and applies the stored loader
settings. On the processor side, `ErasedProcessor::deserialize_meta()` handles processor
settings.

Settings types must satisfy the `is_settings` concept (zpp::bits-serializable +
default-constructible). The `SettingsImpl<T>` wrapper provides polymorphic storage.

---

## [~] Async I/O

**Status:** Interface is synchronous; coroutine support is planned but not implemented.

**Root cause:** `AssetReader::read()` returns a `std::istream` rather than an awaitable. The
IOTaskPool threads block for the duration of each read, limiting I/O parallelism.

**Impact:**
- Each load task occupies an IOTaskPool thread for the full duration of the read.
- Large or slow reads (network, compressed archives) stall pool workers.
- No back-pressure or cancellation mechanism.

**Evidence:**
- `epix_engine/assets/modules/io/reader.cppm:24` —
  `// TODO: use coroutines async operation for readers, and return a future/awaitable instead of blocking the thread`

**What remains:**
1. Change `AssetReader::read()` to return an awaitable/coroutine type.
2. Replace pool-thread blocking with a coroutine-based I/O scheduler.
3. Update all concrete readers (`MemoryAssetReader`, `FileAssetReader`) to the new interface.

---

## [ ] `NestedLoader` — immediate mode missing

**Status:** Missing.

**Problem:** Bevy's `NestedLoader` supports an `immediate()` mode that loads a sub-asset synchronously
and returns `LoadedAsset<A>` (the actual value) rather than a deferred `Handle<A>`. It can also accept a
pre-opened reader via `with_reader(reader)`. C++ only implements the deferred path.

**Evidence:** Verified against `modules/server/loader.cppm` — `NestedLoader` has no `immediate()` or
`with_reader()` methods.

**What remains:**
1. Add `NestedLoader::immediate()` that returns `std::expected<LoadedAsset<A>, AssetLoadError>` by
   calling `load_direct` internally.
2. Optionally `with_reader(istream&)` to supply a pre-opened stream.

---

## [ ] `AssetServer::write_default_loader_meta_file_for_path`

**Status:** Missing. Associated error type `WriteDefaultMetaError` also absent.

**Problem:** Bevy's `AssetServer` exposes a method to write a default `.meta` sidecar file alongside a
source asset, allowing users to bootstrap settings editing. No equivalent exists in C++.

**What remains:**
1. Add `WriteDefaultMetaError` error type.
2. Add `AssetServer::write_default_loader_meta_file_for_path(AssetPath)`.

---

## [ ] `AsAssetId` — ECS component trait missing

**Status:** Missing.

**Problem:** Bevy provides an `AsAssetId` trait (concept) that ECS components can implement to expose
their inner `AssetId`. This enables generic systems that accept any asset-holding component. No C++
equivalent exists; `Handle<T>` and `UntypedHandle` do not participate in such a protocol.

**What remains:**
1. Define an `AsAssetId` concept in `asset_id.cppm` requiring `.as_asset_id() -> UntypedAssetId`.
2. Implement it on `Handle<T>` and `UntypedHandle`.

---

## [ ] `DirectAssetAccessExt` — World-level asset helpers missing

**Status:** Missing.

**Problem:** Bevy provides `World::add_asset<A>()`, `World::load_asset<A>()`, and
`World::load_asset_with_settings<A,S>()` for one-line asset operations from exclusive-world systems.
No equivalent exists on C++ `World`.

**What remains:**
1. Add free functions (or `World` extensions) `world_add_asset<A>`, `world_load_asset<A>`,
   `world_load_asset_with_settings<A,S>`.

---

## [ ] `publish_asset_server_diagnostics` system missing

**Status:** Missing.

**Problem:** Bevy ships a diagnostics system that emits `AssetServerStats` values into the diagnostics
framework each frame. No equivalent system is registered by `AssetPlugin` in C++.

**What remains:**
1. Implement a system reading `AssetServerStats` from `AssetServer` and writing into
   `epix::core::diagnostics` (once a diagnostics subsystem exists).

---

## [ ] `AssetInfos::infos` — `unordered_map` vs SlotMap

**Status:** Perf divergence from Bevy.

**Problem:** Bevy's `AssetInfos` stores per-asset info in a `DenseSlotMap<AssetIndex, AssetInfo>`,
giving O(1) index-keyed access and good cache locality. C++ uses
`unordered_map<UntypedAssetId, AssetInfo>`, which hashes a `{type_index, variant<index,uuid>}` struct
on every lookup.

**Evidence:** `modules/server/info.cppm:78` — `std::unordered_map<UntypedAssetId, AssetInfo> infos;`.

**What remains:**
1. Profile to determine if this is a measurable bottleneck.
2. Optionally replace with a two-level structure: `type_index → DenseSlotMap<AssetIndex, AssetInfo>` for
   index-backed assets, with a separate map for UUID-backed assets.
