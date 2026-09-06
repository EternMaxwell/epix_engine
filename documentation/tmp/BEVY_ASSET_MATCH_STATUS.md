# Asset parity status

This file is a historical parity tracker, not the public API reference. The old
three-month snapshot described the asset pipeline before its asynchronous
conversion and is no longer applicable.

## Current implementation baseline

- Asset I/O uses asynchronous `Reader` and `Writer` abstractions.
- Loader, saver, transformer, and processor work returns `STDEXEC::task`.
- Asset work is scheduled through `epix::task`, including `IoTaskPool`.
- `AssetServer` returns handles immediately for ordinary loads and processes
  internal load events through the ECS schedules installed by `AssetPlugin`.
- Blocking readiness remains available through `wait_for_asset()` and
  `load_acquire()`, with the documented thread restrictions.
- Processing metadata, dependency tracking, source registration, embedded and
  filesystem readers, hot reload, and typed lifecycle events are implemented.

For maintained interfaces and examples, use the pages in
[`documentation/assets`](../assets/quick.md). Any future parity work should be
derived from current headers under `epix_engine/assets/include/epix/assets`, not
from the removed synchronous design.

## Deferred Bevy 0.18 re-audit (2026-09-06)

Reference source: local `tmp/bevy-0.18.0`, tag `v0.18.0`, commit
`5f8270f2e049f90139a503d1e930070d926f9427`. The older work and the historical
section above do not identify a frozen Bevy revision, so they are not sufficient
evidence that the whole module matches 0.18. The current implementation is
broadly Bevy-shaped, but the following source-confirmed items need a full audit
later. This list is preliminary, not an exhaustive parity claim.

| ID | Area | Preliminary Bevy 0.18 comparison | Status |
| --- | --- | --- | --- |
| A1 | Asset-change filter | Bevy has `AsAssetId`, private `AssetChanges`, and `AssetChanged`. Epix gained this while matching mesh bounds. The C++ customization surface is `AsAssetId<T>` and its validating concept is `AsAssetIdImpl`. | Implemented and verified in current parent checkpoint |
| A2 | System ticks used by asset events | Bevy's `Assets::asset_events` takes the read-only `SystemChangeTick` system parameter and records `ticks.this_run()`. Epix had no equivalent and temporarily inferred the tick from the mutable `AssetChanges` resource, which is not the same contract. | Implemented and verified in current ECS parent checkpoint |
| A3 | `AssetPlugin` defaults | Bevy defaults to `AssetMode::Unprocessed`, `file_path = "assets"`, and `processed_file_path = "imported_assets/Default"`. Epix defaults to processed mode and `processed_assets`; it also has an Epix-specific optional embedded processed path. | Decision required later |
| A4 | Direct world access helpers | Bevy exports `DirectAssetAccessExt::{add_asset, load_asset, load_asset_with_settings}` on `World`. No corresponding Epix world helpers were found. | Missing |
| A5 | Asset notification transport | Bevy 0.18 publishes asset notifications through ECS Messages; Epix uses its Event subsystem. This may be an accepted ECS/app adaptation, but the behavior and retention/scheduling semantics still need explicit comparison. | Deferred architecture review |
| A6 | Collection traversal APIs | Bevy `Assets::{ids, iter, iter_mut}` return lazy iterators. Epix `ids()` allocates a vector and `iter`/`iter_mut` are callback APIs. Under the established Rust-iterator-to-C++-range policy these shapes do not match. | Missing |
| A7 | Event representation | Bevy's `AssetEvent<A>` is a tagged enum with per-variant `id`; Epix stores a separate enum discriminator plus an always-present `id`. Effects are similar, but this is an API/data-shape mismatch that needs an explicit decision. | Deferred |
| A8 | Path ownership and parsing | Bevy's `AssetPath<'a>` supports borrowed/owned path data and returns typed `ParseAssetPathError`; Epix always owns its strings/path and `try_parse` returns only `optional`. C++ ownership adaptation may be intentional, but parse-error loss is observable. | Deferred |

Already source-checked as present in Epix: `get_or_insert_with`,
`get_mut_untracked`, `remove_untracked`, load override/settings/acquire variants,
labeled asset scopes, transformed/saved assets, and unapproved path modes. Their
complete signatures and edge behavior have not yet been audited line by line.
