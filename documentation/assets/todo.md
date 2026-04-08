# TODO — Unimplemented Features

Tracks partially implemented or stub features in the `epix_assets` module.

---

## [~] Meta serialization & deserialization {#meta-serialization}

**Status:** Partially stubbed — `.meta` file bytes can be read/written at the byte level, but the
structured content is never serialized (written) or deserialized (parsed). Both directions are absent.

**Root cause:** No serde-equivalent library is available in this codebase yet.
In Bevy, `AssetMeta<L,P>` derives `serde::Serialize + serde::Deserialize` and RON is the on-disk format;
the derive macro requires zero changes to the data type itself.
C++ libraries that achieve the same zero-annotation result (e.g. `zpp_bits`, `glaze`, `reflect-cpp`, Boost.PFR
aggregate tricks) all conflict with `import std;` under the MSVC C++23 module build used by this project.
The fix is blocked on **C++26 static reflection**, which will allow non-intrusive serialization that is
compatible with C++ modules. Until then, every serialize/deserialize path in this section is a hard blocker.

**Impact:**
- `.meta` files written by the asset processor contain no structured content.
- `AssetMetaCheck::Always` locates `.meta` files but their content is silently ignored on load.
- Loader settings stored in `.meta` files are never applied — defaults are always used.
- Processor settings in `.meta` files are also never applied.

**Evidence:**
- `epix_engine/assets/src/server.cpp:389` — `// TODO: parse meta bytes with a RON (or similar) parser`
- `epix_engine/assets/modules/processor/process.cppm:186` — `// TODO: implement actual deserialization when meta serialization is implemented`
- `epix_engine/assets/src/processor.cpp:256,747` — meta read/write stubs

**Specific missing pieces (all verified against source):**

### `AssetMetaDyn::serialize()` — virtual method absent
Bevy: `trait AssetMetaDyn { fn serialize(&self) -> Vec<u8>; }` — implemented by `AssetMeta<L,P>` using RON.
C++ `AssetMetaDyn` has no `serialize()` virtual method; there is no way to write a `.meta` file from an
erased handle. File: `epix_engine/assets/modules/meta.cppm`.

### `AssetMeta<LS,PS>` — no serialize or deserialize
Bevy: `#[derive(Serialize, Deserialize)]` — full RON round-trip for the concrete meta struct.
C++ `AssetMeta<LS,PS>` stores all fields but has neither `serialize()` nor `deserialize(bytes)` methods.
File: `epix_engine/assets/modules/meta.cppm`.

### `AssetMetaMinimal` / `AssetActionMinimal` — no serialize or deserialize
Bevy: both derive `Serialize, Deserialize` for fast loader/processor name extraction.
C++ structs exist but have no (de)serialization. File: `epix_engine/assets/modules/meta.cppm`.

### `Settings` — no serialization constraint
Bevy: `AssetLoader::Settings` must satisfy `Settings + Serialize + Deserialize + Default + Clone`.
C++: `struct Settings { virtual ~Settings() = default; }` — no serialization interface or concept
constraint. Every concrete settings type is unreachable from `.meta` bytes until this is resolved.
File: `epix_engine/assets/modules/meta.cppm`.

### `ErasedAssetLoader::deserialize_meta(bytes)` — virtual method absent
Bevy: `ErasedAssetLoader::deserialize_meta(bytes) -> Result<Box<dyn AssetMetaDyn>>`.
C++ `ErasedAssetLoader` only declares `default_meta()`; there is no virtual `deserialize_meta`.
Without it the server cannot reconstruct a typed `AssetMeta<LS,PS>` from a `.meta` file.
File: `epix_engine/assets/modules/server/loader.cppm`.

**What remains:**
1. **Unblocked by C++26 reflection** — adopt a serde-equivalent (e.g. `reflect-cpp` once `import std`
   conflict is resolved, or a C++26 reflection-driven serializer) as the codec for all meta types.
2. Add serialization/deserialization to `Settings`-derived types (non-intrusive via the chosen library —
   requires no base-class changes once reflection is available).
3. Add `virtual std::vector<uint8_t> serialize() const = 0` to `AssetMetaDyn`; implement in
   `AssetMeta<LS,PS>` using the chosen format.
