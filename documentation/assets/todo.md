# Assets TODO

The asset module now uses asynchronous `Reader`/`Writer` interfaces and
stdexec tasks throughout file, memory, processing, loader, saver, and direct-load
paths. Binary sidecar metadata round-trips through zpp::bits.

Remaining gaps and possible improvements:

- `NestedLoader` has deferred typed/untyped loading and relative paths, but no
  convenience equivalent to an immediate nested-load builder.
- There is no `AssetServer` helper to write a default loader metadata sidecar
  for a path.
- No generic `AsAssetId` protocol exists for arbitrary asset-holding ECS
  components.
- Direct `World` convenience helpers for add/load/load-with-settings are absent;
  use `AssetServer` and `Assets<T>` resources.
- Asset-server diagnostics await a diagnostics subsystem.
- `AssetInfos` uses `unordered_map<UntypedAssetId, AssetInfo>`; replacing it
  should be driven by profiling rather than parity alone.
