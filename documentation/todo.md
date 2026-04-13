# Documentation TODO — Cross-module tracker

Tracks outstanding implementation gaps found during documentation audits.
Per-module details live in each module's `documentation/<module>/todo.md`.

---

| Module   | Item                                                                                                             | Severity |
| -------- | ---------------------------------------------------------------------------------------------------------------- | -------- |
| `core`   | `set_table` hook commented out across all `WorldQuery<T>` — table-level iteration not implemented                | perf     |
| `core`   | No user-facing API for required-component registration (`App` method or static `require_components` hook)        | feature  |
| `assets` | ~~`.meta` serialization~~ — **resolved**: binary round-trip via zpp::bits fully wired into load/process pipeline | done     |
| `assets` | `AssetReader` interface is synchronous; coroutine/async reads planned but not implemented                        | perf     |
| `assets` | `NestedLoader` immediate mode missing (`immediate()` + `with_reader()`)                                          | feature  |
| `assets` | `AssetServer::write_default_loader_meta_file_for_path` + `WriteDefaultMetaError` missing                         | feature  |
| `assets` | `AsAssetId` concept missing — no ECS component protocol for asset-holding components                             | feature  |
| `assets` | `DirectAssetAccessExt` missing — no `World`-level `add_asset / load_asset` helpers                               | feature  |
| `assets` | `publish_asset_server_diagnostics` system missing                                                                | feature  |
| `assets` | `AssetInfos::infos` uses `unordered_map` instead of SlotMap — potential perf gap                                 | perf     |

See [documentation/core/todo.md](core/todo.md) and [documentation/assets/todo.md](assets/todo.md) for full details.