4. Add `static std::expected<AssetMeta<LS,PS>, DeserializeMetaError> deserialize(std::span<const uint8_t>)`
   to `AssetMeta<LS,PS>`.
5. Add serialize/deserialize to `AssetMetaMinimal` and `AssetActionMinimal`.
6. Add `virtual std::unique_ptr<AssetMetaDyn> deserialize_meta(std::span<const uint8_t>) const = 0`
   to `ErasedAssetLoader`; implement in `ErasedAssetLoaderImpl<T>`.
7. Wire deserialized settings into `AssetServer::load()` and `AssetProcessor::process()`.

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
3. Update all concrete readers (`MemoryAssetReader`, filesystem reader) to the new interface.

---

## [ ] `NestedLoader` — immediate mode missing

**Status:** Missing.

**Problem:** Bevy's `NestedLoader` supports an `immediate()` mode that loads a sub-asset synchronously
and returns `LoadedAsset<A>` (the actual value) rather than a deferred `Handle<A>`. It can also accept a
pre-opened reader via `with_reader(reader)`. C++ only implements the deferred path.

**Evidence:** `BEVY_MATCH_STATUS.md` §9 NestedLoader table — verified against `modules/server/loader.cppm`.

**What remains:**
1. Add `NestedLoader::immediate()` that returns `std::expected<LoadedAsset<A>, AssetLoadError>` by
   calling `load_direct` internally.
2. Optionally `with_reader(istream&)` to supply a pre-opened stream.

---

## [ ] `AssetServer::write_default_loader_meta_file_for_path`

**Status:** Missing. Associated error type `WriteDefaultMetaError` also absent.

**Problem:** Bevy's `AssetServer` exposes a method to write a default `.meta` sidecar file alongside a
source asset, allowing users to bootstrap settings editing. No equivalent exists in C++. This is also
blocked by meta serialization (see above), but the method and error type should still be declared.

**Evidence:** `BEVY_MATCH_STATUS.md` §6 async-waiting table — verified against `modules/server/mod.cppm`.

**What remains:**
1. Add `WriteDefaultMetaError` error type.
2. Add `AssetServer::write_default_loader_meta_file_for_path(AssetPath)`.
3. Implementation depends on meta serialization being ready.

---

## [ ] `AsAssetId` — ECS component trait missing

**Status:** Missing.

**Problem:** Bevy provides an `AsAssetId` trait (concept) that ECS components can implement to expose
their inner `AssetId`. This enables generic systems that accept any asset-holding component. No C++
equivalent exists; `Handle<T>` and `UntypedHandle` do not participate in such a protocol.

**Evidence:** `BEVY_MATCH_STATUS.md` §1 — verified no such concept in any assets module file.

**What remains:**
1. Define an `AsAssetId` concept in `asset_id.cppm` requiring `.as_asset_id() -> UntypedAssetId`.
2. Implement it on `Handle<T>` and `UntypedHandle`.

---

## [ ] `DirectAssetAccessExt` — World-level asset helpers missing

**Status:** Missing.

**Problem:** Bevy provides `World::add_asset<A>()`, `World::load_asset<A>()`, and
`World::load_asset_with_settings<A,S>()` for one-line asset operations from exclusive-world systems.
No equivalent exists on C++ `World`.

**Evidence:** `BEVY_MATCH_STATUS.md` §1 — verified no such helpers in `assets.cppm` or elsewhere.

**What remains:**
1. Add free functions (or `World` extensions) `world_add_asset<A>`, `world_load_asset<A>`,
   `world_load_asset_with_settings<A,S>`.

---

## [ ] `publish_asset_server_diagnostics` system missing

**Status:** Missing.

**Problem:** Bevy ships a diagnostics system that emits `AssetServerStats` values into the diagnostics
framework each frame. No equivalent system is registered by `AssetPlugin` in C++.

**Evidence:** `BEVY_MATCH_STATUS.md` §6 — verified no such system in `assets.cppm` or `src/`.

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
Confirmed still present.

**What remains:**
1. Profile to determine if this is a measurable bottleneck.
2. Optionally replace with a two-level structure: `type_index → DenseSlotMap<AssetIndex, AssetInfo>` for
   index-backed assets, with a separate map for UUID-backed assets.
